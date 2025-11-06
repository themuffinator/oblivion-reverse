# Oblivion Reverse Engineering Notes

This document summarizes the initial reconnaissance performed on the Oblivion Quake II mod. It consolidates the Binary Ninja HLIL observations, Quake II source review, and the generated diff between the Oblivion and stock Quake II game DLLs.

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
