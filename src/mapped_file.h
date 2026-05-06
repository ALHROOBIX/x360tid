// =============================================================================
// mapped_file.h — Memory-Mapped File
// =============================================================================
#pragma once
#include "common.h"

class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile() { close(); }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] bool open(const char* path) noexcept {
        close();
#ifdef _WIN32
        handle_ = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(handle_, &fileSize)) { close(); return false; }
        size_ = static_cast<size_t>(fileSize.QuadPart);
        if (size_ > constants::kMaxFileSize) { close(); return false; }
        mapping_ = CreateFileMappingA(handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) { close(); return false; }
        data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) { close(); return false; }
#else
        fd_ = ::open(path, O_RDONLY);
        if (fd_ < 0) return false;
        struct stat st;
        if (fstat(fd_, &st) != 0) { close(); return false; }
        size_ = static_cast<size_t>(st.st_size);
        if (size_ > constants::kMaxFileSize) { close(); return false; }
        if (size_ == 0) { data_ = nullptr; return true; }
        data_ = static_cast<const uint8_t*>(mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
        if (data_ == MAP_FAILED) { data_ = nullptr; close(); return false; }
        madvise(const_cast<uint8_t*>(data_), size_, MADV_SEQUENTIAL);
#endif
        return true;
    }

    void close() noexcept {
        if (data_) {
#ifdef _WIN32
            UnmapViewOfFile(data_);
#else
            munmap(const_cast<uint8_t*>(data_), size_);
#endif
            data_ = nullptr;
        }
#ifdef _WIN32
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
        size_ = 0;
    }

    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { return data_ != nullptr; }

    [[nodiscard]] bool readBytes(size_t offset, size_t len, void* out) const noexcept {
        if (!data_ || offset + len > size_ || offset + len < offset) return false;
        std::memcpy(out, data_ + offset, len);
        return true;
    }

    [[nodiscard]] bool checkBounds(size_t offset, size_t len) const noexcept {
        if (!data_) return false;
        uint64_t end;
        if (!safeAdd<uint64_t>(static_cast<uint64_t>(offset), static_cast<uint64_t>(len), end)) return false;
        return end <= size_;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};
