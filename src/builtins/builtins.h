#pragma once
// sc-offline's built-in plugins: its features as plugins compiled into the DLL (sco-core's
// docs/framework.md, Phase 4). Each is a sco::plugins::Builtin, the three plugin functions listed
// in a table instead of exported from the DLL. The shell registers none: G1 early DataCore refuses
// builtin inputs, so every feature ships as an external plugin folder. These helpers remain for
// the unbuilt *_plugin.cpp sources tools/check.sh still parses.
#include "sco/plugins.h"
#include "../version.h"   // SCO_VERSION: every built-in reports it in its sco_plugin_info
#include <cstdint>
#include <cstring>

// Contracts ships as the external plugins/contracts folder; no built-in fallback.
// Multiplayer uses the approved G13 broker as an external module, never this registry.
// Bridges remain independent disabled external modules.

// External features never have a host fallback.

// The index of the entry named `name` (any case) in a list of count names, or -1.
inline int FindBuiltinName(int count, const char* (*nameAt)(int), const char* name) {
    for (int i = 0; i < count; ++i)
        if (_stricmp(nameAt(i), name) == 0) return i;
    return -1;
}

// One argument of a built-in's command.
inline sco_arg_def BuiltinArg(const char* name, sco_arg_type type, const char* help) {
    sco_arg_def a = {};
    a.name = name;
    a.type = type;
    a.help = help;
    return a;
}

// Registers one of a built-in's commands, gated on its capability (the built-in's id).
inline sco_result RegisterBuiltinCommand(const sco_api* api, sco_plugin* self, const char* capability, const char* name,
                                         const char* title, const char* help, sco_command_fn fn,
                                         const sco_arg_def* args = nullptr, uint32_t nargs = 0) {
    sco_command c = {};
    c.size = sizeof(c);
    c.name = name;
    c.title = title;
    c.help = help;
    c.capability = capability;
    c.args = args;
    c.nargs = nargs;
    c.arg_def_size = sizeof(sco_arg_def);
    c.fn = fn;
    return api->register_command(self, &c);
}
