#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seb::nt {

using NTSTATUS = LONG;

struct UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    UNICODE_STRING* ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
};

struct IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
};

struct FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
};

struct FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
};

struct FILE_NETWORK_OPEN_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
};

struct FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
};

struct KEY_VALUE_PARTIAL_INFORMATION {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
};

struct KEY_BASIC_INFORMATION {
    LARGE_INTEGER LastWriteTime;
    ULONG TitleIndex;
    ULONG NameLength;
    WCHAR Name[1];
};

inline constexpr ULONG OBJ_CASE_INSENSITIVE_FLAG = 0x00000040UL;
inline constexpr ULONG FILE_DIRECTORY_FILE_FLAG = 0x00000001UL;
inline constexpr ULONG FILE_SYNCHRONOUS_IO_NONALERT_FLAG = 0x00000020UL;
inline constexpr ULONG FILE_NON_DIRECTORY_FILE_FLAG = 0x00000040UL;
inline constexpr ULONG FILE_SEQUENTIAL_ONLY_FLAG = 0x00000004UL;
inline constexpr ULONG FILE_OPEN_FOR_BACKUP_INTENT_FLAG = 0x00004000UL;
inline constexpr ULONG FILE_OPEN_REPARSE_POINT_FLAG = 0x00200000UL;
inline constexpr ULONG FILE_OPEN_FLAG = 0x00000001UL;
inline constexpr ULONG FILE_OVERWRITE_IF_FLAG = 0x00000005UL;
inline constexpr ULONG FILE_SHARE_ALL = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

inline constexpr ULONG FileBasicInformationClass = 4;
inline constexpr ULONG FileStandardInformationClass = 5;
inline constexpr ULONG FileDirectoryInformationClass = 1;

// NtQueryDirectoryFile returns STATUS_BUFFER_OVERFLOW when the caller's buffer
// was filled but more directory entries remain. The buffered entries are still
// valid and must be processed before querying again.
inline constexpr NTSTATUS STATUS_BUFFER_OVERFLOW = static_cast<NTSTATUS>(0x80000005u);
inline constexpr NTSTATUS STATUS_NO_MORE_FILES = static_cast<NTSTATUS>(0x80000006u);
inline constexpr NTSTATUS STATUS_NO_MORE_ENTRIES = static_cast<NTSTATUS>(0x8000001Au);
inline constexpr NTSTATUS STATUS_BUFFER_TOO_SMALL = static_cast<NTSTATUS>(0xC0000023u);
inline constexpr NTSTATUS STATUS_END_OF_FILE = static_cast<NTSTATUS>(0xC0000011u);
inline constexpr ULONG KeyBasicInformationClass = 0;
inline constexpr ULONG KeyValuePartialInformationClass = 2;

extern "C" {
NTSTATUS NTAPI NtCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes,
    IO_STATUS_BLOCK* IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);

NTSTATUS NTAPI NtOpenFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes,
    IO_STATUS_BLOCK* IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions);

NTSTATUS NTAPI NtReadFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    IO_STATUS_BLOCK* IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);

NTSTATUS NTAPI NtWriteFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    IO_STATUS_BLOCK* IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);

NTSTATUS NTAPI NtQueryInformationFile(
    HANDLE FileHandle, IO_STATUS_BLOCK* IoStatusBlock, PVOID FileInformation,
    ULONG Length, ULONG FileInformationClass);

NTSTATUS NTAPI NtQueryAttributesFile(
    OBJECT_ATTRIBUTES* ObjectAttributes, FILE_BASIC_INFORMATION* FileInformation);

NTSTATUS NTAPI NtQueryDirectoryFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    IO_STATUS_BLOCK* IoStatusBlock, PVOID FileInformation, ULONG Length,
    ULONG FileInformationClass, BOOLEAN ReturnSingleEntry, UNICODE_STRING* FileName, BOOLEAN RestartScan);

NTSTATUS NTAPI NtCreateSection(
    PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes,
    PLARGE_INTEGER MaximumSize, ULONG SectionPageProtection, ULONG AllocationAttributes, HANDLE FileHandle);

