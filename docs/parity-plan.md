# Parity Restoration Plan

Purpose: track the work required to bring the reconstructed Oblivion game DLL to feature parity with the retail binary. Status values will be updated as each milestone finishes.

## Current parity gaps (audit snapshot)
- **Spawn coverage** – HLIL manifests show core Quake II items, weapons, keys, and trigger/target classes missing from `g_spawn.c`, so retail maps drop entities during parsing.【F:docs/manifests/spawn_manifest_comparison.json†L2-L59】【F:docs/manifests/spawn_manifest_comparison.json†L18-L55】
- **Default seeds and spawnflag handling** – Moving brushes, mission helpers, and several monsters lack the default speed/accel/decel and AI pointer assignments present in the retail DLL; brush entities also diverge on spawnflag bit clearing (e.g., `func_door`).【F:docs/manifests/spawn_manifest_comparison.json†L65-L183】
- **Monster AI completeness** – Cyborg, spider, and kigrax behaviour still differs from the HLIL snapshot (collapsed mmove tables, simplified attack/pain handling, missing salvo/hull toggles).【F:docs/monster_ai_discrepancies.md†L6-L82】【F:docs/monster_ai_discrepancies.md†L84-L180】
- **Mission/path actors** – `misc_actor` and related mission scripting paths add hidden defaults (e.g., injected `"Yo Mama"` targetname, START_ON spawnflag) and broadened timers that need verification across spawn, save, and think loops.【F:docs/oblivion_reverse_analysis.md†L35-L116】【F:docs/oblivion_reverse_analysis.md†L117-L202】

## Work plan (ordered and task-oriented)
- [x] **Draft and check in the parity plan** – Capture the current audit state and ordered milestones in this document.
- [ ] **Rebuild spawn table parity**
  - [ ] Enumerate all missing `SP_` entries from the HLIL manifest and map each to source file locations.
  - [ ] Implement stubs or full spawners for retail items, weapons, keys, triggers, and targets in `g_spawn.c`.
  - [ ] Regenerate spawn manifests and compare against `spawn_manifest_comparison.json` to confirm coverage.
  - [ ] Add regression tests that fail if future spawner omissions reappear.
- [ ] **Restore spawn defaults and spawnflag semantics**
  - [ ] Port default speed/accel/decel and AI pointer wiring for movers, cameras, and monsters noted in the manifest diff.
  - [ ] Mirror retail spawnflag bit mutations (e.g., `func_door` clears) and document rationale inline.
  - [ ] Refresh manifests to verify default values match the retail DLL.
  - [ ] Extend tests to assert on key defaults and spawnflag state transitions.
- [ ] **Complete monster AI parity (cyborg, spider, kigrax)**
  - [ ] Reconstruct HLIL mmove tables and attack/pain sequences for each monster.
  - [ ] Restore special behaviours (kigrax salvo/hull toggles, spider/cyborg audio and timing tables).
  - [ ] Add focused AI regression tests per monster to lock behaviour timelines.
  - [ ] Validate against HLIL traces and update documentation with any remaining deltas.
- [ ] **Verify mission actor/controller parity**
  - [ ] Align `misc_actor`/`target_actor` spawn defaults, START_ON handling, and injected targetnames.
  - [ ] Confirm mission timer/state persistence through save/load and multi-client messaging paths.
  - [ ] Add mission-script regression coverage for spawn, think, and completion loops.
  - [ ] Re-run map-level checks to ensure actor entities spawn and behave identically.
- [ ] **Full-game regression and demo validation**
  - [ ] Rebuild DLL and run spawn/AI/mission regression suites.
  - [ ] Capture parity evidence via manifest diffs and (when possible) demo playback.
  - [ ] Log remaining gaps and backfill earlier milestones as needed.

**Next action when working the plan:** begin the "Rebuild spawn table parity" milestone by enumerating missing `SP_` entries and mapping them to source files.
