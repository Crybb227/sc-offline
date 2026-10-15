#pragma once
#ifndef _WIN32
#error "admission_probe is Windows only; the decision core is admission_evidence.h"
#endif
#include "admission_evidence.h"

#include <string>

namespace admission {

// Reads the effective Windows Firewall state through INetFwPolicy2 into `out`. Rules whose ApplicationName
// differs from exePath are skipped (they can never count). Every failure leaves the matching ok flag false and
// records the HRESULT and stage in out.hr / out.stage. Never throws.
void ProbeFirewall(const std::wstring& exePath, FwSnapshot& out) noexcept;

// Reads the IPv4 and IPv6 TCP tables (TCP_TABLE_OWNER_PID_ALL) with bounded buffer retries. Failure leaves
// out.ok false and records the Win32 error and stage. Never throws.
void ProbeTcpTable(TcpSnapshot& out) noexcept;

// GetModuleFileNameW(nullptr); false on failure or truncation.
bool CurrentExePath(std::wstring& out) noexcept;

}
