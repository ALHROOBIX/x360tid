// =============================================================================
// xex_parser.h — XEX1/XEX2 File Parser
// =============================================================================
#pragma once
#include "../common.h"

class XexParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        if (mf.size() < 4) return false;
        uint32_t magic;
        if (!readU32BE(mf.data(), mf.size(), 0, magic)) return false;
        return magic == constants::kXex1Magic || magic == constants::kXex2Magic;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf, bool verbose) noexcept {
        TitleInfo info;
        info.format = "XEX";
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        // Use the shared parseXexFromMemory with name extraction for standalone XEX files
        TitleInfo xexInfo = parseXexFromMemory(d, sz, true);
        info = std::move(xexInfo);
        info.format = "XEX";

        // Re-read format_version from the magic
        uint32_t magic;
        if (readU32BE(d, sz, 0, magic)) {
            info.format_version = (magic == constants::kXex1Magic) ? "XEX1" : "XEX2";
        }

        // Also read module_flags and header_size for verbose output
        if (verbose) {
            uint32_t module_flags = 0, header_size = 0, header_count = 0;
            (void)readU32BE(d, sz, 0x04, module_flags);
            (void)readU32BE(d, sz, 0x08, header_size);
            (void)readU32BE(d, sz, 0x14, header_count);

            for (uint32_t i = 0; i < header_count && i < 65536; ++i) {
                size_t off = 0x18 + i * 8;
                uint32_t key, value;
                if (!readU32BE(d, sz, off, key)) break;
                if (!readU32BE(d, sz, off + 4, value)) break;

                if ((key & 0xFFFFFF00) == constants::kXex2OriginalPeName && key >= 0x02) {
                    uint32_t keyLow = key & 0xFF;
                    uint64_t dataOffset;
                    if (keyLow == 0x00) {
                        dataOffset = off + 4;
                    } else if (keyLow == 0x01) {
                        if (!safeAdd<uint64_t>(off + 4, value, dataOffset)) continue;
                    } else {
                        dataOffset = value;
                    }
                    uint32_t nameLen;
                    if (readU32BE(d, sz, dataOffset, nameLen) && nameLen > 0 && nameLen < 256) {
                        if (mf.checkBounds(static_cast<size_t>(dataOffset) + 4, nameLen)) {
                            if (info.title_name.empty()) {
                                info.title_name = std::string(reinterpret_cast<const char*>(d + dataOffset + 4), nameLen);
                            }
                        }
                    }
                }
            }
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }
};
