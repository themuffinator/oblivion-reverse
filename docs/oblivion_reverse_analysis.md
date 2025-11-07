# Oblivion Reverse Engineering Notes

This document summarizes the initial reconnaissance performed on the Oblivion Quake II mod. It consolidates the Binary Ninja HLIL observations, Quake II source review, and the generated diff between the Oblivion and stock Quake II game DLLs.

## Repository layout

The checked-in source tree now retains only the game DLL under `src/game/`. Engine-specific clients, renderers, and platform layers have been removed from this repository and should be consulted in the vanilla Quake II drop preserved at `references/source-code/Quake-2-master/` when engine behaviour needs to be cross-referenced.

## Binary metadata comparison

### Shared characteristics
- Both DLLs export only the `GetGameAPI` entry point required by the Quake II engine.
- The HLIL reports consistent PE section structure (``.text``, ``.rdata``, ``.data``, ``.reloc``), confirming a conventional Visual C++ build layout.

### Divergent characteristics
- **Toolchain signatures**: Oblivion reports mixed Visual Studio 6.0/7.1 and MASM fingerprints, while the stock DLL identifies as Visual Studio 97.
- **Imports**: Oblivion depends on 75 imports, compared with 270 imports in the stock Quake II build.
- **PE header values**:
  - Oblivion relocates to image base `0x10000000` with `0x1000` file alignment and larger code/data sections.
  - Stock Quake II uses image base `0x20000000` with `0x200` file alignment.
- **Global structure sizes**: Oblivion advertises `edict_size = 0x3d0`, expanding the standard Quake II `edict_t` size (`0x37c`).

## Game API wiring

Both HLIL dumps confirm the expected game module contract:

1. `GetGameAPI` copies the engine-provided `game_import_t` table into the DLL's globals.
2. The function fills a `game_export_t` structure with DLL function pointers and state.
3. The populated export block is returned to the engine.

Differences surfaced in the Oblivion binding:

- `RunFrame` and `ServerCommand` callbacks dispatch to new helper functions absent in the stock DLL.
- The larger `edict_size` implies entity iteration must stride by `0x3d0`, evidenced by Oblivion's frame loop (`sub_1000d5a0 → sub_1000d940`).
- Oblivion's entity factory routines place target pointers at offset `0x238`, compared to Quake II's historical `0x1f8` location, signaling additional per-entity fields.

## Runtime behaviour highlights

- Oblivion's frame loop checks `edict_t` records using the 0x3d0 stride and conditionally dispatches per-frame logic, mirroring Quake II's legacy loop but accounting for the enlarged structure.
- Server console command handling still recognizes `test`, `addip`, `removeip`, `listip`, and `writeip`, providing continuity with Quake II's administrative command set.
- The observed layout shifts will require alignment with the expanded struct fields when reconstructing the C source.

## Reference source alignment

Consulting the open-source Quake II tree (`references/source-code/Quake-2-master/`):

- `game.def` exports only `GetGameAPI`, confirming the single-entry DLL model seen in both HLIL outputs.
- `game_export_t` (see `src/game/game.h`) documents the expected callback order and global state fields that Oblivion also fills, albeit with different implementations for several slots.
- `edict_t` (also defined in `src/game/game.h`) provides the baseline layout. Oblivion's larger `edict_size` indicates that the mod appends fields beyond the stock structure definition.
- `cl_main.c` and `qcommon/common.c` demonstrate how the engine consumes the game DLL, anchoring future parity work between the reconstructed source and the reference engine interfaces.

## HLIL diff artifact

A unified diff between the two HLIL dumps (generated with `git diff --no-index references/HLIL/quake2/gamex86.dll_hlil.txt references/HLIL/oblivion/gamex86.dll_hlil.txt`) serves as a mechanical reference for:

- PE header differences and import table reductions.
- Newly introduced helper functions and control flow adjustments in Oblivion.
- Structural changes implied by altered offsets and sizes in entity-handling code.

