// =============================================================================
// xcontent_parser.h — CON/LIVE/PIRS (XContent) + STFS Parser
// =============================================================================
// Includes: stfsBlockToOffset, XContentParser, StfsParser
// XContentParser handles CON/LIVE/PIRS packages with STFS/SVOD content.
// =============================================================================
#pragma once
#include "../common.h"
#include "gdfx_parser.h"

// ============================================================================
// STFS Utility
// ============================================================================

[[nodiscard]] inline int64_t stfsBlockToOffset(uint32_t blockIndex, uint32_t blocksPerHashTable, uint32_t headerSize) noexcept {
    uint32_t block = blockIndex;
    uint32_t bi = blockIndex;
    for (int i = 0; i < 3; ++i) {
        uint32_t levelBlocks = constants::kStfsBlocksPerHashLevel[i];
        block += ((bi + levelBlocks) / levelBlocks) * blocksPerHashTable;
        if (bi < levelBlocks) break;
    }
    uint64_t headerAligned = ((uint64_t)headerSize + 0xFFF) & ~0xFFFULL;
    int64_t offset;
    if (!safeAdd<int64_t>(static_cast<int64_t>(headerAligned), static_cast<int64_t>((uint64_t)block << 12), offset)) {
        return -1;
    }
    return offset;
}

// ============================================================================
// XContentParser
// ============================================================================

class XContentParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        if (mf.size() < 4) return false;
        uint32_t magic;
        if (!readU32BE(mf.data(), mf.size(), 0, magic)) return false;
        return magic == constants::kConMagic || magic == constants::kLiveMagic || magic == constants::kPirsMagic;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf, bool /*verbose*/, bool deep) noexcept {
        TitleInfo info;
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        uint32_t magic;
        if (!readU32BE(d, sz, 0, magic)) { info.error = "Cannot read magic"; return info; }
        if (magic == constants::kConMagic) info.format = "CON";
        else if (magic == constants::kLiveMagic) info.format = "LIVE";
        else if (magic == constants::kPirsMagic) info.format = "PIRS";
        else { info.error = "Unknown magic"; return info; }

        uint32_t header_size;
        if (!readU32BE(d, sz, 0x340, header_size)) {
            info.error = "Cannot read header_size";
            return info;
        }

        uint32_t content_type;
        if (!readU32BE(d, sz, 0x344, content_type)) {
            info.error = "Cannot read content_type";
            return info;
        }
        info.content_type = content_type;
        info.content_type_str = contentTypeToString(content_type);

        uint32_t metadata_version = 0;
        (void)readU32BE(d, sz, 0x344 + 0x04, metadata_version);
        char verBuf[32];
        snprintf(verBuf, sizeof(verBuf), "%s v%u", info.format.c_str(), metadata_version);
        info.format_version = verBuf;

        // Execution info at offset 0x344 + 0x10
        const size_t execInfoOffset = 0x344 + 0x10;
        if (mf.checkBounds(execInfoOffset, 0x18)) {
            ExecutionInfo ei = parseExecutionInfo(d, sz, execInfoOffset);
            info.title_id = ei.title_id;
            info.media_id = ei.media_id;
            info.version_value = ei.version_value;
            info.base_version = ei.base_version_value;
            info.version_string = ei.versionString();
            info.disc_number = ei.disc_number;
            info.disc_count = ei.disc_count;
            info.savegame_id = ei.savegame_id;
        }

        // Title name at offset 0x344 + 0x134D
        const size_t titleNameOffset = 0x344 + 0x134D;
        if (mf.checkBounds(titleNameOffset, 128)) {
            info.title_name = utf16beToUtf8(d + titleNameOffset, 64);
        }

        // Volume type
        uint32_t volume_type = 0;
        (void)readU32BE(d, sz, 0x344 + 0x65, volume_type);

        if (deep && volume_type == 0 && info.title_id == 0) {
            // STFS - try to find .xex and extract title ID
            auto stfsResult = tryExtractStfsXex(mf);
            if (stfsResult.title_id != 0 && info.title_id == 0) {
                info.title_id = stfsResult.title_id;
                info.media_id = stfsResult.media_id;
                info.version_value = stfsResult.version_value;
                info.version_string = stfsResult.version_string;
            }
        }
        else if (deep && volume_type == 1) {
            parseSvodContent(mf, info);
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }

private:
    [[nodiscard]] static TitleInfo tryExtractStfsXex(const MappedFile& mf) noexcept {
        TitleInfo info;
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        const size_t vdOffset = 0x344 + 0x35;
        if (!mf.checkBounds(vdOffset, 0x24)) return info;

        uint8_t flags = d[vdOffset + 0x02];
        uint16_t file_table_block_count = 0;
        (void)readU16BE(d, sz, vdOffset + 0x03, file_table_block_count);

        uint32_t file_table_block_number = 0;
        (void)readU24LE(d, sz, vdOffset + 0x05, file_table_block_number);

        bool readOnly = (flags & 0x01) != 0;
        uint32_t blocksPerHashTable = readOnly ? 1 : 2;

        uint32_t headerSize = 0;
        (void)readU32BE(d, sz, 0x340, headerSize);

        int64_t ftOffset = stfsBlockToOffset(file_table_block_number, blocksPerHashTable, headerSize);
        if (ftOffset < 0 || !mf.checkBounds(static_cast<size_t>(ftOffset), constants::kStfsBlockSize)) {
            return info;
        }

        uint32_t totalEntries = file_table_block_count * 64;
        if (totalEntries > constants::kMaxDirectoryEntries) totalEntries = constants::kMaxDirectoryEntries;

        for (uint32_t i = 0; i < totalEntries; ++i) {
            size_t entryOff = static_cast<size_t>(ftOffset) + i * constants::kStfsDirEntrySize;
            if (!mf.checkBounds(entryOff, constants::kStfsDirEntrySize)) break;

            const uint8_t* namePtr = d + entryOff;
            if (namePtr[0] == 0) continue;

            uint8_t flagsByte = d[entryOff + 0x28];
            uint8_t nameLength = flagsByte & 0x3F;
            bool isDir = (flagsByte & 0x80) != 0;
            if (nameLength == 0 || nameLength > 40) continue;

            std::string name = win1252ToUtf8(namePtr, nameLength);

            if (!isDir && name.size() >= 4) {
                auto ext = name.substr(name.size() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".xex") {
                    uint32_t startBlock = 0;
                    (void)readU24LE(d, sz, entryOff + 0x2F, startBlock);
                    uint32_t fileSize = 0;
                    (void)readU32BE(d, sz, entryOff + 0x34, fileSize);

                    if (fileSize >= 4 && startBlock != constants::kStfsEndOfChain) {
                        int64_t blockOff = stfsBlockToOffset(startBlock, blocksPerHashTable, headerSize);
                        if (blockOff >= 0 && mf.checkBounds(static_cast<size_t>(blockOff), 4)) {
                            uint32_t magic = (uint32_t(d[blockOff]) << 24) | (uint32_t(d[blockOff + 1]) << 16) |
                                             (uint32_t(d[blockOff + 2]) << 8) | uint32_t(d[blockOff + 3]);
                            if (magic == constants::kXex2Magic || magic == constants::kXex1Magic) {
                                size_t readSize = std::min(static_cast<size_t>(fileSize), size_t(65536));
                                if (mf.checkBounds(static_cast<size_t>(blockOff), readSize)) {
                                    info = parseXexFromMemory(d + blockOff, readSize);
                                    if (info.title_id != 0) return info;
                                }
                            }
                        }
                    }
                }
            }
        }

        return info;
    }

    static void parseSvodContent(const MappedFile& mf, TitleInfo& info) noexcept {
        const uint8_t* d = mf.data();
        const size_t vdOffset = 0x344 + 0x35;
        if (!mf.checkBounds(vdOffset, 0x24)) return;

        struct Layout { uint64_t magic; uint64_t base; };
        Layout layouts[] = {
            {0x2000, 0x0000},   // EnhancedGDF
            {0x12000, 0x10000}, // XSF
            {0xD000, 0xB000},   // SingleFile
            {0x2000, 0x0000},   // MultipleFiles
        };

        for (const auto& l : layouts) {
            if (mf.checkBounds(l.magic, constants::kGdfxMagicLen)) {
                if (memcmp(d + l.magic, constants::kGdfxMagic, constants::kGdfxMagicLen) == 0) {
                    auto gdfxResult = GdfxParser::parseFromOffset(mf, l.base, false);
                    if (gdfxResult.title_id != 0) {
                        info.title_id = gdfxResult.title_id;
                        info.media_id = gdfxResult.media_id;
                        info.version_value = gdfxResult.version_value;
                        info.version_string = gdfxResult.version_string;
                        info.disc_number = gdfxResult.disc_number;
                        info.disc_count = gdfxResult.disc_count;
                        info.savegame_id = gdfxResult.savegame_id;
                        // Also get game name from GDFX/XEX
                        if (!gdfxResult.title_name.empty() && info.title_name.empty()) {
                            info.title_name = std::move(gdfxResult.title_name);
                        }
                    }
                    return;
                }
            }
        }
    }
};

