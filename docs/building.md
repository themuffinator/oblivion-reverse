# Building the game module

This repository now ships with a CMake build that targets the Quake II game DLL/SO located under `src/game/`.

## Prerequisites

- A C11-capable compiler (tested with GCC 11.4 on Ubuntu 22.04)
- [CMake](https://cmake.org/) 3.16 or newer
- Build tools supported by CMake (for example `ninja` or GNU Make)

## Configure and build (Linux / macOS)

```bash
cmake -S . -B build
cmake --build build
```

The resulting shared object is placed in `build/` as `game.so` (or `game.dylib` on macOS).

## Configure and build (Windows)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The resulting DLL will be emitted as `build/Release/game.dll`.

### Notes

- The build script automatically includes every `.c` file located in `src/game/` and produces a position-independent shared module named `game`.
- The Windows build uses the existing `game.def` export definition file so that the exports match the original SDK.
- You can change the build directory (`build` in the commands above) to any location you prefer.
- To install the compiled library, run `cmake --install build` and look under `lib/` (Linux/macOS) or `bin/` (Windows) inside the installation prefix.

## Validation

The Linux instructions above were validated on Ubuntu 22.04 using GCC 11.4 and CMake 3.22.1. The build completed successfully and produced `build/game.so`.
