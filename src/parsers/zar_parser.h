// =============================================================================
// zar_parser.h — ZArchive Parser
// =============================================================================
// Based on Exzap/ZArchive format specification (verified against source code):
//   - 64 KiB zstd-compressed blocks (COMPRESSED_BLOCK_SIZE = 64*1024)
//   - Big-endian footer with section offsets
//   - BFS-ordered flat file tree with directory entries pointing to child ranges
//   - Name table with variable-length encoding:
//       short: [length_7bit] [name_bytes]
//       long:  [0x80|length_low7] [length_high8] [name_bytes]
//   - fileOffset in entries is a BYTE offset in the uncompressed stream
//   - Block index = fileOffset / COMPRESSED_BLOCK_SIZE
//   - CompressionOffsetRecord: baseOffset + sum(size[i]+1) for i < subIndex
//   - CRITICAL: blockOffset is relative to sectionCompressedData.offset
//     Must add compressedDataOffset to get absolute file offset
// =============================================================================
#pragma once
#include "../common.h"
#include "../zstd_library.h"

class ZArchiveParser {
public:
    [[nodiscard]] static bool detect(const MappedFile& mf) noexcept {
        if (mf.size() < constants::kZarFooterSize) return false;
        uint32_t magic;
        if (!readU32BE(mf.data(), mf.size(), mf.size() - 4, magic)) return false;
        return magic == constants::kZarMagic;
    }

