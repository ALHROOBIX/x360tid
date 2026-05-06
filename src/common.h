// =============================================================================
// common.h — x360tid v1.0.0 Shared Types, Constants & Utilities
// =============================================================================
//
// Xbox 360 Title ID Extraction Tool
// Based on the proven, working x360tid v1.0.0 single-file implementation.
// Build: g++ -std=c++17 -O3 -pthread -o x360tid src/main.cpp -ldl
//
// Copyright (c) 2026 ALHROOBIX
// =============================================================================

#pragma once

#define X360TID_VERSION_MAJOR 1
#define X360TID_VERSION_MINOR 0
#define X360TID_VERSION_PATCH 0

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <cassert>
#include <string>
#include <vector>
#include <array>
#include <queue>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <future>
#include <chrono>
#include <type_traits>
#include <optional>
#include <limits>
#include <sstream>
#include <iostream>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
  #include <fcntl.h>
#else
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <dlfcn.h>
  #include <signal.h>
  #include <errno.h>
#endif

#include <dirent.h>

// ============================================================================
// Constants
// ============================================================================

namespace constants {

constexpr uint32_t kXex1Magic = 0x58455831;
constexpr uint32_t kXex2Magic = 0x58455832;
constexpr uint32_t kConMagic  = 0x434F4E20;
constexpr uint32_t kLiveMagic = 0x4C495645;
constexpr uint32_t kPirsMagic = 0x50495253;
constexpr const char kGdfxMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kGdfxMagicLen = 20;
constexpr uint32_t kXsfMagic = 0x58535F1A;
constexpr uint32_t kZarMagic = 0x169F52D6;
constexpr uint32_t kZarVersion = 0x61BF3A01;
constexpr uint32_t kXdbfMagic = 0x58444246;

constexpr size_t kSectorSize = 0x800;
constexpr size_t kStfsBlockSize = 0x1000;
constexpr size_t kSvodBlockSize = 0x800;
constexpr size_t kZarBlockSize = 0x10000;

constexpr uint64_t kXgdOffsets[] = {0x0, 0xFB20, 0x20600, 0x2080000, 0xFD90000};
constexpr size_t kXgdOffsetCount = 5;

constexpr size_t kXContentHeaderSize = 0x344;
constexpr size_t kXContentMetadataSize = 0x93D6;

constexpr uint32_t kStfsEndOfChain = 0xFFFFFF;
constexpr uint32_t kStfsBlocksPerHashLevel[] = {170, 28900, 4913000};
constexpr size_t kStfsDirEntrySize = 0x40;
constexpr size_t kStfsEntriesPerBlock = kStfsBlockSize / kStfsDirEntrySize;

constexpr size_t kSvodBlocksPerL0Hash = 0x198;
constexpr size_t kSvodHashesPerL1Hash = 0xA1C4;
constexpr size_t kSvodBlocksPerFile = 0x14388;
constexpr uint64_t kSvodMaxFileSize = 0xA290000;

constexpr size_t kZarFooterSize = 0x90;

constexpr size_t kMaxFileSize = 64ULL * 1024 * 1024 * 1024;
constexpr size_t kMaxDepth = 16;
constexpr size_t kMaxDirectoryEntries = 1000000;
constexpr size_t kMaxBlockChainLength = 10000000;

constexpr uint32_t kXex2ExecutionInfo         = 0x00040000;
constexpr uint32_t kXex2AlternateTitleIds     = 0x00040700;
constexpr uint32_t kXex2OriginalPeName        = 0x00018300;
constexpr uint32_t kXex2ResourceInfo          = 0x00000200;  // Resource Info (SPA/XDBF) - base key

constexpr uint32_t kContentTypeSavedGame     = 0x00000001;
constexpr uint32_t kContentTypeInstalledGame = 0x00004000;
constexpr uint32_t kContentTypeXboxTitle     = 0x00005000;
constexpr uint32_t kContentTypeXbox360Title  = 0x00007000;
constexpr uint32_t kContentTypeGameDemo      = 0x00080000;
constexpr uint32_t kContentTypeGameTitle     = 0x000A0000;
constexpr uint32_t kContentTypeArcadeTitle   = 0x000D0000;
constexpr uint32_t kContentTypeCommunityGame = 0x02000000;

constexpr uint32_t kDashboardTitleId = 0xFFFE07D1;

} // namespace constants

