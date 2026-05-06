// =============================================================================
// output.h — Output Formatters (Human, JSON, CSV)
// =============================================================================
#pragma once
#include "common.h"
#include "format_detect.h"

class OutputFormatter {
public:
    static void printHuman(const std::vector<TitleInfo>& results, bool verbose) {
        if (results.empty()) {
            printf("No files to process.\n");
            return;
        }

        size_t titleIdWidth = 10;
        size_t formatWidth = 6;
        size_t classWidth = 12;
        size_t nameWidth = 20;
        size_t versionWidth = 16;
        size_t fileWidth = 20;

        for (const auto& r : results) {
            formatWidth = std::max(formatWidth, r.format.size());
            classWidth = std::max(classWidth, r.classification.size());
            nameWidth = std::max(nameWidth, std::min(r.title_name.size(), (size_t)50));
            versionWidth = std::max(versionWidth, r.version_string.size());
            fileWidth = std::max(fileWidth, std::min(getFileName(r.file_path).size(), (size_t)40));
        }

        printf("%-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
               (int)titleIdWidth, "Title ID",
               (int)formatWidth, "Format",
               (int)classWidth, "Class",
               (int)nameWidth, "Name",
               (int)versionWidth, "Version",
               (int)fileWidth, "File");
        printf("%s  %s  %s  %s  %s  %s\n",
               std::string(titleIdWidth, '-').c_str(),
               std::string(formatWidth, '-').c_str(),
               std::string(classWidth, '-').c_str(),
               std::string(nameWidth, '-').c_str(),
               std::string(versionWidth, '-').c_str(),
               std::string(fileWidth, '-').c_str());

        for (const auto& r : results) {
            char titleIdBuf[16];
            if (r.title_id != 0) {
                snprintf(titleIdBuf, sizeof(titleIdBuf), "%08X", r.title_id);
            } else {
                snprintf(titleIdBuf, sizeof(titleIdBuf), "????");
            }

            std::string name = r.title_name;
            if (name.size() > nameWidth) name = name.substr(0, nameWidth - 3) + "...";
            std::string file = getFileName(r.file_path);
            if (file.size() > fileWidth) file = file.substr(0, fileWidth - 3) + "...";

            printf("%-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
                   (int)titleIdWidth, titleIdBuf,
                   (int)formatWidth, r.format.c_str(),
                   (int)classWidth, r.classification.c_str(),
                   (int)nameWidth, name.c_str(),
                   (int)versionWidth, r.version_string.c_str(),
                   (int)fileWidth, file.c_str());

            if (verbose) {
                if (r.format_version.size() > 0)  printf("  Format Version: %s\n", r.format_version.c_str());
                if (r.media_id != 0)              printf("  Media ID:       %08X\n", r.media_id);
                if (r.base_version != 0)          printf("  Base Version:   %08X\n", r.base_version);
                if (r.content_type != 0)          printf("  Content Type:   %s\n", r.content_type_str.c_str());
                if (r.disc_count > 1)             printf("  Disc:           %u/%u\n", r.disc_number, r.disc_count);
                if (r.savegame_id != 0)           printf("  Savegame ID:    %08X\n", r.savegame_id);
                if (!r.alternate_title_ids.empty()) {
                    printf("  Alt Title IDs:  ");
                    for (size_t j = 0; j < r.alternate_title_ids.size(); ++j) {
                        if (j > 0) printf(", ");
                        printf("%08X", r.alternate_title_ids[j]);
                    }
                    printf("\n");
                }
                if (r.parse_time_us > 0)          printf("  Parse Time:     %ld us\n", (long)r.parse_time_us);
                if (!r.zstd_available && r.format == "ZAR") printf("  ZSTD:           not available\n");
                if (!r.error.empty())             printf("  Error:          %s\n", r.error.c_str());
            }
        }

        printf("\n%zu file(s) processed\n", results.size());
    }

