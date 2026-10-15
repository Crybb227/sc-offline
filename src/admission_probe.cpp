#include "admission_probe.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <netfw.h>

#include <cstring>
#include <exception>
#include <vector>

namespace admission {
namespace {

constexpr size_t kMaxRules = 100000;
constexpr int kTcpAttempts = 8;
constexpr DWORD kTcpMaxBytes = 16u << 20;

// Local GUID copies so the build needs neither uuid.lib nor __uuidof support.
constexpr GUID kClsidNetFwPolicy2 = {0xe2b3c97f, 0x6ae1, 0x41ac, {0x81, 0x7a, 0xf6, 0xf9, 0x21, 0x66, 0xd7, 0xdd}};
constexpr GUID kIidNetFwPolicy2 = {0x98325047, 0xc671, 0x4174, {0x8d, 0x81, 0xde, 0xfc, 0xd3, 0xf0, 0x31, 0x86}};
constexpr GUID kIidNetFwRule = {0xaf230d27, 0xbaba, 0x4e42, {0xac, 0xed, 0xf5, 0x24, 0xf2, 0x2c, 0xfc, 0xe2}};
constexpr GUID kIidEnumVariant = {0x00020404, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { reset(); }
    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T** put() {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }
    T* operator->() const { return p_; }
    T* get() const { return p_; }

private:
    T* p_ = nullptr;
};

class Bstr {
public:
    Bstr() = default;
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    ~Bstr() { SysFreeString(b_); }
    BSTR* put() {
        SysFreeString(b_);
        b_ = nullptr;
        return &b_;
    }
    std::wstring str() const { return b_ ? std::wstring(b_, SysStringLen(b_)) : std::wstring(); }

private:
    BSTR b_ = nullptr;
};

void Fail(FwSnapshot& out, HRESULT hr, const char* stage) {
    if (out.hr == 0) {
        out.hr = static_cast<long>(hr);
        out.stage = stage;
    }
}

bool ReadText(INetFwRule* rule, HRESULT (STDMETHODCALLTYPE INetFwRule::*getter)(BSTR*), std::wstring& dst) {
    Bstr b;
    if (FAILED((rule->*getter)(b.put()))) return false;
    dst = b.str();
    return true;
}

bool InterfacesEmpty(INetFwRule* rule, bool& isEmpty) {
    VARIANT v;
    VariantInit(&v);
    if (FAILED(rule->get_Interfaces(&v))) return false;
    isEmpty = false;
    if (v.vt == VT_EMPTY || v.vt == VT_NULL) {
        isEmpty = true;
    } else if ((v.vt & VT_ARRAY) && v.parray) {
        LONG lo = 0, hi = -1;
        if (SUCCEEDED(SafeArrayGetLBound(v.parray, 1, &lo)) && SUCCEEDED(SafeArrayGetUBound(v.parray, 1, &hi))) {
            isEmpty = hi < lo;
        }
    }
    VariantClear(&v);
    return true;
}

bool ReadRuleBody(INetFwRule* rule, FwRuleView& v) {
    bool ok = true;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (SUCCEEDED(rule->get_Enabled(&enabled))) v.enabled = enabled != VARIANT_FALSE; else ok = false;
    NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
    if (SUCCEEDED(rule->get_Action(&action))) v.block = action == NET_FW_ACTION_BLOCK; else ok = false;
    NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_MAX;
    if (SUCCEEDED(rule->get_Direction(&dir))) v.direction = static_cast<int>(dir); else ok = false;
    LONG profiles = 0;
    if (SUCCEEDED(rule->get_Profiles(&profiles))) v.profiles = profiles; else ok = false;
    LONG protocol = -1;
    if (SUCCEEDED(rule->get_Protocol(&protocol))) v.protocol = protocol; else ok = false;
    ok = ReadText(rule, &INetFwRule::get_ServiceName, v.serviceName) && ok;
    ok = ReadText(rule, &INetFwRule::get_LocalPorts, v.localPorts) && ok;
    ok = ReadText(rule, &INetFwRule::get_RemotePorts, v.remotePorts) && ok;
    ok = ReadText(rule, &INetFwRule::get_LocalAddresses, v.localAddrs) && ok;
    ok = ReadText(rule, &INetFwRule::get_RemoteAddresses, v.remoteAddrs) && ok;
    ok = ReadText(rule, &INetFwRule::get_InterfaceTypes, v.interfaceTypes) && ok;
    ok = InterfacesEmpty(rule, v.interfacesEmpty) && ok;
    return ok;
}

bool EnumerateRules(INetFwPolicy2* policy, const std::wstring& exePath, FwSnapshot& out) {
    ComPtr<INetFwRules> rules;
    HRESULT hr = policy->get_Rules(rules.put());
    if (FAILED(hr) || !rules.get()) {
        Fail(out, FAILED(hr) ? hr : E_POINTER, "get_Rules");
        return false;
    }
    ComPtr<IUnknown> unk;
    hr = rules->get__NewEnum(unk.put());
    if (FAILED(hr) || !unk.get()) {
        Fail(out, FAILED(hr) ? hr : E_POINTER, "get__NewEnum");
        return false;
    }
    ComPtr<IEnumVARIANT> en;
    hr = unk->QueryInterface(kIidEnumVariant, en.putVoid());
    if (FAILED(hr) || !en.get()) {
        Fail(out, FAILED(hr) ? hr : E_POINTER, "QueryInterface(IEnumVARIANT)");
        return false;
    }
    for (;;) {
        VARIANT item;
        VariantInit(&item);
        ULONG fetched = 0;
        hr = en->Next(1, &item, &fetched);
        if (hr == S_FALSE || (SUCCEEDED(hr) && fetched == 0)) return true;
        if (FAILED(hr)) {
            Fail(out, hr, "IEnumVARIANT::Next");
            return false;
        }
        if (++out.rulesSeen > kMaxRules) {
            VariantClear(&item);
            Fail(out, E_FAIL, "rule count bound");
            return false;
        }
        FwRuleView view;
        {
            ComPtr<INetFwRule> rule;
            if (item.vt == VT_DISPATCH && item.pdispVal &&
                SUCCEEDED(item.pdispVal->QueryInterface(kIidNetFwRule, rule.putVoid())) && rule.get()) {
                Bstr app;
                const bool haveApp = SUCCEEDED(rule->get_ApplicationName(app.put()));
                if (haveApp) view.appName = app.str();
                if (haveApp && !SamePath(view.appName, exePath)) {
                    VariantClear(&item);
                    continue;
                }
                view.propsOk = haveApp && ReadRuleBody(rule.get(), view);
            }
        }
        VariantClear(&item);
        out.rules.push_back(std::move(view));
    }
}

void FillFirewall(const std::wstring& exePath, FwSnapshot& out) {
    ComPtr<INetFwPolicy2> policy;
    HRESULT hr = CoCreateInstance(kClsidNetFwPolicy2, nullptr, CLSCTX_INPROC_SERVER, kIidNetFwPolicy2, policy.putVoid());
    if (FAILED(hr) || !policy.get()) {
        Fail(out, FAILED(hr) ? hr : E_POINTER, "CoCreateInstance(NetFwPolicy2)");
        return;
    }
    long active = 0;
    hr = policy->get_CurrentProfileTypes(&active);
    if (FAILED(hr)) {
        Fail(out, hr, "get_CurrentProfileTypes");
        return;
    }
    out.activeProfiles = active & kProfileMask;
    out.profilesOk = true;
    for (long bit : {kProfileDomain, kProfilePrivate, kProfilePublic}) {
        if (!(out.activeProfiles & bit)) continue;
        VARIANT_BOOL enabled = VARIANT_FALSE;
        hr = policy->get_FirewallEnabled(static_cast<NET_FW_PROFILE_TYPE2>(bit), &enabled);
        if (FAILED(hr)) {
            out.profilesOk = false;
            Fail(out, hr, "get_FirewallEnabled");
            return;
        }
        if (enabled != VARIANT_FALSE) out.enabledProfiles |= bit;
    }
    out.stage = "get_Rules";
    out.rulesOk = EnumerateRules(policy.get(), exePath, out);
    if (!out.rulesOk) out.rules.clear();
}

void FailTcp(TcpSnapshot& out, ULONG err, const char* stage) {
    out.ok = false;
    out.rows.clear();
    if (out.error == 0) {
        out.error = static_cast<long>(err);
        out.stage = stage;
    }
}

bool ReadTcpFamily(ULONG family, TcpSnapshot& out) {
    std::vector<unsigned char> buf;
    DWORD size = 0;
    for (int attempt = 0; attempt < kTcpAttempts; ++attempt) {
        ULONG rc = GetExtendedTcpTable(buf.empty() ? nullptr : buf.data(), &size, FALSE, family,
                                       TCP_TABLE_OWNER_PID_ALL, 0);
        if (rc == ERROR_INSUFFICIENT_BUFFER) {
            if (size == 0 || size > kTcpMaxBytes - size / 4 - 4096) {
                FailTcp(out, rc, "GetExtendedTcpTable size bound");
                return false;
            }
            size += size / 4 + 4096;
            buf.assign(size, 0);
            continue;
        }
        if (rc != NO_ERROR) {
            FailTcp(out, rc, family == AF_INET ? "GetExtendedTcpTable(AF_INET)" : "GetExtendedTcpTable(AF_INET6)");
            return false;
        }
        if (buf.size() < sizeof(DWORD)) {
            FailTcp(out, ERROR_INVALID_DATA, "GetExtendedTcpTable returned no buffer");
            return false;
        }
        DWORD n = 0;
        std::memcpy(&n, buf.data(), sizeof(n));
        if (family == AF_INET) {
            const size_t head = offsetof(MIB_TCPTABLE_OWNER_PID, table);
            if (n > (buf.size() - head) / sizeof(MIB_TCPROW_OWNER_PID)) {
                FailTcp(out, ERROR_INVALID_DATA, "AF_INET table size mismatch");
                return false;
            }
            const auto* t = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < n; ++i) {
                TcpRow row;
                row.v6 = false;
                std::memcpy(row.remote, &t->table[i].dwRemoteAddr, 4);
                row.state = t->table[i].dwState;
                row.pid = t->table[i].dwOwningPid;
                out.rows.push_back(row);
            }
        } else {
            const size_t head = offsetof(MIB_TCP6TABLE_OWNER_PID, table);
            if (n > (buf.size() - head) / sizeof(MIB_TCP6ROW_OWNER_PID)) {
                FailTcp(out, ERROR_INVALID_DATA, "AF_INET6 table size mismatch");
                return false;
            }
            const auto* t = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < n; ++i) {
                TcpRow row;
                row.v6 = true;
                std::memcpy(row.remote, t->table[i].ucRemoteAddr, 16);
                row.state = t->table[i].dwState;
                row.pid = t->table[i].dwOwningPid;
                out.rows.push_back(row);
            }
        }
        return true;
    }
    FailTcp(out, ERROR_INSUFFICIENT_BUFFER, "GetExtendedTcpTable retries exhausted");
    return false;
}

}

