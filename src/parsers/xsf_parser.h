// =============================================================================
// xsf_parser.h — XSF (Xbox Ship File) Parser
// =============================================================================
#pragma once
#include "../common.h"
#include "gdfx_parser.h"

class XsfParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        if (sz >= 4) {
            uint32_t magic;
            if (readU32BE(d, sz, 0, magic) && magic == constants::kXsfMagic) return true;
        }

        for (size_t i = 1; i < constants::kXgdOffsetCount; ++i) {
            uint64_t offset = constants::kXgdOffsets[i];
            if (offset + 4 > sz) continue;
            uint32_t magic;
            if (readU32BE(d, sz, offset, magic) && magic == constants::kXsfMagic) return true;
        }
        return false;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf, bool verbose) noexcept {
        TitleInfo info;
        info.format = "XSF";
        (void)verbose;
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        uint64_t xsfOffset = 0;
        bool found = false;

        if (sz >= 4) {
            uint32_t magic;
            if (readU32BE(d, sz, 0, magic) && magic == constants::kXsfMagic) {
                xsfOffset = 0;
                found = true;
            }
        }
        if (!found) {
            for (size_t i = 1; i < constants::kXgdOffsetCount; ++i) {
                uint64_t offset = constants::kXgdOffsets[i];
                if (offset + 4 > sz) continue;
                uint32_t magic;
                if (readU32BE(d, sz, offset, magic) && magic == constants::kXsfMagic) {
                    xsfOffset = offset;
                    found = true;
                    break;
                }
            }
        }

        // Search for GDFX magic within the XSF
        uint64_t searchEnd = (static_cast<uint64_t>(sz) < xsfOffset + 0x20000ULL) ? static_cast<uint64_t>(sz) : (xsfOffset + 0x20000ULL);
        for (uint64_t searchOff = xsfOffset; searchOff < searchEnd; searchOff += 0x800) {
            if (searchOff + constants::kGdfxMagicLen > sz) break;
            if (memcmp(d + searchOff, constants::kGdfxMagic, constants::kGdfxMagicLen) == 0) {
                uint64_t vdOff;
                if (safeAdd<uint64_t>(searchOff, 32ULL * constants::kSectorSize, vdOff) && vdOff + constants::kGdfxMagicLen <= sz) {
                    if (memcmp(d + vdOff, constants::kGdfxMagic, constants::kGdfxMagicLen) == 0) {
                        auto gdfxResult = GdfxParser::parseFromOffset(mf, searchOff - 32ULL * constants::kSectorSize, false);
                        if (gdfxResult.title_id != 0) {
                            info.title_id = gdfxResult.title_id;
                            info.media_id = gdfxResult.media_id;
                            info.version_value = gdfxResult.version_value;
                            info.version_string = gdfxResult.version_string;
                            if (!gdfxResult.title_name.empty()) {
                                info.title_name = std::move(gdfxResult.title_name);
                            }
                        }
                    }
                }
                break;
            }
        }

        char verBuf[64];
        snprintf(verBuf, sizeof(verBuf), "XSF @0x%llX", (unsigned long long)xsfOffset);
        info.format_version = verBuf;
        info.classification = classifyTitleId(info.title_id);
        return info;
    }
};
