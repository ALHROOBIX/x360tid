# x360tid v1.0.0

**Xbox 360 Title ID Extraction Tool**

A high-performance, cross-platform C++17 CLI tool for extracting Title IDs
and metadata from all Xbox 360 game file formats. Zero compile-time dependencies
when built with static zstd.

---

## Features

- **10 format support** — XEX, CON/LIVE/PIRS, GDFX/ISO, STFS, SVOD, ZArchive, XSF, XDBF/GPD
- **Title name extraction** — Reads game names from SPA/XDBF resources and UTF-16BE scanning
- **Cross-platform** — Linux & Windows (MinGW cross-compile or MSVC)
- **Static build** — Bundle zstd decompressor directly, no external DLL/SO needed
- **Multi-threaded** — Parallel batch processing with configurable thread pool
- **Multiple output** — Human-readable table, JSON, or CSV
- **Zero copy** — Memory-mapped file I/O for maximum throughput
- **Safe** — Bounds checking on every buffer read, no undefined behavior

---

## Supported Formats

| Format | Extension | Title ID Source | Name Extraction |
|--------|-----------|-----------------|-----------------|
| XEX1/XEX2 | `.xex` | Execution Info header | SPA/XDBF Resource |
| CON | `.con`, `.pkg` | XContent metadata | XContent displayName |
| LIVE | `.live` | XContent metadata | XContent displayName |
| PIRS | `.pirs` | XContent metadata | XContent displayName |
| GDFX/ISO | `.iso` | Embedded default.xex | SPA/XDBF Resource |
| STFS | (container) | File enumeration | XContent displayName |
| SVOD/GOD | `.data/` | Block mapping | Via embedded XEX |
| ZArchive | `.zar` | ZSTD decompression → XEX | SPA/XDBF Resource |
| XSF | `.xsf` | Embedded default.xex | SPA/XDBF Resource |
| XDBF/GPD | `.gpd` | Entry parsing | XDBF title entry |

---

## Installation

### Linux

#### Option 1: Build from source (dynamic zstd — requires `libzstd` at runtime)

```bash
# Install build dependencies
sudo dnf install g++ libzstd         # Fedora
sudo apt install g++ libzstd-dev     # Ubuntu/Debian

# Build
make

# Install system-wide (optional)
sudo make install
```

#### Option 2: Build from source (static zstd — no external library needed)

```bash
# Only g++ needed, no libzstd required
sudo dnf install g++     # Fedora
sudo apt install g++     # Ubuntu/Debian

# Build with static zstd
make linux-static

# Run
./x360tid-static game.iso
```

#### Option 3: Download pre-built binary

```bash
chmod +x x360tid-static-linux
./x360tid-static-linux game.iso
```

### Windows

#### Option 1: Cross-compile from Linux (recommended)

```bash
# Install MinGW cross-compiler
sudo dnf install mingw64-gcc-c++                          # Fedora
sudo apt install g++-mingw-w64-x86-64-posix               # Ubuntu/Debian

# Build standalone .exe (zstd built-in, no DLL needed)
make win-static
```

#### Option 2: Build with MSVC on Windows

```cmd
cl /std:c++17 /O2 /EHsc /DX360TID_STATIC_ZSTD /DZSTD_DISABLE_ASM ^
   /Ideps\zstd /Ideps\zstd\common /Ideps\zstd\decompress ^
   src\main.cpp deps\zstd\common\*.c deps\zstd\decompress\*.c
```

#### Option 3: Dynamic zstd (needs `zstd.dll` next to the exe)

```bash
# Cross-compile with dynamic zstd loading
make win

# On Windows: place zstd.dll next to x360tid.exe
```

---

## Usage

```bash
# Scan a single file
x360tid game.xex

# Verbose output with all metadata
x360tid -V game.iso

# Scan a directory recursively
x360tid -r /path/to/games/

# JSON output (for scripting)
x360tid -j directory_of_games/

# CSV output
x360tid -c *.xex *.iso

# Deep scan (enumerate files inside containers)
x360tid -d game.pkg

# Custom thread count
x360tid -t 4 /games/

# Combine options
x360tid -rVj /games/ > output.json
```

### Output Example

```
Title ID    Format   Class          Name                            Version           File
----------  -------  -------------  ------------------------------  ----------------  --------------------
00000000    GDFX     Retail                                           1.0.0.0           
00000000    GDFX     Retail                                           1.0.0.0           
```

### All Options

| Option | Long | Description |
|--------|------|-------------|
| `-h` | `--help` | Show help message |
| `-v` | `--version` | Show version |
| `-V` | `--verbose` | Verbose output with all metadata |
| `-r` | `--recursive` | Recursively scan directories |
| `-d` | `--deep` | Deep scan: enumerate container files |
| `-j` | `--json` | Output in JSON format |
| `-c` | `--csv` | Output in CSV format |
| `-t N` | `--threads N` | Thread count (default: hardware concurrency) |

