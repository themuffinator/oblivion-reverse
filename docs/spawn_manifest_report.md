# Spawn manifest comparison report

This report summarizes the manifest that `tools/extract_spawn_manifest.py` now
builds from the Oblivion retail `gamex86.dll` HLIL dump and the current
reimplementation under `src/game/`. The script emits a consolidated manifest at
`docs/manifests/spawn_manifest.json` and a diff-centric view at
`docs/manifests/spawn_manifest_comparison.json`.

## Overview

* `tools/extract_spawn_manifest.py` parses Quake II-style `field_t` metadata,
  spawn tables, and spawn functions in the HLIL text and cross-references the
  active `SP_` implementations in this repository. The script also walks the C
  sources to recover default assignments and spawnflag mutations so that we can
  diff those behaviours against the original binary.
* `tests/test_spawn_manifest.py` snapshots the generated data to prevent drift
  and ensure future contributors rerun the extractor when the engine changes.

## Entities present in HLIL but missing from the repo

The refreshed comparison manifest now highlights only a short list of HLIL-only
class names without a modern spawn implementation: the mission-critical
`key_power_cube`, heavy ordnance (`detpack`, `grenade`, `mine`), the Makron boss
(`monster_makron`), and a lingering `%s/listip.cfg` path literal. 【F:docs/manifests/spawn_manifest_comparison.json†L248-L255】
Bringing these entries across—or intentionally stubbing them—will prevent maps
from losing objective items or scripted encounters.

## Entities only present in the repo

`missing_in_hlil` remains extensive because the retail DLL lacks many of the
reconstructed Quake II staples present in this repo. That list spans worldspawn
helpers (e.g., `info_player_start`, `worldspawn`), interaction triggers
(`trigger_multiple`, `target_spawner`), utility entities (`misc_camera`,
`misc_banner`), and large portions of the monster roster (`monster_gladiator`,
`monster_spider`, `monster_sentinel`, etc.). 【F:docs/manifests/spawn_manifest_comparison.json†L129-L247】
Treat these as Oblivion-specific content until HLIL coverage confirms otherwise
so that documentation and regression tests capture their bespoke behaviour.

## Spawnflag mismatches

Several core movers and interaction points check or mutate different spawnflag
bits compared to the binary:

| Entity             | HLIL behaviour | Repo behaviour |
|--------------------|----------------|----------------|
| `func_door`        | No spawnflag checks recorded | Checks bits 1, 16, and 64 on spawn |
| `func_door_rotating` | No spawnflag checks recorded | Checks bits 1, 2, 16, 64, and 128 |
| `light`            | No spawnflag checks recorded | Tests bit 1 (`START_OFF`) |

(See `docs/manifests/spawn_manifest_comparison.json` under
`spawnflag_mismatches` for the exact bit sets.) 【F:docs/manifests/spawn_manifest_comparison.json†L256-L286】

These gaps need manual review—the extractor only records literal bitmasks so
any macro or logic change should be double-checked before porting behaviour.

## Default value drift

The `default_mismatches` block has narrowed to a handful of concrete gaps:

* **`light` toggles** – HLIL zeros the value at offset `0x58` (clearing the
  style/flag slot) while the repo leaves it untouched. 【F:docs/manifests/spawn_manifest_comparison.json†L2-L12】
* **Boss monster scaffolding** – `monster_jorg`, `monster_supertank`, and
  `monster_tank` share missing assignments at offsets `0x12c`, `0x140`, `0x2c4`,
  and `0x390`, suggesting additional bounding-box or AI pointer initialisation
  that needs to be restored. 【F:docs/manifests/spawn_manifest_comparison.json†L14-L127】

Porting these writes will ensure heavyweight encounters inherit the full set of
startup values from the retail DLL.

## Recommended follow-ups

1. **Restore the outstanding HLIL-only spawns.** Implement or intentionally stub
   `detpack`, `grenade`, `mine`, `key_power_cube`, `monster_makron`, and the
   `%s/listip.cfg` helper so retail maps retain their scripted objects.
   【F:docs/manifests/spawn_manifest_comparison.json†L248-L255】
2. **Confirm repo-only entities are Oblivion specific.** Review the extensive
   `missing_in_hlil` list and document which actors, triggers, and props are
   deliberate additions versus extraction gaps. 【F:docs/manifests/spawn_manifest_comparison.json†L129-L247】
3. **Align spawnflag checks.** Reconcile the additional bit tests the repo
   performs for `func_door`, `func_door_rotating`, and `light` against the HLIL
   behaviour. 【F:docs/manifests/spawn_manifest_comparison.json†L256-L286】
4. **Restore heavyweight defaults.** Port the missing initialisation writes for
   `light` and the boss monsters so their runtime state mirrors the binary.
   【F:docs/manifests/spawn_manifest_comparison.json†L2-L127】

With the snapshot test in place, future engine tweaks that touch spawn logic
must update the manifest, ensuring drift stays visible.