// ============================================================================
// StfsParser (standalone, for direct file extraction from STFS)
// ============================================================================

class StfsParser {
public:
    [[nodiscard]] static bool readFile(
        const MappedFile& mf,
        const std::string& targetFile,
        std::vector<uint8_t>& outData) noexcept
    {
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        const size_t vdOffset = 0x344 + 0x35;
        if (!mf.checkBounds(vdOffset, 0x24)) return false;

        uint8_t flags = d[vdOffset + 0x02];
        uint16_t file_table_block_count = 0;
        (void)readU16BE(d, sz, vdOffset + 0x03, file_table_block_count);

        uint32_t file_table_block_number = 0;
        (void)readU24LE(d, sz, vdOffset + 0x05, file_table_block_number);

        bool readOnly = (flags & 0x01) != 0;
        uint32_t blocksPerHashTable = readOnly ? 1 : 2;

        uint32_t headerSize = 0;
        (void)readU32BE(d, sz, 0x340, headerSize);

        int64_t ftOffset = stfsBlockToOffset(file_table_block_number, blocksPerHashTable, headerSize);
        if (ftOffset < 0) return false;

        uint32_t totalEntries = file_table_block_count * 64;
        for (uint32_t i = 0; i < totalEntries; ++i) {
            size_t entryOff = static_cast<size_t>(ftOffset) + i * constants::kStfsDirEntrySize;
            if (!mf.checkBounds(entryOff, constants::kStfsDirEntrySize)) break;

            const uint8_t* namePtr = d + entryOff;
            if (namePtr[0] == 0) continue;

            uint8_t flagsByte = d[entryOff + 0x28];
            uint8_t nameLength = flagsByte & 0x3F;
            bool isDir = (flagsByte & 0x80) != 0;

            if (nameLength == 0 || nameLength > 40) continue;
            std::string name = win1252ToUtf8(namePtr, nameLength);

            if (!isDir && strEqCI(name.c_str(), targetFile.c_str(), targetFile.size())) {
                uint32_t startBlock = 0;
                (void)readU24LE(d, sz, entryOff + 0x2F, startBlock);
                uint32_t fileSize = 0;
                (void)readU32BE(d, sz, entryOff + 0x34, fileSize);

                if (fileSize == 0) return false;
                size_t readSize = std::min(static_cast<size_t>(fileSize), size_t(65536));
                outData.resize(readSize);

                size_t bytesRemaining = readSize;
                uint32_t currentBlock = startBlock;
                size_t outOffset = 0;
                uint32_t chainLen = 0;

                while (currentBlock != constants::kStfsEndOfChain && bytesRemaining > 0) {
                    if (++chainLen > constants::kMaxBlockChainLength) break;

                    int64_t blockOffset = stfsBlockToOffset(currentBlock, blocksPerHashTable, headerSize);
                    if (blockOffset < 0) break;

                    size_t toRead = std::min(bytesRemaining, constants::kStfsBlockSize);
                    if (!mf.checkBounds(static_cast<size_t>(blockOffset), toRead)) break;

                    std::memcpy(outData.data() + outOffset, d + blockOffset, toRead);
                    outOffset += toRead;
                    bytesRemaining -= toRead;

                    currentBlock++;
                    if (currentBlock >= 0xFFF0) break;
                }

                return outOffset > 0;
            }
        }
        return false;
    }
};
