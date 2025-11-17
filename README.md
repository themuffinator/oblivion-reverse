# Oblivion Reverse Mod

## Overview
Oblivion Reverse is a community-driven effort to reverse engineer and document the classic *Oblivion* Quake mod. This repository aggregates source code recreations, research notes, and tooling used to understand the original mod's behavior. The project aims to preserve the mod, provide educational insight into its inner workings, and enable future enhancements that remain faithful to the original gameplay experience.

## Repository Structure
- `src/` – Core reverse-engineered source code for the mod.
- `pack/` – Asset packs and data extracted from the original release.
- `docs/` – Project documentation, including research notes and design references.
- `references/` – Collected reference material, such as decompiled scripts and technical analysis.
- `CMakeLists.txt` – Build configuration for generating the mod binaries and tools.

## Getting Started
### Prerequisites
- A C++20-compatible compiler (e.g., GCC, Clang, or MSVC).
- CMake 3.20 or later.
- Python 3.9+ for auxiliary tooling and scripts located in `docs/` and `references/`.
- Git LFS for handling large binary assets in the `pack/` directory.

### Cloning the Repository
```bash
git clone https://github.com/<your-account>/oblivion-reverse.git
cd oblivion-reverse
git submodule update --init --recursive
```

### Building the Project
1. Create a build directory:
   ```bash
   cmake -S . -B build
   ```
   On Windows, Quake II expects a 32-bit Release `gamex86.dll`, so be sure to
   configure CMake for a Win32 generator:
   ```bash
   cmake -S . -B build -A Win32
   ```
2. Configure the build:
   ```bash
   cmake --build build --config Release
   ```
3. The compiled binaries and tools will be located under `build/`.

### Running the Mod
1. Copy the generated binaries from `build/` into your Quake installation directory.
2. Launch Quake with the following command-line options:
   ```bash
   quake -game oblivion
   ```
3. The mod should now load with the reverse-engineered content. Refer to the original documentation in `docs/` for gameplay details.

## Contributing
Contributions are welcome. Please follow these guidelines:
1. Fork the repository and create a new branch for your feature or bugfix.
2. Adhere to the existing code style and add comments where necessary.
3. Include tests or validation steps for code changes when possible.
4. Submit a pull request describing the rationale behind your changes.

## Credits
- Original *Oblivion* mod by Team Evolve. Official page: [https://www.celephais.net/oblivion/main.html](https://www.celephais.net/oblivion/main.html).
- Reverse engineering, tooling, and documentation by the Oblivion Reverse community.

## License
This project is distributed under the terms of the license found in the [LICENSE](LICENSE) file. Ensure that any contributions comply with the project's licensing requirements.
