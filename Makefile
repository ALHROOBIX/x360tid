# x360tid v1.0.0 — Xbox 360 Title ID Extraction Tool
#
# Build targets:
#   make            Linux build (dynamic zstd, requires libzstd at runtime)
#   make linux-static  Linux build (zstd compiled in, no external lib needed)
#   make win        Windows cross-compile (dynamic zstd, requires zstd.dll)
#   make win-static Windows cross-compile (zstd compiled in, standalone .exe)
#   make clean
#
# Windows cross-compilation requires MinGW-w64 or llvm-mingw:
#   Fedora:    sudo dnf install mingw64-gcc-c++
#   Ubuntu:    sudo apt install g++-mingw-w64-x86-64-posix
#   Manual:    make win MINGW_DIR=/path/to/llvm-mingw/bin

CXX ?= g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra

SRCDIR = src
SRC = $(SRCDIR)/main.cpp

HEADERS = $(SRCDIR)/common.h $(SRCDIR)/mapped_file.h $(SRCDIR)/thread_pool.h \
          $(SRCDIR)/zstd_library.h $(SRCDIR)/format_detect.h $(SRCDIR)/output.h \
          $(SRCDIR)/parsers/xex_parser.h $(SRCDIR)/parsers/gdfx_parser.h \
          $(SRCDIR)/parsers/xcontent_parser.h $(SRCDIR)/parsers/svod_parser.h \
          $(SRCDIR)/parsers/zar_parser.h $(SRCDIR)/parsers/xsf_parser.h \
          $(SRCDIR)/parsers/xdbf_parser.h

# zstd static source files (decompression only)
ZSTD_DIR = deps/zstd
ZSTD_SRC = $(ZSTD_DIR)/common/debug.c \
           $(ZSTD_DIR)/common/entropy_common.c \
           $(ZSTD_DIR)/common/error_private.c \
           $(ZSTD_DIR)/common/fse_decompress.c \
           $(ZSTD_DIR)/common/xxhash.c \
           $(ZSTD_DIR)/common/zstd_common.c \
           $(ZSTD_DIR)/decompress/huf_decompress.c \
           $(ZSTD_DIR)/decompress/zstd_ddict.c \
           $(ZSTD_DIR)/decompress/zstd_decompress.c \
           $(ZSTD_DIR)/decompress/zstd_decompress_block.c

# zstd compiler flags
# ZSTD_DISABLE_ASM: use pure C decompression (portable, no .S assembly needed)
# Note: ZSTD_STATIC_LINKING_ONLY is defined inside zstd_internal.h, not here
ZSTD_CFLAGS = -DX360TID_STATIC_ZSTD -DZSTD_DISABLE_ASM \
              -I$(ZSTD_DIR) -I$(ZSTD_DIR)/common -I$(ZSTD_DIR)/decompress

MINGW_DIR ?=

.PHONY: all clean install win win-static linux-static

# Linux build (default — dynamic zstd)
all: x360tid

x360tid: $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -pthread -o $@ $(SRC) -ldl -lpthread

# Linux build with static zstd (no external library needed)
linux-static: x360tid-static

x360tid-static: $(SRC) $(HEADERS) $(ZSTD_SRC)
	$(CXX) $(CXXFLAGS) $(ZSTD_CFLAGS) -pthread -o $@ $(SRC) $(ZSTD_SRC) -lpthread

# Windows cross-compilation build (dynamic zstd — needs zstd.dll)
win: x360tid.exe

x360tid.exe: $(SRC) $(HEADERS) resource.rc
	@if [ -n "$(MINGW_DIR)" ] && [ -x "$(MINGW_DIR)/x86_64-w64-mingw32-clang++" ]; then \
		WINCXX="$(MINGW_DIR)/x86_64-w64-mingw32-clang++"; \
	elif command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then \
		WINCXX="x86_64-w64-mingw32-clang++"; \
	elif command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then \
		WINCXX="x86_64-w64-mingw32-g++"; \
	else \
		echo "ERROR: Windows cross-compiler not found!"; \
		echo ""; \
		echo "Install one of:"; \
		echo "  Fedora:    sudo dnf install mingw64-gcc-c++"; \
		echo "  Ubuntu:    sudo apt install g++-mingw-w64-x86-64-posix"; \
		echo "  Manual:    make win MINGW_DIR=/path/to/llvm-mingw/bin"; \
		echo ""; \
		echo "Or download llvm-mingw from:"; \
		echo "  https://github.com/mstorsjo/llvm-mingw/releases"; \
		exit 1; \
	fi; \
	if [ -n "$(MINGW_DIR)" ] && [ -x "$(MINGW_DIR)/x86_64-w64-mingw32-windres" ]; then \
		WINRES="$(MINGW_DIR)/x86_64-w64-mingw32-windres"; \
	elif command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then \
		WINRES="x86_64-w64-mingw32-windres"; \
	else \
		WINRES="windres"; \
	fi; \
	echo "Compiling resources with: $$WINRES"; \
	$$WINRES resource.rc -o resource.o; \
	echo "Building with: $$WINCXX"; \
	$$WINCXX $(CXXFLAGS) -static -o $@ $(SRC) resource.o; \
	rm -f resource.o

# Windows cross-compilation with static zstd (standalone .exe, no DLL needed)
win-static: x360tid-static.exe

x360tid-static.exe: $(SRC) $(HEADERS) $(ZSTD_SRC) resource.rc
	@if [ -n "$(MINGW_DIR)" ] && [ -x "$(MINGW_DIR)/x86_64-w64-mingw32-clang++" ]; then \
		WINCXX="$(MINGW_DIR)/x86_64-w64-mingw32-clang++"; \
	elif command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then \
		WINCXX="x86_64-w64-mingw32-clang++"; \
	elif command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then \
		WINCXX="x86_64-w64-mingw32-g++"; \
	else \
		echo "ERROR: Windows cross-compiler not found!"; \
		echo ""; \
		echo "Install one of:"; \
		echo "  Fedora:    sudo dnf install mingw64-gcc-c++"; \
		echo "  Ubuntu:    sudo apt install g++-mingw-w64-x86-64-posix"; \
		echo "  Manual:    make win-static MINGW_DIR=/path/to/llvm-mingw/bin"; \
		exit 1; \
	fi; \
	if [ -n "$(MINGW_DIR)" ] && [ -x "$(MINGW_DIR)/x86_64-w64-mingw32-windres" ]; then \
		WINRES="$(MINGW_DIR)/x86_64-w64-mingw32-windres"; \
	elif command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then \
		WINRES="x86_64-w64-mingw32-windres"; \
	else \
		WINRES="windres"; \
	fi; \
	echo "Compiling resources with: $$WINRES"; \
	$$WINRES resource.rc -o resource.o; \
	echo "Building with: $$WINCXX (static zstd)"; \
	$$WINCXX $(CXXFLAGS) $(ZSTD_CFLAGS) -static -o $@ $(SRC) $(ZSTD_SRC) resource.o; \
	rm -f resource.o

# Clean & Install
clean:
	rm -f x360tid x360tid-static x360tid.exe x360tid-static.exe resource.o

install: x360tid
	install -m 755 x360tid /usr/local/bin/
