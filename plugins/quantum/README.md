# Quantum control and Travel — SDK-only G8/G3 consumer

The external `quantum` folder owns commands, input policy and the retained **Travel** panel. It uses only extracted public SDK headers. The host does not compile/start its old `src/quantum.cpp` hooks, and its global quantum cvar keeper no longer runs. Legacy source is retained inactive; there is no built-in or private-pointer fallback when plugins are off.

## Approved controls and current engine blocker

G8 control is approved for R1. Core's `game.quantum` / `sc_quantum_v1` activates its capability only with an explicitly configured complete game-pack backend and installed owner cleanup. **The default production engine adapter is still absent.** Signature matches and historical offsets do not certify local-drive ownership, offline admission or reversible charging/audio/effect teardown. Consequently default production controls still refuse. This module is an implemented consumer, **not completed boost/effects/NAV gameplay parity**.

| Command | Contract |
| --- | --- |
| `quantum.enable <on:bool>` | Explicit owner-scoped enable/disable; no load-time auto-enable |
| `quantum.hold <milliseconds:int>` | 1..1000 ms inclusive; fresh capability/service lookup; provider rechecks thread/owner/drive/offline/EAC |
| `quantum.release` | Owner-only hold cancellation, even after the capability closes |
| `quantum.boost` | Toggle hold for at most 1000 ms without refresh; optional `ctrl+alt+b` binding |
| `quantum.control_status` | Copied bounded phase/block/progress/owner state; unknown/unavailable is explicit |

After explicit enable, the Windows game-thread tick can refresh **150 ms** hold leases every **50 ms** while Caps Lock is physically held and this process owns the foreground window. `caps_input` defaults true. `mouse_input` defaults **false**; enabling it explicitly treats the primary mouse button as hold input while focused. It **does not infer NAV mode** or intercept the game's B/engage action: leave it off for ordinary weapon firing. Native NAV/action forwarding requires an admitted game-pack adapter and remains pending. Non-Windows builds do not poll input.

Charging, drive input forwarding, audio handles/effect state and restoration remain **G8 provider responsibilities**, not arbitrary plugin effect requests or raw writes. The public contract has no effect-tag, sound-ID, arbitrary action, cvar or game-pointer route. Do not copy legacy effects into the consumer to bypass the missing adapter. G7 cvar writes remain unapproved.

Every callback extent is checked before use, tables are reopened per operation, durations/arguments/copied observations are validated, and the plugin never steals another owner's control. Capability/admission loss stops input refresh and requests owner cancellation. Ordinary unload unsubscribes, removes its hotkey and requests cancellation/disable for its own lease; the provider's actual owner-release hook remains authoritative for quarantine/fault cleanup. Failed cleanup is not a reload-safety claim.

## Existing travel and storage compatibility

Retains `quantum.travel(place, altitude)`, `quantum.bookmark(name)`, `quantum.save_bookmark(name)` and `quantum.scan`. `quantum.travel` and `quantum.bookmark` move you through sco-core's public **`game.relocation` 1.0**: the plugin's `relocation` setting is checked, then the capability `game.relocation` fresh, then the arguments; one request goes out (zone id, pose local to that zone, 30 s timeout), and the plugin follows the ticket each game tick and reports arrival or the failure/cancellation reason once. One move at a time; tickets are never persisted. `quantum.travel` finds the destination zone by its entity name and asks for a point at radius + altitude (100..20000 m) along that zone's own +Z axis, identity rotation; `quantum.bookmark` resolves a saved bookmark's exact/partial pose. `game.relocation` moves only within your current star system and only with sco-core's offline admission, a verified engine backend, a live player and a known system, which needs this release's host, **sco-core main e616ed6 or later**; without those conditions moves answer unavailable. The `relocation` setting (bool, default true, "Allow moving the player (offline only)") turns moves off for this plugin; `relocation = off` in `sc-offline.ini` turns them off for every plugin once the host pin has `SetRelocationEnabled`. `quantum.status`, `quantum.delete_bookmark(name)` and `quantum.inspect_bookmark(name)` remain unchanged. `quantum.scan` copies at most 256 observed zones, 64 per page, with epoch checks; it is not exhaustive system discovery. Unknown system/root/name never authorizes movement; the provider decides the star system.

Legacy destinations remain `system | display name | entity name | radius` in `locations.txt` / `locations_found.txt`. Bookmarks remain `bookmark system|name` followed by `spot x y z zone` lines. Optional `pose x y z w` records preserve orientation; old readers ignore them and old files default to identity. `data_root` selects the legacy root (default `data`). Owner-scoped `quantum.db` keeps the exact `bookmarks(pos,system,name,spots)` schema and `bookmarks.txt.time` marker. Windows markers remain decimal FILETIME. Import/update code is unchanged; absent storage retains text fallback and failed DB transactions do not overwrite it.

The optional `sco.widgets` panel retains ID `quantum.travel` and now has title **Travel**, combining paged destination/bookmark lists, altitude, bookmark save/delete/read-only inspection and observed scan with bounded G8 controls. No host/ImGui include or renderer callback is added. The panel's Travel and Go-to-bookmark buttons use the same commands. Missing widgets leaves command-only operation.

## Build, install and focused evidence

Use the published `sdk-v2.3.0` archive (it contains `sc_quantum.h`, `sc_game_spatial.h`, `sc_relocation.h` and `sco_widgets.h`). The release workflow stages quantum enabled through `tools/check_quantum_sdk.py`, which builds against that archive outside the checkout, runs the module workflow below, traces compiler dependencies, runs boundary negatives and a relocation red arm (POSIX), and runs the SDK's `sco-plugin-check` with no game services. That is not a game proof.

```sh
cmake -S plugins/quantum -B build-quantum -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/include -DSCO_QUANTUM_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-quantum --target quantum-workflow-test
ctest --test-dir build-quantum -R '^quantum-workflow$' --output-on-failure
cmake --install build-quantum --prefix /path/to/product --component quantum
```

Installed `data/plugins/quantum/` contains the native module and enabled manifest; ordinary folder disable and `plugins=off` semantics apply. Native modules are trusted code, GPL-3.0-only.

**Observed:** macOS AppleClang compiled/linked the actual module with `-Wall -Wextra -Werror`; one dynamically loaded synthetic public-provider workflow passed (legacy IDs/Travel composition, no startup writes, absent/physically truncated/null callbacks, bounded holds, capability-loss cancellation, the `relocation` switch, the `game.relocation` capability, one request with tick tracking to complete and failed, widget routes, ordinary unload twice/reload). This fixture does not execute the real game-pack adapter or its release hook, Windows input, database transactions or engine effects. **Windows/MSVC, real renderer, storage import twice, native detours, NAV/action forwarding, owned drive/charge/audio/effect restoration, every in-game move and where `quantum.travel` actually puts you, and actual gameplay: NOT VERIFIED; not run in game.**
