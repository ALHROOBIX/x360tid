// =============================================================================
// x360tid v1.0.0 — Xbox 360 Title ID Extraction Tool
// =============================================================================
//
// Multi-file C++17 implementation. No external compile-time dependencies.
// Build (Linux):   g++ -std=c++17 -O3 -pthread -o x360tid src/main.cpp -ldl
// Build (Windows): clang++ -std=c++17 -O3 -target x86_64-w64-windows-gnu -o x360tid.exe src/main.cpp
//
// Supported formats:
//   XEX1/XEX2, CON/LIVE/PIRS, GDFX/ISO, STFS, SVOD,
//   ZArchive (.zar), XSF, XDBF/GPD
//
// Copyright (c) 2026 ALHROOBIX
// =============================================================================

#include "format_detect.h"
#include "output.h"
#include "thread_pool.h"

// =============================================================================
// Windows Compatibility Helpers
// =============================================================================

#ifdef _WIN32
  // Use Win32 API for file type detection — avoids MinGW struct stat / stat() name collision
  static bool pathIsDirectory(const char* path) noexcept {
      DWORD attrs = GetFileAttributesA(path);
      return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
  }
  static bool pathIsFile(const char* path) noexcept {
      DWORD attrs = GetFileAttributesA(path);
      return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
  }
#else
  static bool pathIsDirectory(const char* path) noexcept {
      struct stat st;
      return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
  }
  static bool pathIsFile(const char* path) noexcept {
      struct stat st;
      return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
  }
#endif

// =============================================================================
// Directory Scanner
// =============================================================================

static void scanDirectory(const std::string& dirPath, std::vector<std::string>& outFiles, bool recursive, int depth = 0) {
    if (depth > 32) return;

#ifdef _WIN32
    // Windows: use FindFirstFile/FindNextFile
    std::string searchPath = dirPath;
    if (!searchPath.empty() && searchPath.back() != '/' && searchPath.back() != '\\') {
        searchPath += '/';
    }
    std::string searchPattern = searchPath + "*";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (findData.cFileName[0] == '.' && (findData.cFileName[1] == '\0' ||
            (findData.cFileName[1] == '.' && findData.cFileName[2] == '\0'))) {
            continue;
        }

        std::string fullPath = searchPath + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) {
                scanDirectory(fullPath, outFiles, recursive, depth + 1);
            }
        } else {
            outFiles.push_back(std::move(fullPath));
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
#else
    // POSIX: use opendir/readdir
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        std::string fullPath = dirPath;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
            fullPath += '/';
        }
        fullPath += entry->d_name;

        if (pathIsDirectory(fullPath.c_str()) && recursive) {
            scanDirectory(fullPath, outFiles, recursive, depth + 1);
        } else if (pathIsFile(fullPath.c_str())) {
            outFiles.push_back(std::move(fullPath));
        }
    }
    closedir(dir);
#endif
}

// =============================================================================
// Signal Handling
// =============================================================================

static std::atomic<bool> g_interrupted{false};

#ifndef _WIN32
static void signalHandler(int) {
    g_interrupted = true;
}

static void setupSignalHandlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}
#else
static BOOL WINAPI consoleHandler(DWORD) {
    g_interrupted = true;
    return TRUE;
}

static void setupSignalHandlers() {
    SetConsoleCtrlHandler(consoleHandler, TRUE);
}
#endif

// =============================================================================
// Scan Options
// =============================================================================

struct ScanOptions {
    bool recursive = false;
    bool verbose = false;
    bool deep = false;
    bool json_output = false;
    bool csv_output = false;
    std::vector<std::string> paths;
};

// =============================================================================
// Version & Help
// =============================================================================

static void printVersion() {
    printf("x360tid v%d.%d.%d\n", X360TID_VERSION_MAJOR, X360TID_VERSION_MINOR, X360TID_VERSION_PATCH);
    printf("Xbox 360 Title ID Extraction Tool\n");
    printf("Supports: XEX1/XEX2, CON/LIVE/PIRS, GDFX/ISO, STFS, SVOD, ZArchive, XSF, XDBF/GPD\n");
}