// ============================================================================
// Endian Read Functions
// ============================================================================

[[nodiscard]] inline bool readU8(const uint8_t* data, size_t size, size_t offset, uint8_t& out) noexcept {
    if (offset >= size) return false;
    out = data[offset];
    return true;
}

[[nodiscard]] inline bool readU16BE(const uint8_t* data, size_t size, size_t offset, uint16_t& out) noexcept {
    if (offset + 1 >= size) return false;
    out = (uint16_t(data[offset]) << 8) | uint16_t(data[offset + 1]);
    return true;
}

[[nodiscard]] inline bool readU32BE(const uint8_t* data, size_t size, size_t offset, uint32_t& out) noexcept {
    if (offset + 3 >= size) return false;
    out = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
          (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
    return true;
}

[[nodiscard]] inline bool readU64BE(const uint8_t* data, size_t size, size_t offset, uint64_t& out) noexcept {
    if (offset + 7 >= size) return false;
    out = (uint64_t(data[offset]) << 56) | (uint64_t(data[offset + 1]) << 48) |
          (uint64_t(data[offset + 2]) << 40) | (uint64_t(data[offset + 3]) << 32) |
          (uint64_t(data[offset + 4]) << 24) | (uint64_t(data[offset + 5]) << 16) |
          (uint64_t(data[offset + 6]) << 8)  | uint64_t(data[offset + 7]);
    return true;
}

[[nodiscard]] inline bool readU16LE(const uint8_t* data, size_t size, size_t offset, uint16_t& out) noexcept {
    if (offset + 1 >= size) return false;
    out = uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
    return true;
}

[[nodiscard]] inline bool readU32LE(const uint8_t* data, size_t size, size_t offset, uint32_t& out) noexcept {
    if (offset + 3 >= size) return false;
    out = uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
          (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
    return true;
}

[[nodiscard]] inline bool readU64LE(const uint8_t* data, size_t size, size_t offset, uint64_t& out) noexcept {
    if (offset + 7 >= size) return false;
    out = uint64_t(data[offset]) | (uint64_t(data[offset + 1]) << 8) |
          (uint64_t(data[offset + 2]) << 16) | (uint64_t(data[offset + 3]) << 24) |
          (uint64_t(data[offset + 4]) << 32) | (uint64_t(data[offset + 5]) << 40) |
          (uint64_t(data[offset + 6]) << 48) | (uint64_t(data[offset + 7]) << 56);
    return true;
}

[[nodiscard]] inline bool readU24LE(const uint8_t* data, size_t size, size_t offset, uint32_t& out) noexcept {
    if (offset + 2 >= size) return false;
    out = uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
          (uint32_t(data[offset + 2]) << 16);
    return true;
}

// ============================================================================
// String Utilities
// ============================================================================

[[nodiscard]] inline std::string utf16beToUtf8(const uint8_t* data, size_t charCount) noexcept {
    std::string result;
    result.reserve(charCount * 3);
    for (size_t i = 0; i < charCount; ++i) {
        size_t off = i * 2;
        uint16_t cp = (uint16_t(data[off]) << 8) | data[off + 1];
        if (cp == 0) break;
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < charCount) {
                uint16_t cp2 = (uint16_t(data[off + 2]) << 8) | data[off + 3];
                if (cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                    uint32_t codepoint = 0x10000 + (((uint32_t(cp) & 0x3FF) << 10) | (cp2 & 0x3FF));
                    result += static_cast<char>(0xF0 | (codepoint >> 18));
                    result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    ++i;
                    continue;
                }
            }
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

[[nodiscard]] inline std::string win1252ToUtf8(const uint8_t* data, size_t len) noexcept {
    static const uint32_t kWin1252Map[128] = {
        0x20AC,0xFFFF,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,
        0x0160,0x2039,0x0152,0xFFFF,0x017D,0xFFFF,0xFFFF,0x2018,0x2019,0x201C,
        0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0xFFFF,
        0x017E,0x0178,0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
        0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,0x00B0,0x00B1,
        0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,0x00B8,0x00B9,0x00BA,0x00BB,
        0x00BC,0x00BD,0x00BE,0x00BF,0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,
        0x00C6,0x00C7,0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF,
        0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D7,0x00D8,0x00D9,
        0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x00DF,0x00E0,0x00E1,0x00E2,0x00E3,
        0x00E4,0x00E5,0x00E6,0x00E7,0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,
        0x00EE,0x00EF,0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F7,
        0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF
    };
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = data[i];
        if (c == 0) break;
        if (c < 0x80) {
            result += static_cast<char>(c);
        } else {
            uint32_t cp = kWin1252Map[c - 0x80];
            if (cp == 0xFFFF) { result += '?'; continue; }
            if (cp < 0x80) {
                result += static_cast<char>(cp);
            } else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
    }
    return result;
}

[[nodiscard]] inline bool strEqCI(const char* a, const char* b, size_t len) noexcept {
    for (size_t i = 0; i < len; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
    return true;
}

[[nodiscard]] inline std::string classifyTitleId(uint32_t titleId) noexcept {
    if (titleId == constants::kDashboardTitleId) return "Dashboard";
    uint32_t prefix = (titleId >> 16) & 0xFFFF;
    if (prefix == 0x5841) return "XBLA";
    if (prefix == 0x5848) return "App";
    if (prefix == 0x584A) return "App";
    if ((titleId & 0xFFFF0000) == 0xFFFE0000) return "System";
    if (titleId < 0x7D0 && (titleId & 0xFFFF0000) != 0xFFFE0000) return "Xbox Original";
    return "Retail";
}

[[nodiscard]] inline std::string contentTypeToString(uint32_t ct) noexcept {
    if (ct == constants::kContentTypeSavedGame)     return "SavedGame";
    if (ct == constants::kContentTypeInstalledGame) return "InstalledGame";
    if (ct == constants::kContentTypeXboxTitle)     return "XboxTitle";
    if (ct == constants::kContentTypeXbox360Title)  return "Xbox360Title";
    if (ct == constants::kContentTypeGameDemo)      return "GameDemo";
    if (ct == constants::kContentTypeGameTitle)     return "GameTitle";
    if (ct == constants::kContentTypeArcadeTitle)   return "ArcadeTitle";
    if (ct == constants::kContentTypeCommunityGame) return "CommunityGame";
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", ct);
    return buf;
}

template<typename T>
[[nodiscard]] inline bool safeAdd(T a, T b, T& result) noexcept {
    if (b > 0 && a > std::numeric_limits<T>::max() - b) return false;
    if (b < 0 && a < std::numeric_limits<T>::min() - b) return false;
    result = a + b;
    return true;
}

template<typename T>
[[nodiscard]] inline bool safeMul(T a, T b, T& result) noexcept {
    if (a != 0 && b != 0) {
        if (a > std::numeric_limits<T>::max() / b) return false;
    }
    result = a * b;
    return true;
}

// ============================================================================
// Data Structures
// ============================================================================

struct ExecutionInfo {
    uint32_t media_id = 0;
    uint32_t version_value = 0;
    uint32_t base_version_value = 0;
    uint32_t title_id = 0;
    uint8_t platform = 0;
    uint8_t executable_table = 0;
    uint8_t disc_number = 0;
    uint8_t disc_count = 0;
    uint32_t savegame_id = 0;

    [[nodiscard]] uint16_t versionMajor() const noexcept { return (version_value >> 28) & 0xF; }
    [[nodiscard]] uint16_t versionMinor() const noexcept { return (version_value >> 24) & 0xF; }
    [[nodiscard]] uint16_t versionBuild() const noexcept { return (version_value >> 8) & 0xFFFF; }
    [[nodiscard]] uint16_t versionQfe() const noexcept { return version_value & 0xFF; }
    [[nodiscard]] std::string versionString() const noexcept {
        char buf[64];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", versionMajor(), versionMinor(), versionBuild(), versionQfe());
        return buf;
    }
};

struct TitleInfo {
    uint32_t title_id = 0;
    std::string title_name;
    std::string publisher;
    std::string format;
    std::string format_version;
    std::string classification;
    std::string content_type_str;
    uint32_t content_type = 0;
    uint32_t media_id = 0;
    uint32_t version_value = 0;
    uint32_t base_version = 0;
    std::string version_string;
    uint8_t disc_number = 0;
    uint8_t disc_count = 0;
    uint32_t savegame_id = 0;
    bool zstd_available = true;
    std::vector<uint32_t> alternate_title_ids;
    std::string file_path;
    int64_t parse_time_us = 0;
    std::string error;
};

// ============================================================================
// UTF-16BE Name Scanning Helpers (must come before parseXexFromMemory)
// ============================================================================

[[nodiscard]] inline bool isLikelyTitleName(const std::string& s) noexcept {
    if (s.size() < 3) return false;

    // Reject strings that are all the same character
    bool allSame = true;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] != s[0]) { allSame = false; break; }
    }
    if (allSame) return false;

    // Common non-name strings to reject
    static const char* kBlacklist[] = {
        "xbox", "xex", "microsoft", "copyright", "default",
        "kernel", "xam", "xui", "title", "module",
        "unknown", "xenia", "generic", "sample",
        "debug", "release", "test", "placeholder",
        nullptr
    };

    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (int i = 0; kBlacklist[i]; ++i) {
        if (lower == kBlacklist[i]) return false;
        std::string prefix(kBlacklist[i]);
        if (lower.size() > prefix.size() &&
            lower.compare(0, prefix.size(), prefix) == 0 &&
            !std::isalpha(static_cast<unsigned char>(lower[prefix.size()]))) {
            return false;
        }
    }

    // Reject strings that look like paths or file extensions
    if (s.find('\\') != std::string::npos) return false;
    if (s.find('/') != std::string::npos) return false;
    if (s.find(".xex") != std::string::npos) return false;
    if (s.find(".dll") != std::string::npos) return false;
    if (s.find(".exe") != std::string::npos) return false;
    if (s.size() > 120) return false;

    return true;
}

[[nodiscard]] inline bool isPrintableAscii(uint8_t b) noexcept {
    return b >= 0x20 && b <= 0x7E;
}

[[nodiscard]] inline std::string scanForUtf16BENameRegion(const uint8_t* region, size_t regionSize) noexcept {
    if (!region || regionSize < 2) return {};

    std::string bestName;
    size_t bestLen = 0;

    size_t i = 0;
    while (i + 1 < regionSize) {
        // UTF-16BE for ASCII: [0x00, char_byte]
        if (region[i] == 0x00 && isPrintableAscii(region[i + 1])) {
            size_t start = i;
            size_t charCount = 0;

            while (i + 1 < regionSize &&
                   region[i] == 0x00 && isPrintableAscii(region[i + 1])) {
                ++charCount;
                i += 2;
            }

            if (charCount >= 3) {
                std::string candidate;
                candidate.reserve(charCount);
                for (size_t j = start; j + 1 < regionSize && j < start + charCount * 2; j += 2) {
                    candidate.push_back(static_cast<char>(region[j + 1]));
                }

                if (isLikelyTitleName(candidate) && candidate.size() > bestLen) {
                    bestName = candidate;
                    bestLen = candidate.size();
                }
            }
        } else {
            ++i;
        }
    }

    return bestName;
}

[[nodiscard]] inline std::string scanForUtf16BEName(const uint8_t* data, size_t size) noexcept {
    // Scan up to 1 MB for UTF-16BE title strings
    size_t scanLen = std::min(size, static_cast<size_t>(1024 * 1024));
    return scanForUtf16BENameRegion(data, scanLen);
}

// ============================================================================
// Extract title from XDBF (Xbox Database Format) data
// ============================================================================

[[nodiscard]] inline std::string extractTitleFromXdbf(const uint8_t* data, size_t size, size_t xdbfOffset) noexcept {
    if (xdbfOffset + 0x18 > size) return {};

    uint32_t magic = 0;
    if (!readU32BE(data, size, xdbfOffset, magic) || magic != constants::kXdbfMagic) return {};

    uint32_t entryCount = 0;
    if (!readU32BE(data, size, xdbfOffset + 0x08, entryCount)) return {};
    if (entryCount > 1024) return {};

    size_t entryTableOffset = xdbfOffset + 0x18;
    std::string bestName;

    for (uint32_t i = 0; i < entryCount; ++i) {
        size_t eOff = entryTableOffset + i * 0x10;
        if (eOff + 0x10 > size) break;

        uint16_t ns = 0;
        uint32_t idHigh = 0, idLow = 0, eOffset = 0, eSize = 0;
        (void)readU16BE(data, size, eOff + 0x00, ns);
        (void)readU32BE(data, size, eOff + 0x02, idHigh);
        (void)readU32BE(data, size, eOff + 0x06, idLow);
        (void)readU32BE(data, size, eOff + 0x0A, eOffset);
        (void)readU32BE(data, size, eOff + 0x0E, eSize);

        // Namespace 0x03 = Title entries; 0x8000 = Achievement entries
        if (ns == 0x03 && eSize >= 4 && eOffset + eSize <= size) {
            size_t strOff = xdbfOffset + eOffset;
            if (strOff + eSize <= size) {
                size_t headerSkip = 0;
                if (eSize > 4) {
                    uint32_t headerWord = 0;
                    (void)readU32BE(data, size, strOff, headerWord);
                    if (headerWord <= 0x20) headerSkip = 4;
                }
                size_t charCount = (eSize - headerSkip) / 2;
                if (charCount >= 2 && charCount <= 256) {
                    std::string name = utf16beToUtf8(data + strOff + headerSkip, charCount);
                    if (!name.empty() && isLikelyTitleName(name)) {
                        if (name.size() > bestName.size()) {
                            bestName = std::move(name);
                        }
                    }
                }
            }
        }
        else if (ns == 0x01 && eSize >= 8 && eOffset + eSize <= size) {
            size_t strOff = xdbfOffset + eOffset;
            if (strOff + eSize <= size) {
                uint32_t strLen = 0;
                if (readU32BE(data, size, strOff + 4, strLen) && strLen > 0 && strLen <= 256) {
                    size_t charCount = strLen;
                    if (strOff + 8 + charCount * 2 <= size) {
                        std::string name = utf16beToUtf8(data + strOff + 8, charCount);
                        if (!name.empty() && isLikelyTitleName(name)) {
                            if (name.size() > bestName.size()) {
                                bestName = std::move(name);
                            }
                        }
                    }
                }
            }
        }
    }

    return bestName;
}

// ============================================================================
// Title Name Extraction from XEX2 Resource Info (SPA/XDBF)
// ============================================================================

[[nodiscard]] inline std::string extractNameFromResourceInfo(const uint8_t* data, size_t size, uint64_t dataOffset) noexcept {
    if (dataOffset + 4 > size) return {};

    uint32_t count = 0;
    if (!readU32BE(data, size, static_cast<size_t>(dataOffset), count)) return {};
    if (count == 0 || count > 1024) return {};

    for (int entrySize : {16, 12}) {
        size_t entryPos = static_cast<size_t>(dataOffset) + 4;
        for (uint32_t i = 0; i < count; ++i) {
            if (entryPos + static_cast<size_t>(entrySize) > size) break;

            uint32_t resType = 0, resName = 0, resAddr = 0, resSize = 0;
            (void)readU32BE(data, size, entryPos, resType);
            (void)readU32BE(data, size, entryPos + 4, resName);

            if (entrySize == 16) {
                (void)readU32BE(data, size, entryPos + 8, resAddr);
                (void)readU32BE(data, size, entryPos + 12, resSize);
            } else {
                (void)readU32BE(data, size, entryPos + 8, resAddr);
                resSize = 0;
            }

            if (resType == 0x00000002 || resType == 0x00000004) {
                size_t scanStart = resAddr & 0x7FFFFFFFu;
                if (scanStart < size && scanStart + 4 < size) {
                    uint32_t xdbfMagic = 0;
                    if (readU32BE(data, size, scanStart, xdbfMagic) && xdbfMagic == constants::kXdbfMagic) {
                        std::string name = extractTitleFromXdbf(data, size, scanStart);
                        if (!name.empty()) return name;
                    }
                    size_t scanLen = std::min(static_cast<size_t>(8192), size - scanStart);
                    std::string name = scanForUtf16BENameRegion(data + scanStart, scanLen);
                    if (!name.empty()) return name;
                }
                size_t sectorOff = static_cast<size_t>(resAddr) * constants::kSectorSize;
                if (sectorOff < size && sectorOff + 4 < size && sectorOff != scanStart) {
                    size_t scanLen = std::min(static_cast<size_t>(8192), size - sectorOff);
                    std::string name = scanForUtf16BENameRegion(data + sectorOff, scanLen);
                    if (!name.empty()) return name;
                }
            }
            entryPos += static_cast<size_t>(entrySize);
        }
    }
    return {};
}

// ============================================================================
// ExecutionInfo Parser + XEX from Memory (shared)
// ============================================================================

[[nodiscard]] inline ExecutionInfo parseExecutionInfo(const uint8_t* d, size_t sz, size_t offset) noexcept {
    ExecutionInfo ei;
    (void)readU32BE(d, sz, offset + 0x00, ei.media_id);
    (void)readU32BE(d, sz, offset + 0x04, ei.version_value);
    (void)readU32BE(d, sz, offset + 0x08, ei.base_version_value);
    (void)readU32BE(d, sz, offset + 0x0C, ei.title_id);
    (void)readU8(d, sz, offset + 0x10, ei.platform);
    (void)readU8(d, sz, offset + 0x11, ei.executable_table);
    (void)readU8(d, sz, offset + 0x12, ei.disc_number);
    (void)readU8(d, sz, offset + 0x13, ei.disc_count);
    (void)readU32BE(d, sz, offset + 0x14, ei.savegame_id);
    return ei;
}

[[nodiscard]] inline TitleInfo parseXexFromMemory(const uint8_t* data, size_t size, bool extractName = false) noexcept {
    TitleInfo info;
    info.format = "XEX";

    uint32_t magic;
    if (!readU32BE(data, size, 0, magic)) return info;
    info.format_version = (magic == constants::kXex1Magic) ? "XEX1" : "XEX2";

    uint32_t header_count;
    if (!readU32BE(data, size, 0x14, header_count)) return info;
    if (header_count > 65536) return info;

    for (uint32_t i = 0; i < header_count; ++i) {
        size_t off = 0x18 + i * 8;
        uint32_t key, value;
        if (!readU32BE(data, size, off, key)) break;
        if (!readU32BE(data, size, off + 4, value)) break;

        uint32_t keyBase = key & 0xFFFFFF00;
        uint8_t keyLow = key & 0xFF;

        uint64_t dataOffset;
        if (keyLow == 0x00) {
            dataOffset = off + 4;
        } else if (keyLow == 0x01) {
            if (!safeAdd<uint64_t>(off + 4, value, dataOffset)) continue;
        } else {
            dataOffset = value;
        }

        if (keyBase == constants::kXex2ExecutionInfo) {
            if (dataOffset + 0x18 > size) continue;
            ExecutionInfo ei = parseExecutionInfo(data, size, dataOffset);
            info.title_id = ei.title_id;
            info.media_id = ei.media_id;
            info.version_value = ei.version_value;
            info.base_version = ei.base_version_value;
            info.version_string = ei.versionString();
            info.disc_number = ei.disc_number;
            info.disc_count = ei.disc_count;
            info.savegame_id = ei.savegame_id;
        }
        else if (keyBase == constants::kXex2AlternateTitleIds) {
            if (dataOffset + 4 > size) continue;
            uint32_t altSize;
            if (!readU32BE(data, size, dataOffset, altSize)) continue;
            if (altSize < 4 || altSize > 4096) continue;
            uint32_t count = (altSize - 4) / 4;
            if (dataOffset + 4 + count * 4 > size) continue;
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t altId;
                if (readU32BE(data, size, dataOffset + 4 + j * 4, altId)) {
                    info.alternate_title_ids.push_back(altId);
                }
            }
        }
        else if (extractName && keyBase == constants::kXex2ResourceInfo) {
            std::string name = extractNameFromResourceInfo(data, size, dataOffset);
            if (!name.empty()) {
                info.title_name = std::move(name);
            }
        }
    }

    // Fallback: scan for UTF-16BE title string if name not found via Resource Info
    if (extractName && info.title_name.empty() && info.title_id != 0) {
        std::string name = scanForUtf16BEName(data, size);
        if (!name.empty()) {
            info.title_name = std::move(name);
        }
    }

    return info;
}
