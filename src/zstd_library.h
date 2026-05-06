// =============================================================================
// zstd_library.h — ZSTD Library Interface (static or dynamic)
// =============================================================================
// When X360TID_STATIC_ZSTD is defined, zstd is compiled directly into the
// executable — no DLL/SO needed at runtime. Otherwise, zstd is loaded
// dynamically at runtime via dlopen/LoadLibrary.
// =============================================================================
#pragma once
#include "common.h"

#ifdef X360TID_STATIC_ZSTD
// ---- Static linking: zstd is compiled into the binary ----
#include "../deps/zstd/zstd.h"

class ZstdLibrary {
public:
    ZstdLibrary() : available_(true) {}

    ~ZstdLibrary() = default;
    ZstdLibrary(const ZstdLibrary&) = delete;
    ZstdLibrary& operator=(const ZstdLibrary&) = delete;

    [[nodiscard]] bool available() const noexcept { return available_; }

    [[nodiscard]] size_t decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize) const noexcept {
        return ZSTD_decompress(dst, dstCapacity, src, srcSize);
    }

    [[nodiscard]] bool isError(size_t code) const noexcept {
        return ZSTD_isError(code) != 0;
    }

private:
    bool available_;
};

#else
// ---- Dynamic linking: load zstd at runtime ----

class ZstdLibrary {
public:
    using ZSTD_decompress_t = size_t(*)(void*, size_t, const void*, size_t);
    using ZSTD_getErrorName_t = const char*(*)(size_t);
    using ZSTD_isError_t = unsigned(*)(size_t);

    ZstdLibrary() {
#ifdef _WIN32
        lib_ = LoadLibraryA("zstd.dll");
#else
        lib_ = dlopen("libzstd.so.1", RTLD_LAZY);
        if (!lib_) lib_ = dlopen("libzstd.so", RTLD_LAZY);
        if (!lib_) lib_ = dlopen("libzstd.dylib", RTLD_LAZY);
#endif
        if (!lib_) return;

#ifdef _WIN32
        // GetProcAddress returns FARPROC; cast through void* to avoid GCC warning
        // (-Wcast-function-type). This is the standard pattern for Win32 dynamic loading.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        decompress_fn_ = reinterpret_cast<ZSTD_decompress_t>(GetProcAddress(static_cast<HMODULE>(lib_), "ZSTD_decompress"));
        isError_fn_ = reinterpret_cast<ZSTD_isError_t>(GetProcAddress(static_cast<HMODULE>(lib_), "ZSTD_isError"));
#pragma GCC diagnostic pop
#else
        decompress_fn_ = reinterpret_cast<ZSTD_decompress_t>(dlsym(lib_, "ZSTD_decompress"));
        isError_fn_ = reinterpret_cast<ZSTD_isError_t>(dlsym(lib_, "ZSTD_isError"));
#endif
        available_ = (decompress_fn_ != nullptr);
    }

    ~ZstdLibrary() {
        if (lib_) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(lib_));
#else
            dlclose(lib_);
#endif
        }
    }

    ZstdLibrary(const ZstdLibrary&) = delete;
    ZstdLibrary& operator=(const ZstdLibrary&) = delete;

    [[nodiscard]] bool available() const noexcept { return available_; }

    [[nodiscard]] size_t decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize) const noexcept {
        if (!decompress_fn_) return 0;
        return decompress_fn_(dst, dstCapacity, src, srcSize);
    }

    [[nodiscard]] bool isError(size_t code) const noexcept {
        if (!isError_fn_) return code != 0;
        return isError_fn_(code) != 0;
    }

private:
    void* lib_ = nullptr;
    ZSTD_decompress_t decompress_fn_ = nullptr;
    ZSTD_isError_t isError_fn_ = nullptr;
    bool available_ = false;
};

#endif // X360TID_STATIC_ZSTD
