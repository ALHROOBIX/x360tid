// =============================================================================
// gdfx_parser.h — GDFX/ISO Parser
// =============================================================================
// Uses "MICROSOFT*XBOX*MEDIA" magic and binary tree directory walk.
// parseFromOffset() is public for use by XContentParser, SvodParser, XsfParser.
// =============================================================================
#pragma once
#include "../common.h"

class GdfxParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        for (size_t i = 0; i < constants::kXgdOffsetCount; ++i) {
            uint64_t offset = constants::kXgdOffsets[i] + 32ULL * constants::kSectorSize;
            if (offset + constants::kGdfxMagicLen > mf.size()) continue;
            if (memcmp(mf.data() + offset, constants::kGdfxMagic, constants::kGdfxMagicLen) == 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf, bool verbose) noexcept {
        for (size_t i = 0; i < constants::kXgdOffsetCount; ++i) {
            uint64_t gameOffset = constants::kXgdOffsets[i];
            auto result = parseFromOffset(mf, gameOffset, verbose);
            if (result.title_id != 0 || result.error.empty()) {
                result.format = "GDFX";
                char buf[32];
                snprintf(buf, sizeof(buf), "XGD%zu", i);
                result.format_version = buf;
                return result;
            }
        }
        TitleInfo info;
        info.format = "GDFX";
        info.error = "Could not find GDFX volume descriptor";
        return info;
    }