Keep this document updated as deeper reverse-engineering yields more precise mappings or corrections.

## Control mappings

- The default key layout reserves the `r` key for RTDU deployment via the `use rtdu` command, matching the packaged configuration defaults.

## `misc_actor` reverse-engineering notes

### Required keys and spawnflags

- `target` continues to drive the scripted path: `sub_1001f380` resolves the string via `sub_1001ad80`, verifies the destination classname is `target_actor`, and aims the actor toward the node before clearing the key. Failing the lookup falls back to the idle routine instead of aborting the spawn, so the translation must mirror that graceful behaviour.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24004-L24056】
- `targetname` becomes optional: when it is absent the spawn path writes the literal `"Yo Mama"` into the field and sets bit `0x20` in the spawnflags so the mission controller can still address the actor. Stock Quake II emits an error and frees the edict in this situation, so the Oblivion port needs to adopt the new defaulting rule.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24071-L24075】【F:src/game/m_actor.c†L440-L451】
- `target_actor` fields retain Quake II semantics. `sub_1001f690` honours the `JUMP`, `SHOOT`, `ATTACK`, `HOLD`, and `BRUTAL` bits, applies jump impulses and `AI_BRUTAL`/`AI_STAND_GROUND` flags, forwards `pathtarget` actions, and, when a `message` is present, relays it to every connected client via `data_1006c1c8`. This mirrors the stock handler but must be reimplemented against the expanded edict layout.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24146-L24211】【F:src/game/m_actor.c†L512-L599】

### Spawn-time defaults

- Movement-related fields and hull are hard-coded: the live branch copies the player-sized {-16, -16, -24}…{16, 16, 32} mins/maxs blob, writes `2` and `5` into the physics slots at offsets `0xf8` and `0x104`, and seeds the animation pointer at `0x358` with the stand mmove before calling the walkmonster bootstrapper. The corpse spawnflag flips to the prone hull blob, forces `health = -1`, `deadflag = 2`, and marks the edict as a corpse before linking.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24080-L24110】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24133-L24135】
- Health defaults to 100 (`0x64`) when missing, `mass` is set to 200, and the Wimpy flag check bypasses the `AI_GOOD_GUY` bit write at `*(self+0x35c)` so that fragile actors stay aggressive. These values diverge from the Quake II source, which only touches `health`, `max_health`, and `mass`, so the Oblivion translation must respect the additional bitfield writes.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24079-L24135】【F:src/game/m_actor.c†L460-L484】
- Function pointers are wired explicitly: `use` → `sub_1001f380`, `pain/reached` → `sub_1001ef70`, `die` → `sub_1001f220`, `stand`/`walk`/`run` → `sub_1001eeb0`/`sub_1001ef00`/`sub_1001ef10`, and the think loop → `sub_1001f340`. After `sub_10012b30` (the walkmonster wrapper) runs, setting the `START_ON` flag triggers the `use` callback immediately, replacing the stock “dormant until used” rule.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24113-L24142】【F:src/game/m_actor.c†L487-L494】

### Mission scripting hooks

- `sub_1001ef70` owns the per-node pause timer, faces the actor toward the `target_actor`, randomises between taunt/flipoff/idle mmoves, and prints one of three flavour barks when the node supplies a `message`, extending the stock implementation that always resumed walking immediately.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L23809-L23878】
- `sub_1001f340` schedules the follow-up think 1–2 s in the future and switches to the idle loop, while `sub_1001f300`/`sub_1001f0f0` feed the path helper that honours HOLD/BRUTAL directives and enemy assignment on scripted targets.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L23882-L23999】
- The touch handler also broadcasts mission text to every active client (`data_1006c1c8` loop) instead of the single `gi.cprintf` used in Quake II, so the translation must escalate the messaging scope accordingly.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L24146-L24176】【F:src/game/m_actor.c†L524-L535】