    static void printJson(const std::vector<TitleInfo>& results) {
        printf("[\n");
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            printf("  {\n");
            printf("    \"title_id\": \"%08X\",\n", r.title_id);
            printf("    \"title_id_decimal\": %u,\n", r.title_id);
            printf("    \"title_name\": \"");
            printJsonEscaped(r.title_name);
            printf("\",\n");
            printf("    \"format\": \"%s\",\n", r.format.c_str());
            printf("    \"format_version\": \"");
            printJsonEscaped(r.format_version);
            printf("\",\n");
            printf("    \"classification\": \"%s\",\n", r.classification.c_str());
            printf("    \"content_type\": \"%s\",\n", r.content_type_str.c_str());
            printf("    \"content_type_raw\": %u,\n", r.content_type);
            printf("    \"media_id\": \"%08X\",\n", r.media_id);
            printf("    \"version\": \"%s\",\n", r.version_string.c_str());
            printf("    \"version_raw\": %u,\n", r.version_value);
            printf("    \"base_version\": %u,\n", r.base_version);
            printf("    \"disc_number\": %u,\n", r.disc_number);
            printf("    \"disc_count\": %u,\n", r.disc_count);
            printf("    \"savegame_id\": \"%08X\",\n", r.savegame_id);
            printf("    \"zstd_available\": %s,\n", r.zstd_available ? "true" : "false");
            printf("    \"file_path\": \"");
            printJsonEscaped(r.file_path);
            printf("\",\n");
            printf("    \"parse_time_us\": %ld,\n", (long)r.parse_time_us);
            if (!r.alternate_title_ids.empty()) {
                printf("    \"alternate_title_ids\": [");
                for (size_t j = 0; j < r.alternate_title_ids.size(); ++j) {
                    if (j > 0) printf(", ");
                    printf("\"%08X\"", r.alternate_title_ids[j]);
                }
                printf("],\n");
            }
            if (!r.error.empty()) {
                printf("    \"error\": \"");
                printJsonEscaped(r.error);
                printf("\",\n");
            }
            printf("    \"publisher\": \"");
            printJsonEscaped(r.publisher);
            printf("\"\n");
            printf("  }%s\n", (i + 1 < results.size()) ? "," : "");
        }
        printf("]\n");
    }

    static void printCsv(const std::vector<TitleInfo>& results) {
        printf("title_id,title_id_decimal,title_name,format,format_version,classification,"
               "content_type,content_type_raw,media_id,version,version_raw,base_version,"
               "disc_number,disc_count,savegame_id,file_path,parse_time_us,error\n");
        for (const auto& r : results) {
            printf("\"%08X\",%u,\"", r.title_id, r.title_id);
            printCsvEscaped(r.title_name);
            printf("\",\"%s\",\"", r.format.c_str());
            printCsvEscaped(r.format_version);
            printf("\",\"%s\",\"%s\",%u,\"%08X\",\"%s\",%u,%u,%u,%u,\"%08X\",\"",
                   r.classification.c_str(), r.content_type_str.c_str(), r.content_type,
                   r.media_id, r.version_string.c_str(), r.version_value, r.base_version,
                   r.disc_number, r.disc_count, r.savegame_id);
            printCsvEscaped(r.file_path);
            printf("\",%ld,\"", (long)r.parse_time_us);
            printCsvEscaped(r.error);
            printf("\"\n");
        }
    }

private:
    static void printJsonEscaped(const std::string& s) {
        for (char c : s) {
            switch (c) {
                case '"': printf("\\\""); break;
                case '\\': printf("\\\\"); break;
                case '\b': printf("\\b"); break;
                case '\f': printf("\\f"); break;
                case '\n': printf("\\n"); break;
                case '\r': printf("\\r"); break;
                case '\t': printf("\\t"); break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        printf("\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    } else {
                        putchar(c);
                    }
                    break;
            }
        }
    }

    static void printCsvEscaped(const std::string& s) {
        for (char c : s) {
            if (c == '"') printf("\"\"");
            else putchar(c);
        }
    }
};