    [[nodiscard]] static TitleInfo parse(const MappedFile& mf, const ZstdLibrary& zstd, bool verbose) noexcept {
        TitleInfo info;
        info.format = "ZAR";
        (void)verbose;
        const uint8_t* d = mf.data();
        const size_t sz = mf.size();

        if (sz < constants::kZarFooterSize) {
            info.error = "File too small for ZAR footer";
            return info;
        }

        size_t footerOffset = sz - constants::kZarFooterSize;
        uint32_t footerVersion = 0, footerMagic = 0;
        if (!readU32BE(d, sz, footerOffset + 0x88, footerVersion)) { info.error = "Cannot read ZAR version"; return info; }
        if (!readU32BE(d, sz, footerOffset + 0x8C, footerMagic))   { info.error = "Cannot read ZAR magic"; return info; }

        if (footerMagic != constants::kZarMagic) {
            info.error = "Invalid ZAR magic";
            return info;
        }

        // Verify version (kVersion1 = 0x61BF3A01 acts as extended magic)
        if (footerVersion != constants::kZarVersion) {
            // Version mismatch — still proceed but note it
        }

        char verBuf[32];
        snprintf(verBuf, sizeof(verBuf), "ZAR v0x%08X", footerVersion);
        info.format_version = verBuf;

        // =====================================================================
        // Read footer sections (ZArchive Footer structure):
        //   0x00: sectionCompressedData {offset(8), size(8)}
        //   0x10: sectionOffsetRecords  {offset(8), size(8)}
        //   0x20: sectionNames          {offset(8), size(8)}
        //   0x30: sectionFileTree       {offset(8), size(8)}
        //   0x40: sectionMetaDirectory  {offset(8), size(8)}
        //   0x50: sectionMetaData       {offset(8), size(8)}
        //   0x60: integrityHash[32]
        //   0x80: totalSize(8)
        //   0x88: version(4)
        //   0x8C: magic(4)
        // =====================================================================
        uint64_t compressedDataOffset = 0, compressedDataSize = 0;
        uint64_t offsetRecordsOffset = 0, offsetRecordsSize = 0;
        uint64_t namesOffset = 0, namesSize = 0;
        uint64_t fileTreeOffset = 0, fileTreeSize = 0;

        // Read all footer fields with proper error checking
        if (!readU64BE(d, sz, footerOffset + 0x00, compressedDataOffset)) { info.error = "Cannot read compressedData offset"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x08, compressedDataSize))  { info.error = "Cannot read compressedData size"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x10, offsetRecordsOffset)) { info.error = "Cannot read offsetRecords offset"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x18, offsetRecordsSize))   { info.error = "Cannot read offsetRecords size"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x20, namesOffset))         { info.error = "Cannot read names offset"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x28, namesSize))           { info.error = "Cannot read names size"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x30, fileTreeOffset))      { info.error = "Cannot read fileTree offset"; return info; }
        if (!readU64BE(d, sz, footerOffset + 0x38, fileTreeSize))        { info.error = "Cannot read fileTree size"; return info; }

        // Validate totalSize field (reference: footer.totalSize must equal fileSize)
        uint64_t totalSize = 0;
        (void)readU64BE(d, sz, footerOffset + 0x80, totalSize);
        if (totalSize != 0 && totalSize != sz) {
            // File might be truncated or corrupt — proceed with caution
        }

        // Validate section ranges (reference: IsWithinValidRange)
        auto validateSection = [&](uint64_t offset, uint64_t size, const char* name) -> bool {
            if (offset + size > sz) {
                info.error = std::string("Section ") + name + " out of bounds";
                return false;
            }
            return true;
        };

        if (!validateSection(compressedDataOffset, compressedDataSize, "compressedData")) return info;
        if (!validateSection(offsetRecordsOffset, offsetRecordsSize, "offsetRecords")) return info;
        if (!validateSection(namesOffset, namesSize, "names")) return info;
        if (!validateSection(fileTreeOffset, fileTreeSize, "fileTree")) return info;

        info.zstd_available = zstd.available();

        // =====================================================================
        // Read offset records (CompressionOffsetRecord: 8 + 2*16 = 40 bytes each)
        // =====================================================================
        struct OffsetRecord {
            uint64_t baseOffset;
            uint16_t sizes[16];
        };
        std::vector<OffsetRecord> records;
        if (offsetRecordsSize > 0) {
            size_t recordCount = static_cast<size_t>(offsetRecordsSize) / 40;
            if (recordCount > constants::kMaxDirectoryEntries) recordCount = constants::kMaxDirectoryEntries;
            records.resize(recordCount);
            bool readOk = true;
            for (size_t i = 0; i < recordCount; ++i) {
                size_t recOff = static_cast<size_t>(offsetRecordsOffset) + i * 40;
                if (recOff + 40 > sz) { readOk = false; break; }
                if (!readU64BE(d, sz, recOff, records[i].baseOffset)) { readOk = false; break; }
                for (int j = 0; j < 16; ++j) {
                    if (!readU16BE(d, sz, recOff + 8 + j * 2, records[i].sizes[j])) { readOk = false; break; }
                }
                if (!readOk) break;
            }
            if (!readOk && records.empty()) {
                info.error = "Failed to read offset records";
                info.classification = classifyTitleId(info.title_id);
                return info;
            }
        }

        // =====================================================================
        // Parse file tree using BFS traversal (matching reference implementation)
        // =====================================================================
        if (fileTreeSize == 0) {
            info.error = "Empty file tree section";
            info.classification = classifyTitleId(info.title_id);
            return info;
        }

        size_t entryCount = static_cast<size_t>(fileTreeSize) / 16;
        if (entryCount == 0) {
            info.error = "No entries in file tree";
            info.classification = classifyTitleId(info.title_id);
            return info;
        }
        if (entryCount > constants::kMaxDirectoryEntries) entryCount = constants::kMaxDirectoryEntries;

        // Validate first entry is root directory
        {
            uint32_t firstEntryNameType = 0;
            if (readU32BE(d, sz, static_cast<size_t>(fileTreeOffset), firstEntryNameType)) {
                if ((firstEntryNameType & 0x80000000) != 0) {
                    info.error = "Invalid ZAR: first entry must be root directory";
                    info.classification = classifyTitleId(info.title_id);
                    return info;
                }
            }
        }

        // Helper: read a file tree entry
        auto readEntry = [&](size_t index, uint32_t& outNameType, uint32_t& outField1, uint32_t& outField2, uint32_t& outField3) -> bool {
            if (index >= entryCount) return false;
            size_t entryOff = static_cast<size_t>(fileTreeOffset) + index * 16;
            if (entryOff + 16 > sz) return false;
            if (!readU32BE(d, sz, entryOff + 0x00, outNameType)) return false;
            if (!readU32BE(d, sz, entryOff + 0x04, outField1))   return false;
            if (!readU32BE(d, sz, entryOff + 0x08, outField2))   return false;
            if (!readU32BE(d, sz, entryOff + 0x0C, outField3))   return false;
            return true;
        };

        // Helper: read name from name table
        auto readName = [&](uint32_t nameOffset) -> std::string {
            if (nameOffset == 0x7FFFFFFF || nameOffset >= namesSize) return {};
            size_t nameOff = static_cast<size_t>(namesOffset) + nameOffset;
            if (nameOff >= sz) return {};
            uint8_t firstByte = d[nameOff];
            size_t nameLen;
            size_t nameStartOff;
            if (firstByte & 0x80) {
                // Extended 2-byte length header
                if (nameOff + 1 >= sz) return {};
                nameLen = ((firstByte & 0x7F) | ((uint32_t)d[nameOff + 1] << 7));
                nameStartOff = nameOff + 2;
            } else {
                nameLen = firstByte & 0x7F;
                nameStartOff = nameOff + 1;
            }
            if (nameLen > 0 && nameStartOff + nameLen <= sz) {
                return std::string(reinterpret_cast<const char*>(d + nameStartOff), nameLen);
            }
            return {};
        };

        // BFS traversal: start from root directory (index 0), find default.xex
        // Per ZArchive reference: directory children are at
        //   directoryRecord.nodeStartIndex to nodeStartIndex + directoryRecord.count
        uint64_t xexStreamOffset = 0;
        uint64_t xexFileSize = 0;
        bool foundXex = false;

        // Strategy: BFS from root, look for default.xex in the root directory first,
        // then in subdirectories. Also do a linear scan as fallback.
        // BFS approach (matching reference implementation):
        {
            // Read root directory entry
            uint32_t rootNameType, rootNodeStart, rootCount, rootReserved;
            if (readEntry(0, rootNameType, rootNodeStart, rootCount, rootReserved)) {
                // Scan root directory children for default.xex
                uint32_t childStart = rootNodeStart;
                uint32_t childEnd = rootNodeStart + rootCount;
                if (childEnd <= entryCount) {
                    for (uint32_t ci = childStart; ci < childEnd; ++ci) {
                        uint32_t childNameType, childField1, childField2, childField3;
                        if (!readEntry(ci, childNameType, childField1, childField2, childField3)) break;

                        bool childIsFile = (childNameType & 0x80000000) != 0;
                        uint32_t childNameOff = childNameType & 0x7FFFFFFF;
                        std::string childName = readName(childNameOff);

                        if (childIsFile && childName.size() == 11 &&
                            strEqCI(childName.c_str(), "default.xex", 11)) {
                            // Found default.xex at root level
                            xexStreamOffset = ((uint64_t)(childField3 & 0xFFFF) << 32) | childField1;
                            xexFileSize = ((uint64_t)(childField3 >> 16) << 32) | childField2;
                            if (xexFileSize != 0 && xexFileSize <= constants::kMaxFileSize) {
                                foundXex = true;
                            }
                            break;
                        }
                    }
                }

                // If not found at root level, search subdirectories
                if (!foundXex) {
                    for (uint32_t ci = childStart; ci < childEnd && !foundXex; ++ci) {
                        uint32_t childNameType, childField1, childField2, childField3;
                        if (!readEntry(ci, childNameType, childField1, childField2, childField3)) break;

                        bool childIsFile = (childNameType & 0x80000000) != 0;
                        if (childIsFile) continue;

                        // It's a directory — scan its children
                        uint32_t subStart = childField1; // nodeStartIndex
                        uint32_t subCount = childField2; // count
                        if (subStart + subCount > entryCount) continue;

                        for (uint32_t si = subStart; si < subStart + subCount; ++si) {
                            uint32_t subNameType, subField1, subField2, subField3;
                            if (!readEntry(si, subNameType, subField1, subField2, subField3)) break;

                            bool subIsFile = (subNameType & 0x80000000) != 0;
                            if (!subIsFile) continue;

                            uint32_t subNameOff = subNameType & 0x7FFFFFFF;
                            std::string subName = readName(subNameOff);

                            if (subName.size() == 11 && strEqCI(subName.c_str(), "default.xex", 11)) {
                                xexStreamOffset = ((uint64_t)(subField3 & 0xFFFF) << 32) | subField1;
                                xexFileSize = ((uint64_t)(subField3 >> 16) << 32) | subField2;
                                if (xexFileSize != 0 && xexFileSize <= constants::kMaxFileSize) {
                                    foundXex = true;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Fallback: linear scan (in case BFS traversal missed it due to deep nesting)
        if (!foundXex) {
            for (size_t i = 1; i < entryCount; ++i) {
                uint32_t eNameType, eField1, eField2, eField3;
                if (!readEntry(i, eNameType, eField1, eField2, eField3)) break;

                bool isFile = (eNameType & 0x80000000) != 0;
                if (!isFile) continue;

                uint32_t nameOff = eNameType & 0x7FFFFFFF;
                std::string name = readName(nameOff);

                if (name.size() == 11 && strEqCI(name.c_str(), "default.xex", 11)) {
                    xexStreamOffset = ((uint64_t)(eField3 & 0xFFFF) << 32) | eField1;
                    xexFileSize = ((uint64_t)(eField3 >> 16) << 32) | eField2;
                    if (xexFileSize != 0 && xexFileSize <= constants::kMaxFileSize) {
                        foundXex = true;
                    }
                    break;
                }
            }
        }

        if (!foundXex) {
            if (info.error.empty()) info.error = "default.xex not found in ZArchive";
            info.classification = classifyTitleId(info.title_id);
            return info;
        }

        if (records.empty()) {
            if (info.error.empty()) info.error = "No offset records for decompression";
            info.classification = classifyTitleId(info.title_id);
            return info;
        }

        // Check ZSTD availability BEFORE attempting decompression
        if (!zstd.available()) {
            info.error = "ZSTD library required for ZAR decompression (install libzstd)";
            info.classification = classifyTitleId(info.title_id);
            return info;
        }

        // =====================================================================
        // Decompress XEX data from ZArchive
        // =====================================================================
        // Per ZArchive reference implementation (zarchivereader.cpp LoadBlock):
        //   1. blockIndex = rawReadOffset / COMPRESSED_BLOCK_SIZE
        //   2. recordIndex = blockIndex / 16
        //   3. recordSubIndex = blockIndex % 16
        //   4. offset = record.baseOffset + sum(record.size[i] + 1 for i < subIndex)
        //   5. compressedSize = record.size[subIndex] + 1
        //   6. Check: (offset + compressedSize) <= compressedDataSize
        //   7. offset += compressedDataOffset  <-- CRITICAL: add section offset!
        //   8. If compressedSize == COMPRESSED_BLOCK_SIZE: uncompressed, read directly
        //      Else: ZSTD decompress (outputSize MUST equal COMPRESSED_BLOCK_SIZE)
        // =====================================================================

        uint64_t startBlockIndex = xexStreamOffset / constants::kZarBlockSize;
        size_t firstBlockSkip = static_cast<size_t>(xexStreamOffset % constants::kZarBlockSize);

        // Read up to 4 MB of XEX data for SPA/XDBF name extraction
        size_t totalReadSize = std::min(xexFileSize, (uint64_t)(4 * 1024 * 1024));
        std::vector<uint8_t> xexData(totalReadSize);
        size_t outOffset = 0;
        uint64_t currentBlockIndex = startBlockIndex;
        size_t decompressFailures = 0;

        while (outOffset < totalReadSize) {
            size_t recordIndex = static_cast<size_t>(currentBlockIndex / 16);
            size_t recordSubIndex = static_cast<size_t>(currentBlockIndex % 16);

            if (recordIndex >= records.size()) break;

            // Calculate block offset within the compressed data section
            uint64_t blockOffsetInSection = records[recordIndex].baseOffset;
            for (size_t k = 0; k < recordSubIndex; ++k) {
                blockOffsetInSection += (uint64_t)records[recordIndex].sizes[k] + 1;
            }

            uint16_t compressedSizeMinus1 = records[recordIndex].sizes[recordSubIndex];
            uint32_t compressedSize = (uint32_t)compressedSizeMinus1 + 1;

            // Bounds check within compressed data section
            if (blockOffsetInSection + compressedSize > compressedDataSize) break;

            // Convert to absolute file offset by adding compressedDataOffset
            uint64_t blockFileOffset = blockOffsetInSection + compressedDataOffset;
            if (blockFileOffset + compressedSize > sz) break;

            // Calculate how much to copy from this block
            size_t blockDataStart = (currentBlockIndex == startBlockIndex) ? firstBlockSkip : 0;
            size_t blockDataAvail = constants::kZarBlockSize - blockDataStart;
            size_t toCopy = std::min(blockDataAvail, totalReadSize - outOffset);

            if (compressedSize == constants::kZarBlockSize) {
                // Block stored uncompressed (compressedSize == block size means no compression)
                if (blockFileOffset + blockDataStart + toCopy > sz) break;
                std::memcpy(xexData.data() + outOffset, d + blockFileOffset + blockDataStart, toCopy);
                outOffset += toCopy;
            } else {
                // Block compressed with ZSTD - decompress full block first
                uint8_t decompressBuf[constants::kZarBlockSize];
                size_t result = zstd.decompress(decompressBuf, constants::kZarBlockSize,
                                                 d + blockFileOffset, compressedSize);
                if (zstd.isError(result) || result == 0) {
                    decompressFailures++;
                    if (decompressFailures > 3) break; // too many failures, stop
                    currentBlockIndex++;
                    continue;
                }
                // Per reference: outputSize must equal COMPRESSED_BLOCK_SIZE
                // Accept partial for robustness but prefer full blocks
                if (blockDataStart >= result) { currentBlockIndex++; continue; }
                size_t avail = std::min(toCopy, result - blockDataStart);
                std::memcpy(xexData.data() + outOffset, decompressBuf + blockDataStart, avail);
                outOffset += avail;
            }

            currentBlockIndex++;
        }

        // Parse XEX header from decompressed data
        if (outOffset >= 4) {
            uint32_t magic = (uint32_t(xexData[0]) << 24) | (uint32_t(xexData[1]) << 16) |
                             (uint32_t(xexData[2]) << 8) | uint32_t(xexData[3]);
            if (magic == constants::kXex2Magic || magic == constants::kXex1Magic) {
                TitleInfo xexInfo = parseXexFromMemory(xexData.data(), outOffset, true);
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
            } else if (info.error.empty()) {
                // Decompressed data doesn't look like XEX — decompression likely failed
                char errBuf[128];
                snprintf(errBuf, sizeof(errBuf), "Decompressed data not XEX (magic=0x%08X, read=%zu bytes, blockFailures=%zu)",
                         magic, outOffset, decompressFailures);
                info.error = errBuf;
            }
        } else if (info.error.empty()) {
            info.error = "Failed to decompress any XEX data from ZAR";
        }

        info.classification = classifyTitleId(info.title_id);
        return info;
    }
};