    [[nodiscard]] static TitleInfo parseFromOffset(const MappedFile& mf, uint64_t gameOffset, bool verbose) noexcept {
        TitleInfo info;
        (void)verbose;
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        uint64_t vdOffset;
        if (!safeAdd<uint64_t>(gameOffset, 32ULL * constants::kSectorSize, vdOffset)) {
            info.error = "Offset overflow";
            return info;
        }

        if (vdOffset + constants::kGdfxMagicLen > sz) {
            info.error = "Volume descriptor out of bounds";
            return info;
        }

        if (memcmp(d + vdOffset, constants::kGdfxMagic, constants::kGdfxMagicLen) != 0) {
            info.error = "GDFX magic not found at offset";
            return info;
        }

        uint32_t rootSector, rootSize;
        if (!readU32LE(d, sz, vdOffset + 0x14, rootSector)) {
            info.error = "Cannot read root sector";
            return info;
        }
        if (!readU32LE(d, sz, vdOffset + 0x18, rootSize)) {
            info.error = "Cannot read root size";
            return info;
        }

        uint64_t rootSectorOffset;
        uint64_t rootOffset;
        if (!safeMul<uint64_t>(rootSector, constants::kSectorSize, rootSectorOffset)) {
            info.error = "Root sector overflow";
            return info;
        }
        if (!safeAdd<uint64_t>(gameOffset, rootSectorOffset, rootOffset)) {
            info.error = "Root offset overflow";
            return info;
        }

        if (rootSize == 0 || rootSize > constants::kMaxFileSize) {
            info.error = "Invalid root size";
            return info;
        }

        if (!mf.checkBounds(static_cast<size_t>(rootOffset), static_cast<size_t>(rootSize))) {
            info.error = "Root directory out of bounds";
            return info;
        }

        // Find default.xex - get both file offset and data
        uint64_t xexFileOffset = 0;
        uint32_t xexFileSize = 0;
        std::vector<uint8_t> xexHeaderData;

        if (findFileInTree(mf, rootOffset, rootSize, gameOffset, "default.xex",
                           xexHeaderData, 0, &xexFileOffset, &xexFileSize)) {
            if (xexHeaderData.size() >= 4) {
                uint32_t magic = (uint32_t(xexHeaderData[0]) << 24) | (uint32_t(xexHeaderData[1]) << 16) |
                                 (uint32_t(xexHeaderData[2]) << 8) | uint32_t(xexHeaderData[3]);
                if (magic == constants::kXex2Magic || magic == constants::kXex1Magic) {
                    // Parse XEX header from the first 64KB
                    TitleInfo xexInfo = parseXexFromMemory(xexHeaderData.data(), xexHeaderData.size(), true);
                    if (xexInfo.title_id != 0) {
                        info.title_id = xexInfo.title_id;
                        info.media_id = xexInfo.media_id;
                        info.version_value = xexInfo.version_value;
                        info.base_version = xexInfo.base_version;
                        info.version_string = xexInfo.version_string;
                        info.disc_number = xexInfo.disc_number;
                        info.disc_count = xexInfo.disc_count;
                        info.savegame_id = xexInfo.savegame_id;
                        info.alternate_title_ids = std::move(xexInfo.alternate_title_ids);
                        info.title_name = std::move(xexInfo.title_name);
                    }

                    // If no name found from the 64KB parse, try reading much more XEX data
                    // directly from the mapped file. The SPA/XDBF resources containing the
                    // game title are often deep in the PE image, far beyond the first 64KB.
                    if (info.title_name.empty() && info.title_id != 0 && xexFileOffset != 0 && xexFileSize > 0) {
                        // Strategy 1: Try to extract name via XEX2 Resource Info with mapped file access
                        info.title_name = extractNameFromXexMappedFile(mf, xexFileOffset, xexFileSize);

                        // Strategy 2: Brute-force UTF-16BE scan of a large portion of the XEX data
                        if (info.title_name.empty()) {
                            size_t fullReadSize = std::min(static_cast<size_t>(xexFileSize), static_cast<size_t>(4 * 1024 * 1024));
                            if (mf.checkBounds(static_cast<size_t>(xexFileOffset), fullReadSize)) {
                                const uint8_t* xexFull = d + static_cast<size_t>(xexFileOffset);
                                info.title_name = scanForUtf16BEName(xexFull, fullReadSize);
                            }
                        }
                    }
                }
            }
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }

private:
    // =====================================================================
    // Extract game name from XEX2 Resource Info using the mapped file
    // This resolves virtual addresses properly by reading the XEX2 header
    // to find the image base, then translating resource addresses to
    // file offsets within the XEX.
    // =====================================================================
    [[nodiscard]] static std::string extractNameFromXexMappedFile(
        const MappedFile& mf, uint64_t xexOffset, uint32_t /*xexSize*/) noexcept
    {
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        if (xexOffset + 0x18 > sz) return {};

        // Read XEX2 header to find Resource Info entries and image base
        uint32_t headerCount = 0;
        if (!readU32BE(d, sz, xexOffset + 0x14, headerCount)) return {};
        if (headerCount == 0 || headerCount > 65536) return {};

        // Find image base address (key 0x00010200 IMAGE_BASE_ADDRESS)
        // and Resource Info entries (key 0x00000200 RESOURCE_INFO)
        uint32_t imageBaseAddr = 0;

        // Read the PE data offset from XEX header at offset 0x08 (header_size field
        // is actually at 0x08, but the PE data starts right after the XEX2 headers)
        uint32_t xexHeaderSize = 0;
        (void)readU32BE(d, sz, xexOffset + 0x08, xexHeaderSize);

        // First pass: find image base address
        for (uint32_t i = 0; i < headerCount; ++i) {
            size_t off = xexOffset + 0x18 + i * 8;
            uint32_t key, value;
            if (!readU32BE(d, sz, off, key)) break;
            if (!readU32BE(d, sz, off + 4, value)) break;

            uint32_t keyBase = key & 0xFFFFFF00;
            uint8_t keyLow = key & 0xFF;

            if (keyBase == 0x00010000) { // ORIGINAL_BASE_ADDRESS
                uint64_t dataOffset;
                if (keyLow == 0x00) dataOffset = off + 4;
                else if (keyLow == 0x01) { if (!safeAdd<uint64_t>(off + 4, value, dataOffset)) continue; }
                else dataOffset = value;

                if (dataOffset + 4 <= sz) {
                    uint32_t baseAddr32 = 0;
                    if (readU32BE(d, sz, dataOffset, baseAddr32)) {
                        imageBaseAddr = baseAddr32;
                    }
                }
            }
        }

        // If no image base found, use common Xbox 360 defaults
        if (imageBaseAddr == 0) imageBaseAddr = 0x82000000;

        // Second pass: find Resource Info entries and extract game names
        for (uint32_t i = 0; i < headerCount; ++i) {
            size_t off = xexOffset + 0x18 + i * 8;
            uint32_t key, value;
            if (!readU32BE(d, sz, off, key)) break;
            if (!readU32BE(d, sz, off + 4, value)) break;

            uint32_t keyBase = key & 0xFFFFFF00;
            uint8_t keyLow = key & 0xFF;

            if (keyBase != constants::kXex2ResourceInfo) continue;

            uint64_t dataOffset;
            if (keyLow == 0x00) dataOffset = off + 4;
            else if (keyLow == 0x01) { if (!safeAdd<uint64_t>(off + 4, value, dataOffset)) continue; }
            else dataOffset = value;

            if (dataOffset + 4 > sz) continue;

            // Read resource count
            uint32_t resCount = 0;
            if (!readU32BE(d, sz, dataOffset, resCount)) continue;
            if (resCount == 0 || resCount > 1024) continue;

            // Try both 16-byte and 12-byte entry sizes
            for (int entrySize : {16, 12}) {
                size_t entryPos = dataOffset + 4;

                for (uint32_t ri = 0; ri < resCount; ++ri) {
                    if (entryPos + static_cast<size_t>(entrySize) > sz) break;

                    uint32_t resType = 0, resName = 0, resAddr = 0, resSize = 0;
                    (void)readU32BE(d, sz, entryPos, resType);
                    (void)readU32BE(d, sz, entryPos + 4, resName);

                    if (entrySize == 16) {
                        (void)readU32BE(d, sz, entryPos + 8, resAddr);
                        (void)readU32BE(d, sz, entryPos + 12, resSize);
                    } else {
                        (void)readU32BE(d, sz, entryPos + 8, resAddr);
                        resSize = 0;
                    }

                    // Only process SPA (type 2) and XDBF (type 4) resources
                    if (resType == 0x00000002 || resType == 0x00000004) {
                        // Translate virtual address to XEX file offset
                        // Method 1: resAddr is a PE virtual address relative to imageBase
                        // The PE data typically starts at xexHeaderSize in the XEX file
                        if (resAddr >= imageBaseAddr) {
                            uint64_t fileOff = xexOffset + xexHeaderSize + (resAddr - imageBaseAddr);
                            // Try reading XDBF at this file offset
                            if (fileOff + 4 <= sz) {
                                uint32_t xdbfMagic = 0;
                                if (readU32BE(d, sz, fileOff, xdbfMagic) && xdbfMagic == constants::kXdbfMagic) {
                                    std::string name = extractTitleFromXdbf(d, sz, fileOff);
                                    if (!name.empty()) return name;
                                }
                                // Try scanning for UTF-16BE name around this area
                                if (resSize > 0 && resSize <= 65536) {
                                    size_t scanLen = std::min(static_cast<size_t>(resSize), sz - static_cast<size_t>(fileOff));
                                    std::string name = scanForUtf16BENameRegion(d + fileOff, scanLen);
                                    if (!name.empty()) return name;
                                }
                            }
                        }

                        // Method 2: resAddr with high bit masked (common in some XEX formats)
                        {
                            size_t scanStart = resAddr & 0x7FFFFFFFu;
                            uint64_t fileOff = xexOffset + scanStart;
                            if (fileOff + 4 <= sz && fileOff != xexOffset + xexHeaderSize + (resAddr - imageBaseAddr)) {
                                uint32_t xdbfMagic = 0;
                                if (readU32BE(d, sz, fileOff, xdbfMagic) && xdbfMagic == constants::kXdbfMagic) {
                                    std::string name = extractTitleFromXdbf(d, sz, fileOff);
                                    if (!name.empty()) return name;
                                }
                            }
                        }

                        // Method 3: resAddr as raw offset from XEX start
                        {
                            uint64_t fileOff = xexOffset + resAddr;
                            if (fileOff + 4 <= sz) {
                                uint32_t xdbfMagic = 0;
                                if (readU32BE(d, sz, fileOff, xdbfMagic) && xdbfMagic == constants::kXdbfMagic) {
                                    std::string name = extractTitleFromXdbf(d, sz, fileOff);
                                    if (!name.empty()) return name;
                                }
                            }
                        }

                        // Method 4: resAddr as sector offset (resAddr * sectorSize)
                        {
                            size_t sectorOff = static_cast<size_t>(resAddr) * constants::kSectorSize;
                            uint64_t fileOff = xexOffset + sectorOff;
                            if (fileOff + 4 <= sz) {
                                uint32_t xdbfMagic = 0;
                                if (readU32BE(d, sz, fileOff, xdbfMagic) && xdbfMagic == constants::kXdbfMagic) {
                                    std::string name = extractTitleFromXdbf(d, sz, fileOff);
                                    if (!name.empty()) return name;
                                }
                            }
                        }
                    }

                    entryPos += static_cast<size_t>(entrySize);
                }
            }
        }

        return {};
    }

    [[nodiscard]] static bool findFileInTree(
        const MappedFile& mf, uint64_t dirOffset, uint32_t dirSize,
        uint64_t gameOffset, const char* targetName,
        std::vector<uint8_t>& outData, int depth,
        uint64_t* outFileOffset = nullptr, uint32_t* outFileSize = nullptr) noexcept
    {
        if (depth > static_cast<int>(constants::kMaxDepth)) return false;
        if (!mf.checkBounds(static_cast<size_t>(dirOffset), dirSize)) return false;
        return walkTreeNode(mf, dirOffset, dirSize, gameOffset, targetName, outData, 0, depth,
                            outFileOffset, outFileSize);
    }

    [[nodiscard]] static bool walkTreeNode(
        const MappedFile& mf, uint64_t dirOffset, uint32_t dirSize,
        uint64_t gameOffset, const char* targetName,
        std::vector<uint8_t>& outData, uint32_t ordinal, int depth,
        uint64_t* outFileOffset = nullptr, uint32_t* outFileSize = nullptr) noexcept
    {
        if (depth > static_cast<int>(constants::kMaxDepth)) return false;

        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        size_t entryOffset = static_cast<size_t>(dirOffset) + ordinal;

        if (entryOffset + 0xE + 1 > sz) return false;
        if (entryOffset + 0xE > static_cast<size_t>(dirOffset) + dirSize) return false;

        uint16_t nodeL, nodeR;
        uint32_t sector, length;
        uint8_t attributes, nameLength;

        if (!readU16LE(d, sz, entryOffset + 0x0, nodeL)) return false;
        if (!readU16LE(d, sz, entryOffset + 0x2, nodeR)) return false;
        if (!readU32LE(d, sz, entryOffset + 0x4, sector)) return false;
        if (!readU32LE(d, sz, entryOffset + 0x8, length)) return false;
        if (!readU8(d, sz, entryOffset + 0xC, attributes)) return false;
        if (!readU8(d, sz, entryOffset + 0xD, nameLength)) return false;

        if (nameLength == 0) return false;
        if (entryOffset + 0xE + nameLength > sz) return false;

        // In-order: left -> current -> right
        if (nodeL > 0 && nodeL * 4 < dirSize) {
            if (walkTreeNode(mf, dirOffset, dirSize, gameOffset, targetName, outData, nodeL * 4, depth + 1,
                             outFileOffset, outFileSize)) {
                return true;
            }
        }

        const char* entryName = reinterpret_cast<const char*>(d + entryOffset + 0xE);
        bool isDir = (attributes & 0x10) != 0;

        if (!isDir && strEqCI(entryName, targetName, nameLength)) {
            uint64_t sectorOffset, fileOffset;
            if (!safeMul<uint64_t>(sector, constants::kSectorSize, sectorOffset)) return false;
            if (!safeAdd<uint64_t>(gameOffset, sectorOffset, fileOffset)) return false;

            if (length > constants::kMaxFileSize) return false;
            if (!mf.checkBounds(static_cast<size_t>(fileOffset), static_cast<size_t>(length))) return false;

            // Return file offset and size if requested
            if (outFileOffset) *outFileOffset = fileOffset;
            if (outFileSize) *outFileSize = length;

            size_t readSize = static_cast<size_t>(length);
            if (readSize > 65536) readSize = 65536;
            outData.resize(readSize);
            std::memcpy(outData.data(), d + fileOffset, readSize);
            return true;
        }

        if (nodeR > 0 && nodeR * 4 < dirSize) {
            if (walkTreeNode(mf, dirOffset, dirSize, gameOffset, targetName, outData, nodeR * 4, depth + 1,
                             outFileOffset, outFileSize)) {
                return true;
            }
        }

        return false;
    }
};
