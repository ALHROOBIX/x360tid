// =============================================================================
// xdbf_parser.h — XDBF/GPD Parser
// =============================================================================
#pragma once
#include "../common.h"

class XdbfParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        if (mf.size() < 24) return false;
        uint32_t magic;
        if (!readU32BE(mf.data(), mf.size(), 0, magic)) return false;
        return magic == constants::kXdbfMagic;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf) noexcept {
        TitleInfo info;
        info.format = "GPD";
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        if (sz < 24) { info.error = "File too small for XDBF"; return info; }

        uint32_t version = 0, entryCount = 0, entryUsed = 0, freeCount = 0, freeUsed = 0;
        (void)readU32BE(d, sz, 0x04, version);
        (void)readU32BE(d, sz, 0x08, entryCount);
        (void)readU32BE(d, sz, 0x0C, entryUsed);
        (void)readU32BE(d, sz, 0x10, freeCount);
        (void)readU32BE(d, sz, 0x14, freeUsed);

        char verBuf[32];
        snprintf(verBuf, sizeof(verBuf), "XDBF v0x%08X", version);
        info.format_version = verBuf;

        size_t entryTableOffset = 24;
        size_t totalEntries = static_cast<size_t>(entryCount) + static_cast<size_t>(freeCount);
        if (totalEntries > constants::kMaxDirectoryEntries) totalEntries = constants::kMaxDirectoryEntries;

        for (size_t i = 0; i < totalEntries; ++i) {
            size_t entryOff = entryTableOffset + i * 18;
            if (entryOff + 18 > sz) break;

            uint16_t section = 0;
            uint64_t id = 0;
            uint32_t entryOffset = 0, entrySize = 0;

            (void)readU16BE(d, sz, entryOff + 0x00, section);

            uint32_t idHigh = 0, idLow = 0;
            (void)readU32BE(d, sz, entryOff + 0x02, idHigh);
            (void)readU32BE(d, sz, entryOff + 0x06, idLow);
            id = ((uint64_t)idHigh << 32) | idLow;

            (void)readU32BE(d, sz, entryOff + 0x0A, entryOffset);
            (void)readU32BE(d, sz, entryOff + 0x0E, entrySize);

            if (section == 0x8000 && entrySize >= 4 && entryOffset + 4 <= sz) {
                if (info.title_id == 0) {
                    info.title_id = static_cast<uint32_t>(id & 0xFFFFFFFF);
                }
            }
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }
};