static void printHelp(const char* progName) {
    printVersion();
    printf("\nUsage: %s [options] <file|directory> [file|directory] ...\n\n", progName);
    printf("Options:\n");
    printf("  -h, --help          Show this help message\n");
    printf("  -v, --version       Show version information\n");
    printf("  -V, --verbose       Show verbose output with all metadata\n");
    printf("  -r, --recursive     Recursively scan directories\n");
    printf("  -d, --deep          Deep scan: enumerate files in containers\n");
    printf("  -j, --json          Output in JSON format\n");
    printf("  -c, --csv           Output in CSV format\n");
    printf("  -t, --threads <N>   Number of threads (default: hardware concurrency)\n");
    printf("\nSupported Formats:\n");
    printf("  XEX1/XEX2  - Xbox 360 Executable\n");
    printf("  CON/LIVE/PIRS - Xbox 360 Content Packages\n");
    printf("  GDFX/ISO   - Game Disc Format / ISO images\n");
    printf("  STFS       - Secure Transaction File System\n");
    printf("  SVOD       - System Volume Object Device\n");
    printf("  ZArchive   - Compressed Archive (requires libzstd)\n");
    printf("  XSF        - Xbox Ship File\n");
    printf("  XDBF/GPD   - Game Profile Data\n");
    printf("\nTitle ID Classification:\n");
    printf("  Dashboard      - 0xFFFE07D1\n");
    printf("  XBLA           - 'XA' prefix (0x5841xxxx)\n");
    printf("  App            - 'XH' or 'XJ' prefix\n");
    printf("  System         - 0xFFFExxxx\n");
    printf("  Xbox Original  - ID < 0x7D0 (non-system)\n");
    printf("  Retail         - All other titles\n");
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    setupSignalHandlers();

    ScanOptions opts;
    int threadCount = 0;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }
        else if (arg == "-V" || arg == "--verbose") {
            opts.verbose = true;
        }
        else if (arg == "-r" || arg == "--recursive") {
            opts.recursive = true;
        }
        else if (arg == "-d" || arg == "--deep") {
            opts.deep = true;
        }
        else if (arg == "-j" || arg == "--json") {
            opts.json_output = true;
        }
        else if (arg == "-c" || arg == "--csv") {
            opts.csv_output = true;
        }
        else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < args.size()) {
                threadCount = std::atoi(args[++i].c_str());
                if (threadCount < 1) threadCount = 1;
                if (threadCount > 64) threadCount = 64;
            }
        }
        else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            printHelp(argv[0]);
            return 1;
        }
        else {
            opts.paths.push_back(arg);
        }
    }

    if (opts.paths.empty()) {
        printHelp(argv[0]);
        return 1;
    }

    // Collect all files
    std::vector<std::string> files;
    for (const auto& path : opts.paths) {
        if (pathIsDirectory(path.c_str())) {
            scanDirectory(path, files, opts.recursive);
        } else if (pathIsFile(path.c_str())) {
            files.push_back(path);
        } else {
            fprintf(stderr, "Warning: Cannot access '%s': %s\n", path.c_str(), std::strerror(errno));
        }
    }

    if (files.empty()) {
        fprintf(stderr, "No files found to process.\n");
        return 1;
    }

    // Initialize ZSTD library
    ZstdLibrary zstd;

    // Parse files
    std::vector<TitleInfo> results;
    results.reserve(files.size());

    if (files.size() > 1 && threadCount != 1) {
        ThreadPool pool(threadCount > 0 ? threadCount : 0);
        std::vector<std::future<TitleInfo>> futures;
        futures.reserve(files.size());

        for (const auto& file : files) {
            if (g_interrupted.load(std::memory_order_relaxed)) break;
            futures.push_back(pool.enqueue([&file, &zstd, &opts]() -> TitleInfo {
                return parseFile(file, zstd, opts.verbose, opts.deep);
            }));
        }

        for (auto& f : futures) {
            if (g_interrupted.load(std::memory_order_relaxed)) break;
            results.push_back(f.get());
        }
    } else {
        for (const auto& file : files) {
            if (g_interrupted.load(std::memory_order_relaxed)) break;
            results.push_back(parseFile(file, zstd, opts.verbose, opts.deep));
        }
    }

    // Output results
    if (opts.json_output) {
        OutputFormatter::printJson(results);
    } else if (opts.csv_output) {
        OutputFormatter::printCsv(results);
    } else {
        OutputFormatter::printHuman(results, opts.verbose);
    }

    return 0;
}
