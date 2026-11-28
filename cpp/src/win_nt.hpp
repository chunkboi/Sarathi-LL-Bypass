#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace seb::nt {

inline BOOL NtClose(HANDLE h) noexcept {
    return CloseHandle(h);
}

struct NtHandle {
    HANDLE h = nullptr;
    NtHandle() = default;
    explicit NtHandle(HANDLE handle) noexcept : h(handle) {}
    ~NtHandle() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    NtHandle(const NtHandle&) = delete;
    NtHandle& operator=(const NtHandle&) = delete;
    NtHandle(NtHandle&& o) noexcept : h(std::exchange(o.h, nullptr)) {}
    NtHandle& operator=(NtHandle&& o) noexcept {
        if (this != &o) {
            if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
            h = std::exchange(o.h, nullptr);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return h; }
};

struct RegKeyGuard {
    HKEY h = nullptr;
    RegKeyGuard() = default;
    ~RegKeyGuard() {
        if (h) RegCloseKey(h);
    }
    RegKeyGuard(const RegKeyGuard&) = delete;
    RegKeyGuard& operator=(const RegKeyGuard&) = delete;
};

inline bool nt_is_dir_w(const wchar_t* path) noexcept {
    if (!path) return false;
    const DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline bool nt_is_file_w(const wchar_t* path) noexcept {
    if (!path) return false;
    const DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline HANDLE nt_open_read_file(const wchar_t* dos_path) noexcept {
    if (!dos_path) return nullptr;
    HANDLE h = CreateFileW(
        dos_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    return (h == INVALID_HANDLE_VALUE) ? nullptr : h;
}

inline uint64_t nt_file_size(HANDLE fh) {
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(fh, &li) || li.QuadPart < 0) {
        throw std::runtime_error("GetFileSizeEx failed");
    }
    return static_cast<uint64_t>(li.QuadPart);
}

inline bool nt_stat_w(const wchar_t* dos_path, uint64_t& mtime_ns, uint64_t& size) noexcept {
    if (!dos_path) return false;
    HANDLE h = CreateFileW(
        dos_path,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    NtHandle fh(h);

    LARGE_INTEGER li{};
    FILETIME ft_write{};
    if (!GetFileSizeEx(fh.get(), &li) || li.QuadPart < 0 ||
        !GetFileTime(fh.get(), nullptr, nullptr, &ft_write)) {
        return false;
    }
    const uint64_t ft = (static_cast<uint64_t>(ft_write.dwHighDateTime) << 32) | ft_write.dwLowDateTime;
    constexpr uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;
    mtime_ns = (ft >= EPOCH_DIFF_100NS) ? (ft - EPOCH_DIFF_100NS) * 100ULL : 0ULL;
    size = static_cast<uint64_t>(li.QuadPart);
    return true;
}

[[nodiscard]] inline std::vector<uint8_t> nt_read_file_bytes_w(
    const wchar_t* path,
    size_t max_bytes = 256ULL * 1024 * 1024
) {
    NtHandle fh(nt_open_read_file(path));
    if (!fh) throw std::runtime_error("CreateFileW failed");
    uint64_t sz = nt_file_size(fh.get());
    if (sz > max_bytes || sz > (std::numeric_limits<size_t>::max)()) {
        throw std::runtime_error("file exceeds maximum allowed read size");
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    size_t off = 0;
    while (off < buf.size()) {
        DWORD chunk = static_cast<DWORD>((std::min<size_t>)(buf.size() - off, 0x40000000ULL));
        DWORD got = 0;
        if (!ReadFile(fh.get(), buf.data() + off, chunk, &got, nullptr)) {
            throw std::runtime_error("ReadFile failed");
        }
        if (got == 0) {
            buf.resize(off);
            break;
        }
        off += got;
    }
    return buf;
}

[[nodiscard]] inline bool nt_write_file_bytes_w(const wchar_t* path, const void* data, size_t len) {
    if (!path || (len > 0 && !data)) return false;

    // Refuse to overwrite an existing reparse point (symlink / junction).
    const DWORD attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    std::wstring tmp_path(path);
    tmp_path.append(L".tmp.");
    tmp_path.append(std::to_wstring(GetCurrentProcessId()));

    HANDLE h = CreateFileW(
        tmp_path.c_str(),
        GENERIC_WRITE | DELETE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    {
        NtHandle fh(h);
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        size_t off = 0;
        while (off < len) {
            DWORD chunk = static_cast<DWORD>((std::min<size_t>)(len - off, 0x40000000ULL));
            DWORD written = 0;
            if (!WriteFile(fh.get(), ptr + off, chunk, &written, nullptr) || written == 0) {
                fh = NtHandle{};
                DeleteFileW(tmp_path.c_str());
                return false;
            }
            off += written;
        }
    }
    if (!MoveFileExW(tmp_path.c_str(), path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp_path.c_str());
        return false;
    }
    return true;
}

struct NtMappedFile {
    NtHandle file;
    NtHandle section;
    const uint8_t* view = nullptr;
    uint64_t size = 0;

    NtMappedFile() = default;
    ~NtMappedFile() {
        if (view) UnmapViewOfFile(view);
    }
    NtMappedFile(const NtMappedFile&) = delete;
    NtMappedFile& operator=(const NtMappedFile&) = delete;
    NtMappedFile(NtMappedFile&& o) noexcept
        : file(std::move(o.file)),
          section(std::move(o.section)),
          view(std::exchange(o.view, nullptr)),
          size(std::exchange(o.size, 0)) {}
    NtMappedFile& operator=(NtMappedFile&& o) noexcept {
        if (this != &o) {
            if (view) UnmapViewOfFile(view);
            file = std::move(o.file);
            section = std::move(o.section);
            view = std::exchange(o.view, nullptr);
            size = std::exchange(o.size, 0);
        }
        return *this;
    }

    static NtMappedFile open_handle(NtHandle&& fh, uint64_t sz) {
        NtMappedFile mf;
        mf.file = std::move(fh);
        mf.size = sz;
        if (sz == 0) return mf;
        if (sz > (std::numeric_limits<SIZE_T>::max)()) {
            throw std::runtime_error("file too large to map");
        }
        HANDLE sec = CreateFileMappingW(mf.file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!sec) throw std::runtime_error("CreateFileMappingW failed");
        mf.section = NtHandle(sec);

        const void* base = MapViewOfFile(mf.section.get(), FILE_MAP_READ, 0, 0, 0);
        if (!base) throw std::runtime_error("MapViewOfFile failed");
        mf.view = static_cast<const uint8_t*>(base);

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(mf.view, &mbi, sizeof(mbi)) == 0 || mbi.RegionSize < sz) {
            throw std::runtime_error("MapViewOfFile region smaller than expected file size");
        }
        return mf;
    }

    static NtMappedFile open(const wchar_t* path) {
        NtHandle fh(nt_open_read_file(path));
        if (!fh) throw std::runtime_error("CreateFileW failed");
        uint64_t sz = nt_file_size(fh.get());
        return open_handle(std::move(fh), sz);
    }
};

inline void nt_sleep_ms(DWORD ms) noexcept {
    Sleep(ms);
}

[[nodiscard]] inline bool nt_wait_all(const HANDLE* handles, ULONG count) noexcept {
    if (count > 0 && !handles) return false;
    ULONG off = 0;
    while (off < count) {
        ULONG batch = (count - off > MAXIMUM_WAIT_OBJECTS) ? MAXIMUM_WAIT_OBJECTS : (count - off);
        DWORD rc = WaitForMultipleObjects(batch, handles + off, TRUE, INFINITE);
        if (rc >= WAIT_OBJECT_0 && rc < WAIT_OBJECT_0 + batch) {
            off += batch;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool nt_reg_read_wstring(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* value_name,
    std::wstring& out
) {
    // RRF_RT_REG_SZ automatically expands REG_EXPAND_SZ unless RRF_NOEXPAND is set
    // (passing RRF_RT_REG_EXPAND_SZ without RRF_NOEXPAND fails with ERROR_INVALID_PARAMETER).
    DWORD bytes = 512;
    std::vector<wchar_t> buf(bytes / sizeof(wchar_t));
    const wchar_t* val = (value_name && value_name[0] != L'\0') ? value_name : nullptr;
    LSTATUS st = RegGetValueW(root, subkey, val, RRF_RT_REG_SZ, nullptr, buf.data(), &bytes);
    if (st == ERROR_MORE_DATA) {
        if (bytes == 0 || bytes > 1024UL * 1024UL) return false;
        buf.resize((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1);
        bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        st = RegGetValueW(root, subkey, val, RRF_RT_REG_SZ, nullptr, buf.data(), &bytes);
    }
    if (st != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return false;
    size_t wchars = bytes / sizeof(wchar_t);
    if (wchars > 0 && buf[wchars - 1] == L'\0') --wchars;
    out.assign(buf.data(), wchars);
    size_t nul = out.find(L'\0');
    if (nul != std::wstring::npos) out.resize(nul);
    return true;
}

[[nodiscard]] inline std::vector<std::wstring> nt_reg_enum_subkeys(HKEY root, const wchar_t* subkey) {
    std::vector<std::wstring> out;
    RegKeyGuard kg;
    if (RegOpenKeyExW(root, subkey, 0, KEY_ENUMERATE_SUB_KEYS, &kg.h) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t name_buf[256];
    for (DWORD idx = 0;; ++idx) {
        DWORD name_len = static_cast<DWORD>(std::size(name_buf));
        LSTATUS st = RegEnumKeyExW(kg.h, idx, name_buf, &name_len, nullptr, nullptr, nullptr, nullptr);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st == ERROR_SUCCESS) {
            out.emplace_back(name_buf, name_len);
        }
    }
    return out;
}

} // namespace seb::nt