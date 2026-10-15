// SPDX-License-Identifier: GPL-3.0-only
#define TRAVEL_TELEPORT
#include "../spatial/travel_plugin.h"
#include "teleport.h"
namespace sco::features::teleport_plugin {
const sco_plugin_info* Query() { return travel::Query(); }
sco_result Load(const sco_api* api, sco_plugin* self) { return travel::Load(api, self); }
void Unload() noexcept { travel::Unload(); }
}
