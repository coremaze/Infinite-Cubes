# Cube World

`cubeworld.exe` of Cube World 1.0.0-1, Windows x64.

## Requirements

- CMake 3.20 or newer
- Python 3
- One of:
  - Linux: MinGW-w64 x86_64 GCC with posix threads (`gcc-mingw-w64-x86-64-posix`, `g++-mingw-w64-x86-64-posix`) and Ninja
  - Windows: Visual Studio 2022 with the C++ workload and the Windows SDK
- Internet access at the first configure, which downloads FreeImage (pinned by SHA-256)

## Build

MinGW (Linux):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake
cmake --build build
```

Visual Studio 2022 (developer command prompt):

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

Options:

| option | default | effect |
| --- | --- | --- |
| `-DSTEAMWORKS_SDK=DIR` | none | link the real Steamworks SDK (put its `steam_api64.dll` next to the executable); without it, a built-in offline stand-in is used and the game is local only |
| `-DREBUILD_SHADERS=ON` / `OFF` | `ON` when fxc is found | compile the HLSL with fxc / use the checked-in shader bytecode |
| `-DFXC_EXECUTABLE=PATH` | | the fxc to use |

Output: `build/cubeworld.exe` (or `build-msvc/Release/cubeworld.exe`).

## Run

Copy the data from your own installation next to the executable:
- `logo.bmp`, the `.plx` files, `resource1.dat`, `resource2.dat`, the `dict_*.xml` files;
- `sprites/`, `sounds/`, `music/`.

Then create an empty `Save/` directory.