---

## Title ID Classification

| Prefix | Type | Example |
|--------|------|---------|
| `0xFFFE07D1` | Dashboard | System shell |
| `0x5841xxxx` | XBLA | Xbox Live Arcade |
| `0x5848xxxx` | App | Xbox App |
| `0x584Axxxx` | App | Xbox App |
| `0xFFFExxxx` | System | System titles |
| `< 0x7D0` (non-system) | Xbox Original | Backward compatible |
| Other | Retail | Retail games |

---

## Build Targets

| Target | Command | Description |
|--------|---------|-------------|
| `make` | `g++ ... -ldl` | Linux, dynamic zstd (needs `libzstd.so`) |
| `make linux-static` | `g++ ... -DX360TID_STATIC_ZSTD` | Linux, static zstd (standalone) |
| `make win` | `x86_64-w64-mingw32-g++ ... -static` | Windows, dynamic zstd (needs `zstd.dll`) |
| `make win-static` | `x86_64-w64-mingw32-g++ ... -DX360TID_STATIC_ZSTD -static` | Windows, static zstd (standalone) |
| `make clean` | | Remove all binaries |
| `make install` | | Install to `/usr/local/bin/` |

---

## Project Structure

```
x360tid/
├── LICENSE                     BSD 3-Clause license + third-party notices
├── Makefile                    Build system (make/make win-static/etc.)
├── README.md                   This file
├── src/
│   ├── main.cpp                CLI entry point, directory scanner, signal handling
│   ├── common.h                Constants, data types, endian I/O, string utils
│   ├── mapped_file.h           Cross-platform memory-mapped file I/O
│   ├── thread_pool.h           Thread pool for parallel processing
│   ├── zstd_library.h          ZSTD interface (static or dynamic loading)
│   ├── format_detect.h         Format auto-detection and parse dispatcher
│   ├── output.h                Output formatters (table/JSON/CSV)
│   └── parsers/
│       ├── xex_parser.h        XEX1/XEX2 executable parser
│       ├── gdfx_parser.h       GDFX/ISO disc image parser
│       ├── xcontent_parser.h   CON/LIVE/PIRS package parser
│       ├── svod_parser.h       SVOD/GOD block mapping parser
│       ├── zar_parser.h        ZArchive compressed archive parser
│       ├── xsf_parser.h        Xbox Ship File parser
│       └── xdbf_parser.h       XDBF/GPD profile data parser
└── deps/
    └── zstd/                   Zstandard v1.5.7 (decompression only)
        ├── LICENSE             BSD 3-Clause (Meta Platforms, Inc.)
        ├── zstd.h              Public API header
        ├── zstd_errors.h
        ├── common/             Common utilities
        └── decompress/         Decompression implementation
```

---

## Architecture

| Component | Implementation |
|-----------|---------------|
| **File I/O** | Memory-mapped files (`mmap` / `MapViewOfFile`) with RAII |
| **Threading** | `std::thread` pool with `std::future` results |
| **ZSTD** | Static compile-in or dynamic `dlopen`/`LoadLibrary` loading |
| **Endianness** | Big-endian and little-endian read functions with bounds checking |
| **Text** | UTF-16BE → UTF-8, Windows-1252 → UTF-8 conversion |
| **Name extraction** | XEX2 Resource Info → SPA/XDBF → UTF-16BE scan fallback |
| **Safety** | Bounds checking on every read, overflow-safe arithmetic, RAII everywhere |

---

## Runtime Dependencies

| Build Type | Linux | Windows |
|------------|-------|---------|
| Dynamic (`make` / `make win`) | `libzstd.so` (optional, for `.zar` files) | `zstd.dll` (optional, for `.zar` files) |
| Static (`make linux-static` / `make win-static`) | None | None |

---

### Acknowledgments

The following projects were analyzed (not copied) to understand Xbox 360 file format structures:

- **[Xenia](https://github.com/xenia-project/xenia)** — Xbox 360 emulator (BSD 3-Clause)
  Copyright (c) 2015 Ben Vanik and contributors

- **[Xenia Canary](https://github.com/xenia-canary/xenia-canary)** — Xenia experimental fork (BSD 3-Clause)
  Copyright (c) 2015 Ben Vanik and contributors

- **[ZArchive](https://github.com/Exzap/ZArchive)** — Compressed archive format (MIT-0)
  Copyright (c) 2022 Exzap

### Bundled Libraries

- **[Zstandard](https://github.com/facebook/zstd)** v1.5.7 — Fast compression library (BSD 3-Clause)
  Copyright (c) Meta Platforms, Inc. and affiliates.

- **xxHash** (bundled with zstd) — Fast hash algorithm (BSD 2-Clause)
  Copyright (c) Yann Collet - Meta Platforms, Inc.

---