NTSTATUS NTAPI NtMapViewOfSection(
    HANDLE SectionHandle, HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits,
    SIZE_T CommitSize, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, ULONG InheritDisposition,
    ULONG AllocationType, ULONG Win32Protect);

NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE ProcessHandle, PVOID BaseAddress);

NTSTATUS NTAPI NtOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes);

NTSTATUS NTAPI NtQueryValueKey(
    HANDLE KeyHandle, UNICODE_STRING* ValueName, ULONG KeyValueInformationClass,
    PVOID KeyValueInformation, ULONG Length, PULONG ResultLength);

NTSTATUS NTAPI NtEnumerateKey(
    HANDLE KeyHandle, ULONG Index, ULONG KeyInformationClass,
    PVOID KeyInformation, ULONG Length, PULONG ResultLength);

NTSTATUS NTAPI NtWaitForMultipleObjects(
    ULONG Count, HANDLE* Handles, ULONG WaitType, BOOLEAN Alertable, PLARGE_INTEGER Timeout);

NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);

NTSTATUS NTAPI NtClose(HANDLE Handle);

NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(
    PCWSTR DosFileName, UNICODE_STRING* NtFileName, PWSTR* FilePart, PVOID RelativeName);

VOID NTAPI RtlFreeUnicodeString(UNICODE_STRING* UnicodeString);

NTSTATUS NTAPI RtlFormatCurrentUserKeyPath(UNICODE_STRING* CurrentUserKeyPath);
}

inline void init_obj_attr(OBJECT_ATTRIBUTES& oa, UNICODE_STRING* name, HANDLE root = nullptr) {
    oa.Length = sizeof(OBJECT_ATTRIBUTES);
    oa.RootDirectory = root;
    oa.ObjectName = name;
    oa.Attributes = OBJ_CASE_INSENSITIVE_FLAG;
    oa.SecurityDescriptor = nullptr;
    oa.SecurityQualityOfService = nullptr;
}

inline void init_unicode_str(UNICODE_STRING& us, const wchar_t* str, size_t wchar_len) {
    if (!str && wchar_len > 0) throw std::runtime_error("null UNICODE_STRING buffer");
    if (wchar_len > 32766) throw std::runtime_error("UNICODE_STRING length overflow");
    us.Length = static_cast<USHORT>(wchar_len * sizeof(wchar_t));
    us.MaximumLength = static_cast<USHORT>((wchar_len + 1) * sizeof(wchar_t));
    us.Buffer = const_cast<PWSTR>(str ? str : L"");
}

struct NtUnicodePath {
    UNICODE_STRING us{};
    bool allocated = false;

    NtUnicodePath() = default;
    explicit NtUnicodePath(const wchar_t* dos_path) {
        if (RtlDosPathNameToNtPathName_U_WithStatus(dos_path, &us, nullptr, nullptr) == 0) {
            allocated = true;
        }
    }
    ~NtUnicodePath() {
        if (allocated) RtlFreeUnicodeString(&us);
    }
    NtUnicodePath(const NtUnicodePath&) = delete;
    NtUnicodePath& operator=(const NtUnicodePath&) = delete;
    explicit operator bool() const { return allocated && us.Buffer != nullptr; }
};

struct NtHandle {
    HANDLE h = nullptr;
    NtHandle() = default;
    explicit NtHandle(HANDLE handle) : h(handle) {}
    ~NtHandle() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) NtClose(h);
    }
    NtHandle(const NtHandle&) = delete;
    NtHandle& operator=(const NtHandle&) = delete;
    NtHandle(NtHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    NtHandle& operator=(NtHandle&& o) noexcept {
        if (this != &o) {
            if (h != nullptr && h != INVALID_HANDLE_VALUE) NtClose(h);
            h = o.h;
            o.h = nullptr;
        }
        return *this;
    }
    explicit operator bool() const { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return h; }
};

inline bool nt_query_attributes(const wchar_t* dos_path, FILE_BASIC_INFORMATION& fbi) {
    NtUnicodePath np(dos_path);
    if (!np) return false;
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &np.us);
    return NtQueryAttributesFile(&oa, &fbi) == 0;
}

