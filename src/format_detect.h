// =============================================================================
// format_detect.h — Format Detection & Parser Dispatch
// =============================================================================
#pragma once
#include "common.h"
#include "mapped_file.h"
#include "zstd_library.h"
#include "parsers/xex_parser.h"
#include "parsers/gdfx_parser.h"
#include "parsers/xcontent_parser.h"
#include "parsers/zar_parser.h"
#include "parsers/xsf_parser.h"
#include "parsers/xdbf_parser.h"

enum class FileFormat {
    Unknown,
    XEX1,
    XEX2,
    CON,
    LIVE,
    PIRS,
    GDFX,
    ZArchive,
    XSF,
    XDBF,
    SVODData,
};

[[nodiscard]] static FileFormat detectFormat(const MappedFile& mf, const std::string& extension) noexcept {
    const uint8_t* d = mf.data();
    const size_t sz = mf.size();

    // Priority 1: Check magic at offset 0
    if (sz >= 4) {
        uint32_t magic;
        if (readU32BE(d, sz, 0, magic)) {
            if (magic == constants::kXex1Magic) return FileFormat::XEX1;
            if (magic == constants::kXex2Magic) return FileFormat::XEX2;
            if (magic == constants::kConMagic)  return FileFormat::CON;
            if (magic == constants::kLiveMagic) return FileFormat::LIVE;
            if (magic == constants::kPirsMagic) return FileFormat::PIRS;
            if (magic == constants::kXdbfMagic) return FileFormat::XDBF;
        }
    }

    // Priority 1b: Check XSF magic at various offsets
    if (XsfParser::detect(mf)) return FileFormat::XSF;

    // Priority 2: Check footer magic (ZAR)
    if (ZArchiveParser::detect(mf)) return FileFormat::ZArchive;

    // Priority 3: Check GDFX magic at sector 32 from XGD offsets
    if (GdfxParser::detect(mf)) return FileFormat::GDFX;

    // Priority 4: Extension-based fallback
    if (!extension.empty()) {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".xex") return FileFormat::XEX2;
        if (ext == ".iso") return FileFormat::GDFX;
        if (ext == ".zar") return FileFormat::ZArchive;
        if (ext == ".con") return FileFormat::CON;
        if (ext == ".live") return FileFormat::LIVE;
        if (ext == ".pirs") return FileFormat::PIRS;
        if (ext == ".gpd") return FileFormat::XDBF;
        if (ext == ".pkg") return FileFormat::CON;
        if (ext == ".xsf") return FileFormat::XSF;
        if (ext == ".data") return FileFormat::SVODData;
    }

    return FileFormat::Unknown;
}

[[nodiscard]] static std::string getFileExtension(const std::string& path) noexcept {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    return path.substr(dot);
}

[[nodiscard]] static std::string getFileName(const std::string& path) noexcept {
    size_t sep = path.find_last_of("/\\");
    if (sep == std::string::npos) return path;
    return path.substr(sep + 1);
}

// ============================================================================
// Main Parser Entry Point
// ============================================================================

[[nodiscard]] inline TitleInfo parseFile(const std::string& path, const ZstdLibrary& zstd, bool verbose, bool deep) noexcept {
    auto startTime = std::chrono::high_resolution_clock::now();

    TitleInfo info;
    info.file_path = path;

    MappedFile mf;
    if (!mf.open(path.c_str())) {
        info.error = "Cannot open file: " + path;
        return info;
    }

    std::string ext = getFileExtension(path);
    FileFormat fmt = detectFormat(mf, ext);

    switch (fmt) {
        case FileFormat::XEX1:
        case FileFormat::XEX2:
            info = XexParser::parse(mf, verbose);
            break;

        case FileFormat::CON:
        case FileFormat::LIVE:
        case FileFormat::PIRS:
            info = XContentParser::parse(mf, verbose, deep);
            break;

        case FileFormat::GDFX:
            info = GdfxParser::parse(mf, verbose);
            break;

        case FileFormat::ZArchive:
            info = ZArchiveParser::parse(mf, zstd, verbose);
            break;

        case FileFormat::XSF:
            info = XsfParser::parse(mf, verbose);
            break;

        case FileFormat::XDBF:
            info = XdbfParser::parse(mf);
            break;

        case FileFormat::SVODData: {
            if (GdfxParser::detect(mf)) {
                info = GdfxParser::parse(mf, verbose);
            } else {
                info.format = "SVOD";
                info.error = "SVOD data file - need header container for full parsing";
            }
            break;
        }

        default:
            info.format = "Unknown";
            info.error = "Unrecognized file format";
            break;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    info.parse_time_us = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

    if (info.title_id != 0) {
        info.classification = classifyTitleId(info.title_id);
    }

    return info;
}
