// =============================================================================
// svod_parser.h — SVOD Parser
// =============================================================================
#pragma once
#include "../common.h"
#include "gdfx_parser.h"

class SvodParser {
public:
    [[nodiscard]] static TitleInfo parseDataFiles(
        const std::string& dataPath,
        const MappedFile& headerFile,
        bool verbose) noexcept
    {
        TitleInfo info;
        info.format = "SVOD";
        (void)verbose;

        const uint8_t* d = headerFile.data();
        const size_t vdOffset = 0x344 + 0x35;
        if (!headerFile.checkBounds(vdOffset, 0x24)) {
            info.error = "Cannot read SVOD descriptor";
            return info;
        }

        struct Layout { uint64_t magicOff; uint64_t baseOff; const char* name; };
        Layout layouts[] = {
            {0x2000, 0x0000, "EnhancedGDF"},
            {0x12000, 0x10000, "XSF"},
            {0xD000, 0xB000, "SingleFile"},
        };

        for (const auto& layout : layouts) {
            if (headerFile.checkBounds(layout.magicOff, constants::kGdfxMagicLen)) {
                if (memcmp(d + layout.magicOff, constants::kGdfxMagic, constants::kGdfxMagicLen) == 0) {
                    info.format_version = layout.name;
                    auto gdfxResult = GdfxParser::parseFromOffset(headerFile, layout.baseOff, false);
                    if (gdfxResult.title_id != 0) {
                        info.title_id = gdfxResult.title_id;
                        info.media_id = gdfxResult.media_id;
                        info.version_value = gdfxResult.version_value;
                        info.version_string = gdfxResult.version_string;
                        info.disc_number = gdfxResult.disc_number;
                        info.disc_count = gdfxResult.disc_count;
                        info.savegame_id = gdfxResult.savegame_id;
                        if (!gdfxResult.title_name.empty()) {
                            info.title_name = std::move(gdfxResult.title_name);
                        }
                    }
                    break;
                }
            }
        }

        if (info.title_id == 0 && !dataPath.empty()) {
            info.error = "Could not extract title ID from SVOD data files";
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }
};
