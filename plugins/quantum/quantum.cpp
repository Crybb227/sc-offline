// SPDX-License-Identifier: GPL-3.0-only
#include "controls.h"
#define TRAVEL_QUANTUM_CONTROLS
#include "../spatial/travel_plugin.h"
#include "quantum.h"
namespace sco::features::quantum {
inline void RefreshPanel() { sco::features::travel::implementation::Panel(); }
}
namespace sco::features::quantum_plugin {
const sco_plugin_info* Query() { return travel::Query(); }
sco_result Load(const sco_api* api, sco_plugin* self) {
    const auto travel=sco::features::travel::Load(api,self);
    if (travel!=SCO_OK) return travel;
    try {
        const auto control=sco::features::quantum::Load(api,self);
        if (control!=SCO_OK) { sco::features::travel::Unload(); return control; }
        sco::features::quantum::RefreshPanel(); return SCO_OK;
    } catch (...) { sco::features::quantum::Unload(); sco::features::travel::Unload(); return SCO_FAILED; }
}
void Unload() noexcept {
    sco::features::quantum::Unload(); sco::features::travel::Unload();
}
}