void ProbeFirewall(const std::wstring& exePath, FwSnapshot& out) noexcept {
    try {
        out = FwSnapshot{};
        out.stage = "CoInitializeEx";
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
            Fail(out, init, "CoInitializeEx");
            return;
        }
        FillFirewall(exePath, out);
        if (SUCCEEDED(init)) CoUninitialize();
    } catch (...) {
        out.profilesOk = false;
        out.rulesOk = false;
        out.rules.clear();
        Fail(out, E_FAIL, "exception");
    }
}

void ProbeTcpTable(TcpSnapshot& out) noexcept {
    try {
        out = TcpSnapshot{};
        out.stage = "GetExtendedTcpTable";
        out.ok = ReadTcpFamily(AF_INET, out) && ReadTcpFamily(AF_INET6, out);
        if (!out.ok) out.rows.clear();
    } catch (...) {
        FailTcp(out, ERROR_OUTOFMEMORY, "exception");
    }
}

bool CurrentExePath(std::wstring& out) noexcept {
    try {
        std::wstring buf(MAX_PATH, L'\0');
        for (int i = 0; i < 8; ++i) {
            const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) return false;
            if (n < buf.size()) {
                buf.resize(n);
                out = std::move(buf);
                return true;
            }
            if (buf.size() >= 32768) return false;
            buf.assign(buf.size() * 2, L'\0');
        }
    } catch (...) {
    }
    return false;
}

}