inline bool nt_is_dir_w(const wchar_t* path) {
    FILE_BASIC_INFORMATION fbi{};
    return nt_query_attributes(path, fbi) && (fbi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline bool nt_is_file_w(const wchar_t* path) {
    FILE_BASIC_INFORMATION fbi{};
    return nt_query_attributes(path, fbi) && (fbi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline bool nt_stat_w(const wchar_t* dos_path, uint64_t& mtime_ns, uint64_t& size) {
    NtUnicodePath np(dos_path);
    if (!np) return false;
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &np.us);
    HANDLE h = nullptr;
    IO_STATUS_BLOCK iosb{};
    if (NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                   FILE_SHARE_ALL, FILE_SYNCHRONOUS_IO_NONALERT_FLAG) != 0) {
        return false;
    }
    NtHandle fh(h);
    FILE_BASIC_INFORMATION fbi{};
    FILE_STANDARD_INFORMATION fsi{};
    if (NtQueryInformationFile(fh.get(), &iosb, &fbi, sizeof(fbi), FileBasicInformationClass) != 0 ||
        NtQueryInformationFile(fh.get(), &iosb, &fsi, sizeof(fsi), FileStandardInformationClass) != 0) {
        return false;
    }
    uint64_t ft = static_cast<uint64_t>(fbi.LastWriteTime.QuadPart);
    constexpr uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;
    mtime_ns = (ft >= EPOCH_DIFF_100NS) ? (ft - EPOCH_DIFF_100NS) * 100ULL : 0ULL;
    size = static_cast<uint64_t>(fsi.EndOfFile.QuadPart);
    return true;
}

inline HANDLE nt_open_read_file(const wchar_t* dos_path) {
    NtUnicodePath np(dos_path);
    if (!np) return nullptr;
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &np.us);
    HANDLE h = nullptr;
    IO_STATUS_BLOCK iosb{};
    NTSTATUS st = NtOpenFile(
        &h, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
        FILE_SHARE_ALL, FILE_SYNCHRONOUS_IO_NONALERT_FLAG | FILE_SEQUENTIAL_ONLY_FLAG);
    return (st == 0) ? h : nullptr;
}

inline HANDLE nt_open_dir(const wchar_t* dos_path) {
    NtUnicodePath np(dos_path);
    if (!np) return nullptr;
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &np.us);
    HANDLE h = nullptr;
    IO_STATUS_BLOCK iosb{};
    NTSTATUS st = NtOpenFile(
        &h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
        FILE_SHARE_ALL, FILE_DIRECTORY_FILE_FLAG | FILE_SYNCHRONOUS_IO_NONALERT_FLAG);
    return (st == 0) ? h : nullptr;
}

inline uint64_t nt_file_size(HANDLE fh) {
    IO_STATUS_BLOCK iosb{};
    FILE_STANDARD_INFORMATION fsi{};
    if (NtQueryInformationFile(fh, &iosb, &fsi, sizeof(fsi), FileStandardInformationClass) != 0) {
        throw std::runtime_error("NtQueryInformationFile(FileStandardInformation) failed");
    }
    return static_cast<uint64_t>(fsi.EndOfFile.QuadPart);
}

[[nodiscard]] inline std::vector<uint8_t> nt_read_file_bytes_w(
    const wchar_t* path,
    size_t max_bytes = 256ULL * 1024 * 1024
) {
    NtHandle fh(nt_open_read_file(path));
    if (!fh) throw std::runtime_error("NtOpenFile failed");
    uint64_t sz = nt_file_size(fh.get());
    if (sz > max_bytes || sz > (std::numeric_limits<size_t>::max)()) {
        throw std::runtime_error("file exceeds maximum allowed read size");
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    size_t off = 0;
    while (off < buf.size()) {
        ULONG chunk = static_cast<ULONG>((buf.size() - off > 0x40000000ULL) ? 0x40000000ULL : (buf.size() - off));
        IO_STATUS_BLOCK iosb{};
        NTSTATUS st = NtReadFile(fh.get(), nullptr, nullptr, nullptr, &iosb, buf.data() + off, chunk, nullptr, nullptr);
        if (st == STATUS_END_OF_FILE || (st == 0 && iosb.Information == 0)) {
            buf.resize(off);
            break;
        }
        if (st != 0) {
            throw std::runtime_error("NtReadFile failed");
        }
        off += iosb.Information;
    }
    return buf;
}

[[nodiscard]] inline bool nt_write_file_bytes_w(const wchar_t* path, const void* data, size_t len) {
    if (!path || (len > 0 && !data)) return false;

    // Refuse to overwrite an existing reparse point (symlink / junction).
    FILE_BASIC_INFORMATION fbi{};
    if (nt_query_attributes(path, fbi) && (fbi.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    std::wstring tmp_path(path);
    tmp_path.append(L".tmp.");
    tmp_path.append(std::to_wstring(GetCurrentProcessId()));

    NtUnicodePath np(tmp_path.c_str());
    if (!np) return false;
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &np.us);
    HANDLE h = nullptr;
    IO_STATUS_BLOCK iosb{};
    NTSTATUS st = NtCreateFile(
        &h, FILE_WRITE_DATA | DELETE | SYNCHRONIZE, &oa, &iosb, nullptr,
        FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF_FLAG,
        FILE_SYNCHRONOUS_IO_NONALERT_FLAG | FILE_NON_DIRECTORY_FILE_FLAG | FILE_OPEN_REPARSE_POINT_FLAG,
        nullptr, 0);
    if (st != 0) return false;
    {
        NtHandle fh(h);
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        size_t off = 0;
        while (off < len) {
            ULONG chunk = static_cast<ULONG>((len - off > 0x40000000ULL) ? 0x40000000ULL : (len - off));
            IO_STATUS_BLOCK w_iosb{};
            if (NtWriteFile(fh.get(), nullptr, nullptr, nullptr, &w_iosb, const_cast<uint8_t*>(ptr + off), chunk, nullptr, nullptr) != 0 ||
                w_iosb.Information == 0) {
                fh = NtHandle{};
                DeleteFileW(tmp_path.c_str());
                return false;
            }
            off += w_iosb.Information;
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
        if (view) NtUnmapViewOfSection(reinterpret_cast<HANDLE>(-1), const_cast<uint8_t*>(view));
    }
    NtMappedFile(const NtMappedFile&) = delete;
    NtMappedFile& operator=(const NtMappedFile&) = delete;
    NtMappedFile(NtMappedFile&& o) noexcept
        : file(std::move(o.file)), section(std::move(o.section)), view(o.view), size(o.size) {
        o.view = nullptr;
        o.size = 0;
    }
    NtMappedFile& operator=(NtMappedFile&& o) noexcept {
        if (this != &o) {
            if (view) NtUnmapViewOfSection(reinterpret_cast<HANDLE>(-1), const_cast<uint8_t*>(view));
            file = std::move(o.file);
            section = std::move(o.section);
            view = o.view;
            size = o.size;
            o.view = nullptr;
            o.size = 0;
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
        HANDLE sec = nullptr;
        // NtCreateSection requires SEC_COMMIT for data file-backed sections
        // (CreateFileMappingW passes SEC_COMMIT internally). InheritDisposition=1
        // (ViewShare) is the standard disposition for read-only mappings.
        if (NtCreateSection(&sec, SECTION_MAP_READ, nullptr, nullptr, PAGE_READONLY, SEC_COMMIT, mf.file.get()) != 0) {
            throw std::runtime_error("NtCreateSection failed");
        }
        mf.section = NtHandle(sec);
        PVOID base = nullptr;
        SIZE_T view_sz = 0;
        if (NtMapViewOfSection(mf.section.get(), reinterpret_cast<HANDLE>(-1), &base, 0, 0, nullptr, &view_sz, 1 /*ViewShare*/, 0, PAGE_READONLY) != 0) {
            throw std::runtime_error("NtMapViewOfSection failed");
        }
        mf.view = static_cast<const uint8_t*>(base);
        if (view_sz < sz) {
            throw std::runtime_error("NtMapViewOfSection view smaller than expected file size");
        }
        return mf;
    }

    static NtMappedFile open(const wchar_t* path) {
        NtHandle fh(nt_open_read_file(path));
        if (!fh) throw std::runtime_error("NtOpenFile failed");
        uint64_t sz = nt_file_size(fh.get());
        return open_handle(std::move(fh), sz);
    }
};

inline void nt_sleep_ms(DWORD ms) {
    LARGE_INTEGER interval{};
    interval.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
    NtDelayExecution(FALSE, &interval);
}

[[nodiscard]] inline bool nt_wait_all(const HANDLE* handles, ULONG count) {
    ULONG off = 0;
    while (off < count) {
        ULONG batch = (count - off > MAXIMUM_WAIT_OBJECTS) ? MAXIMUM_WAIT_OBJECTS : (count - off);
        NTSTATUS st = NtWaitForMultipleObjects(batch, const_cast<HANDLE*>(handles + off), 0 /* WaitAll */, FALSE, nullptr);
        if (st != 0) return false;
        off += batch;
    }
    return true;
}

inline const std::wstring& hkcu_nt_prefix() {
    static std::wstring prefix = []() {
        UNICODE_STRING us{};
        if (RtlFormatCurrentUserKeyPath(&us) == 0 && us.Buffer) {
            std::wstring s(us.Buffer, us.Length / sizeof(wchar_t));
            RtlFreeUnicodeString(&us);
            return s;
        }
        return std::wstring(L"\\Registry\\User\\.Default");
    }();
    return prefix;
}

inline std::wstring hkey_to_nt_path(HKEY root, const wchar_t* subkey) {
    std::wstring base;
    if (root == HKEY_LOCAL_MACHINE) {
        base = L"\\Registry\\Machine";
    } else if (root == HKEY_CURRENT_USER) {
        base = hkcu_nt_prefix();
    } else if (root == HKEY_CLASSES_ROOT) {
        base = L"\\Registry\\Machine\\SOFTWARE\\Classes";
    } else {
        return {};
    }
    if (subkey && subkey[0] != L'\0') {
        base.push_back(L'\\');
        base.append(subkey);
    }
    return base;
}

[[nodiscard]] inline bool nt_reg_read_wstring(HKEY root, const wchar_t* subkey, const wchar_t* value_name, std::wstring& out) {
    auto try_open_and_read = [&](const std::wstring& nt_path) -> bool {
        if (nt_path.empty()) return false;
        UNICODE_STRING us_key{};
        init_unicode_str(us_key, nt_path.c_str(), nt_path.size());
        OBJECT_ATTRIBUTES oa{};
        init_obj_attr(oa, &us_key);
        HANDLE hKey = nullptr;
        if (NtOpenKey(&hKey, KEY_READ, &oa) != 0) return false;
        NtHandle kh(hKey);

        UNICODE_STRING us_val{};
        size_t vlen = value_name ? std::wcslen(value_name) : 0;
        init_unicode_str(us_val, value_name ? value_name : L"", vlen);

        alignas(8) uint8_t stack_buf[512];
        ULONG res_len = 0;
        NTSTATUS st = NtQueryValueKey(kh.get(), &us_val, KeyValuePartialInformationClass, stack_buf, sizeof(stack_buf), &res_len);
        std::vector<uint8_t> heap_buf;
        const uint8_t* raw_buf = stack_buf;
        ULONG raw_cap = sizeof(stack_buf);
        if (st == STATUS_BUFFER_TOO_SMALL || st == STATUS_BUFFER_OVERFLOW) {
            if (res_len < sizeof(KEY_VALUE_PARTIAL_INFORMATION) || res_len > 1024UL * 1024UL) return false;
            heap_buf.resize(res_len);
            raw_buf = heap_buf.data();
            raw_cap = res_len;
            st = NtQueryValueKey(kh.get(), &us_val, KeyValuePartialInformationClass, heap_buf.data(), res_len, &res_len);
        }
        constexpr size_t hdr_sz = offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data);
        if (st != 0 || res_len < hdr_sz || res_len > raw_cap) return false;
        KEY_VALUE_PARTIAL_INFORMATION kv{};
        std::memcpy(&kv, raw_buf, hdr_sz);
        if (kv.DataLength == 0 || (kv.Type != REG_SZ && kv.Type != REG_EXPAND_SZ)) return false;

        const ULONG avail = res_len - static_cast<ULONG>(hdr_sz);
        const ULONG data_bytes = (kv.DataLength < avail) ? kv.DataLength : avail;
        const size_t wchars = data_bytes / sizeof(wchar_t);
        out.resize(wchars);
        if (wchars > 0) {
            std::memcpy(out.data(), raw_buf + hdr_sz, wchars * sizeof(wchar_t));
        }
        size_t nul = out.find(L'\0');
        if (nul != std::wstring::npos) out.resize(nul);
        if (kv.Type == REG_EXPAND_SZ && !out.empty()) {
            DWORD exp_len = ExpandEnvironmentStringsW(out.c_str(), nullptr, 0);
            if (exp_len > 0 && exp_len <= 32768) {
                std::wstring expanded(exp_len, L'\0');
                DWORD written = ExpandEnvironmentStringsW(out.c_str(), expanded.data(), exp_len);
                if (written > 0 && written <= exp_len) {
                    expanded.resize(written);
                    if (!expanded.empty() && expanded.back() == L'\0') expanded.pop_back();
                    out = std::move(expanded);
                }
            }
        }
        return true;
    };

    if (root == HKEY_CLASSES_ROOT) {
        std::wstring user_cls = hkcu_nt_prefix() + L"_Classes";
        if (subkey && subkey[0] != L'\0') {
            user_cls.push_back(L'\\');
            user_cls.append(subkey);
        }
        if (try_open_and_read(user_cls)) return true;
    }
    return try_open_and_read(hkey_to_nt_path(root, subkey));
}

[[nodiscard]] inline std::vector<std::wstring> nt_reg_enum_subkeys(HKEY root, const wchar_t* subkey) {
    std::vector<std::wstring> out;
    std::wstring nt_path = hkey_to_nt_path(root, subkey);
    if (nt_path.empty()) return out;
    UNICODE_STRING us_key{};
    init_unicode_str(us_key, nt_path.c_str(), nt_path.size());
    OBJECT_ATTRIBUTES oa{};
    init_obj_attr(oa, &us_key);
    HANDLE hKey = nullptr;
    if (NtOpenKey(&hKey, KEY_READ, &oa) != 0) return out;
    NtHandle kh(hKey);

    std::vector<uint8_t> buf(1024);
    constexpr size_t hdr_sz = offsetof(KEY_BASIC_INFORMATION, Name);
    for (ULONG idx = 0;; ++idx) {
        ULONG res_len = 0;
        NTSTATUS st = NtEnumerateKey(kh.get(), idx, KeyBasicInformationClass, buf.data(), static_cast<ULONG>(buf.size()), &res_len);
        if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL) {
            if (res_len < hdr_sz || res_len > 1024UL * 1024UL) break;
            buf.resize(res_len);
            st = NtEnumerateKey(kh.get(), idx, KeyBasicInformationClass, buf.data(), static_cast<ULONG>(buf.size()), &res_len);
        }
        if (st == STATUS_NO_MORE_ENTRIES || st == STATUS_NO_MORE_FILES || st != 0) break;
        if (res_len < hdr_sz || res_len > buf.size()) break;
        KEY_BASIC_INFORMATION kbi{};
        std::memcpy(&kbi, buf.data(), hdr_sz);
        if (kbi.NameLength > res_len - hdr_sz) continue;
        size_t wchars = kbi.NameLength / sizeof(wchar_t);
        std::wstring name(wchars, L'\0');
        if (wchars > 0) {
            std::memcpy(name.data(), buf.data() + hdr_sz, wchars * sizeof(wchar_t));
        }
        out.push_back(std::move(name));
    }
    return out;
}

} // namespace seb::nt
