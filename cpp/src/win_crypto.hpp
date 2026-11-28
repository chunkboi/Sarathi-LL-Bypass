#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "win_nt.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef BCRYPT_HASH_REUSABLE_FLAG
#define BCRYPT_HASH_REUSABLE_FLAG 0x00000020
#endif

namespace seb::win {

using WinHandle = nt::NtHandle;
using MappedFile = nt::NtMappedFile;

inline void wipe_bytes(void* ptr, size_t len) noexcept {
    if (ptr && len > 0) {
        SecureZeroMemory(ptr, len);
    }
}

inline void wipe_string(std::string& s) noexcept {
    if (s.capacity() > 0) {
        s.resize(s.capacity(), '\0');
        SecureZeroMemory(s.data(), s.size());
    }
    s.clear();
}

inline void wipe_vector(std::vector<uint8_t>& v) noexcept {
    if (v.capacity() > 0) {
        v.resize(v.capacity(), 0);
        SecureZeroMemory(v.data(), v.size());
    }
    v.clear();
}

struct SecureZeroGuard {
    void* ptr = nullptr;
    size_t len = 0;

    SecureZeroGuard() = default;
    SecureZeroGuard(void* p, size_t n) noexcept : ptr(p), len(n) {}
    ~SecureZeroGuard() { wipe_bytes(ptr, len); }
    SecureZeroGuard(const SecureZeroGuard&) = delete;
    SecureZeroGuard& operator=(const SecureZeroGuard&) = delete;
};

struct SecureStringGuard {
    std::string& ref;
    explicit SecureStringGuard(std::string& s) noexcept : ref(s) {}
    ~SecureStringGuard() { wipe_string(ref); }
    SecureStringGuard(const SecureStringGuard&) = delete;
    SecureStringGuard& operator=(const SecureStringGuard&) = delete;
};

struct SecureVectorGuard {
    std::vector<uint8_t>& ref;
    explicit SecureVectorGuard(std::vector<uint8_t>& v) noexcept : ref(v) {}
    ~SecureVectorGuard() { wipe_vector(ref); }
    SecureVectorGuard(const SecureVectorGuard&) = delete;
    SecureVectorGuard& operator=(const SecureVectorGuard&) = delete;
};

// Page-isolated RAM-pinned secret buffer. Using dedicated VirtualAlloc pages
// ensures VirtualUnlock never prematurely unlocks unrelated stack/heap secrets.
struct LockedBuffer {
    uint8_t* ptr = nullptr;
    size_t size_ = 0;
    size_t alloc_size_ = 0;
    bool locked_ = false;

    LockedBuffer() = default;
    explicit LockedBuffer(size_t n) : size_(n) {
        if (n > 0) {
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            size_t page_sz = si.dwPageSize ? si.dwPageSize : 4096;
            alloc_size_ = ((n + page_sz - 1) / page_sz) * page_sz;
            ptr = static_cast<uint8_t*>(VirtualAlloc(nullptr, alloc_size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!ptr) throw std::bad_alloc();
            locked_ = (VirtualLock(ptr, alloc_size_) != 0);
        }
    }
    ~LockedBuffer() { reset(); }
    void reset() noexcept {
        if (ptr) {
            SecureZeroMemory(ptr, alloc_size_);
            if (locked_) VirtualUnlock(ptr, alloc_size_);
            VirtualFree(ptr, 0, MEM_RELEASE);
            ptr = nullptr;
            size_ = 0;
            alloc_size_ = 0;
            locked_ = false;
        }
    }
    LockedBuffer(const LockedBuffer&) = delete;
    LockedBuffer& operator=(const LockedBuffer&) = delete;
    LockedBuffer(LockedBuffer&& o) noexcept
        : ptr(std::exchange(o.ptr, nullptr)),
          size_(std::exchange(o.size_, 0)),
          alloc_size_(std::exchange(o.alloc_size_, 0)),
          locked_(std::exchange(o.locked_, false)) {}
    LockedBuffer& operator=(LockedBuffer&& o) noexcept {
        if (this != &o) {
            reset();
            ptr = std::exchange(o.ptr, nullptr);
            size_ = std::exchange(o.size_, 0);
            alloc_size_ = std::exchange(o.alloc_size_, 0);
            locked_ = std::exchange(o.locked_, false);
        }
        return *this;
    }

    uint8_t* data() noexcept { return ptr; }
    const uint8_t* data() const noexcept { return ptr; }
    size_t size() const noexcept { return size_; }
};

// Pins a memory range in RAM and securely wipes it on scope exit.
// Note: VirtualUnlock is intentionally NOT called on arbitrary shared stack/heap
// pages because Windows VirtualLock/VirtualUnlock are page-granular and not
// reference-counted; unlocking a shared page could unlock another active secret.
struct MemoryLockGuard {
    void* ptr = nullptr;
    size_t len = 0;

    MemoryLockGuard(void* p, size_t n) noexcept : ptr(p), len(n) {
        if (ptr && len > 0) {
            (void)VirtualLock(ptr, len);
        }
    }
    ~MemoryLockGuard() {
        wipe_bytes(ptr, len);
    }
    MemoryLockGuard(const MemoryLockGuard&) = delete;
    MemoryLockGuard& operator=(const MemoryLockGuard&) = delete;
};

[[nodiscard]] inline std::vector<uint8_t> read_file_bytes_w(const wchar_t* path) {
    return nt::nt_read_file_bytes_w(path);
}

[[nodiscard]] inline bool write_file_bytes_w(const wchar_t* path, const void* data, size_t len) {
    return nt::nt_write_file_bytes_w(path, data, len);
}

[[nodiscard]] inline std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    if (s.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("utf8_to_wide input too large");
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) throw std::runtime_error("MultiByteToWideChar size failed");
    std::wstring out(static_cast<size_t>(len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len) <= 0) {
        throw std::runtime_error("MultiByteToWideChar convert failed");
    }
    return out;
}

[[nodiscard]] inline std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    if (w.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("wide_to_utf8 input too large");
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) throw std::runtime_error("WideCharToMultiByte size failed");
    std::string out(static_cast<size_t>(len), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr) <= 0) {
        throw std::runtime_error("WideCharToMultiByte convert failed");
    }
    return out;
}

inline constexpr std::array<uint16_t, 256> make_hex_lut() {
    std::array<uint16_t, 256> lut{};
    constexpr char H[] = "0123456789abcdef";
    for (size_t i = 0; i < 256; ++i) {
        uint16_t lo = static_cast<uint8_t>(H[i >> 4]);
        uint16_t hi = static_cast<uint8_t>(H[i & 0x0F]);
        lut[i] = static_cast<uint16_t>(lo | (hi << 8));
    }
    return lut;
}

inline constexpr std::array<uint16_t, 256> HEX_LUT = make_hex_lut();

inline void hex_lower_into(const uint8_t* data, size_t len, char* dst) {
    // Write two bytes per input byte. The explicit shifts avoid the
    // unaligned/aliasing uint16_t stores the previous implementation used.
    for (size_t i = 0; i < len; ++i) {
        const uint16_t v = HEX_LUT[data[i]];
        dst[i * 2] = static_cast<char>(v & 0xFF);
        dst[i * 2 + 1] = static_cast<char>(v >> 8);
    }
}

inline std::string hex_lower(const uint8_t* data, size_t len) {
    std::string out(len * 2, '\0');
    hex_lower_into(data, len, out.data());
    return out;
}

inline std::string hex_lower(const std::vector<uint8_t>& v) {
    return hex_lower(v.data(), v.size());
}

namespace detail {
struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    DWORD obj_len = 0;
    AlgHandle(LPCWSTR alg_id, ULONG flags = 0) {
        if (BCryptOpenAlgorithmProvider(&h, alg_id, nullptr, flags) != 0) {
            h = nullptr;
            return;
        }
        DWORD res = 0;
        if (BCryptGetProperty(h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len), sizeof(obj_len), &res, 0) != 0) {
            obj_len = 512;
        }
    }
    ~AlgHandle() {
        if (h) BCryptCloseAlgorithmProvider(h, 0);
    }
    AlgHandle(const AlgHandle&) = delete;
    AlgHandle& operator=(const AlgHandle&) = delete;
};

inline const AlgHandle& sha256_alg_info() {
    static AlgHandle a(BCRYPT_SHA256_ALGORITHM, BCRYPT_HASH_REUSABLE_FLAG);
    if (!a.h) throw std::runtime_error("BCryptOpenAlgorithmProvider SHA256 failed");
    return a;
}

inline BCRYPT_ALG_HANDLE sha256_alg() {
    return sha256_alg_info().h;
}

struct ReusableSha256 {
    std::vector<uint8_t> obj;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    ReusableSha256() {
        const auto& info = sha256_alg_info();
        obj.resize(info.obj_len);
        if (BCryptCreateHash(info.h, &hHash, obj.data(), static_cast<ULONG>(obj.size()), nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG) != 0) {
            hHash = nullptr;
        }
    }
    ~ReusableSha256() {
        if (hHash) BCryptDestroyHash(hHash);
    }
};

inline BCRYPT_HASH_HANDLE thread_sha256_handle() {
    thread_local ReusableSha256 tls;
    if (!tls.hHash) throw std::runtime_error("BCryptCreateHash reusable SHA256 failed");
    return tls.hHash;
}

inline BCRYPT_ALG_HANDLE hmac_sha256_alg() {
    static AlgHandle a(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!a.h) throw std::runtime_error("BCryptOpenAlgorithmProvider HMAC-SHA256 failed");
    return a.h;
}

inline BCRYPT_ALG_HANDLE hmac_sha1_alg() {
    static AlgHandle a(BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!a.h) throw std::runtime_error("BCryptOpenAlgorithmProvider HMAC-SHA1 failed");
    return a.h;
}

inline BCRYPT_ALG_HANDLE sha1_alg() {
    static AlgHandle a(BCRYPT_SHA1_ALGORITHM, 0);
    if (!a.h) throw std::runtime_error("BCryptOpenAlgorithmProvider SHA1 failed");
    return a.h;
}

struct BCryptHashGuard {
    BCRYPT_HASH_HANDLE h = nullptr;
    BCryptHashGuard() = default;
    ~BCryptHashGuard() {
        if (h) BCryptDestroyHash(h);
    }
    BCryptHashGuard(const BCryptHashGuard&) = delete;
    BCryptHashGuard& operator=(const BCryptHashGuard&) = delete;
};

struct BCryptKeyGuard {
    BCRYPT_KEY_HANDLE h = nullptr;
    BCryptKeyGuard() = default;
    ~BCryptKeyGuard() {
        if (h) BCryptDestroyKey(h);
    }
    BCryptKeyGuard(const BCryptKeyGuard&) = delete;
    BCryptKeyGuard& operator=(const BCryptKeyGuard&) = delete;
};

inline BCRYPT_ALG_HANDLE aes_cbc_alg() {
    static struct AesInit {
        BCRYPT_ALG_HANDLE h = nullptr;
        AesInit() {
            if (BCryptOpenAlgorithmProvider(&h, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0) {
                if (BCryptSetProperty(
                        h, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t)),
                        0) != 0) {
                    BCryptCloseAlgorithmProvider(h, 0);
                    h = nullptr;
                }
            }
        }
        ~AesInit() {
            if (h) BCryptCloseAlgorithmProvider(h, 0);
        }
    } a;
    if (!a.h) throw std::runtime_error("BCryptOpenAlgorithmProvider AES-CBC failed");
    return a.h;
}
} // namespace detail

[[nodiscard]] inline std::array<uint8_t, 20> sha1_raw(const uint8_t* data, size_t len) {
    detail::BCryptHashGuard hg;
    if (BCryptCreateHash(detail::sha1_alg(), &hg.h, nullptr, 0, nullptr, 0, 0) != 0) {
        throw std::runtime_error("BCryptCreateHash SHA1 failed");
    }
    std::array<uint8_t, 20> out{};
    NTSTATUS st = 0;
    uint8_t dummy = 0;
    if (len == 0) {
        st = BCryptHashData(hg.h, &dummy, 0, 0);
    } else {
        if (!data) throw std::runtime_error("null SHA1 input");
        size_t off = 0;
        while (off < len) {
            ULONG chunk = static_cast<ULONG>((std::min<size_t>)(len - off, 0x40000000ULL));
            st = BCryptHashData(hg.h, const_cast<PUCHAR>(data + off), chunk, 0);
            if (st != 0) break;
            off += chunk;
        }
    }
    if (st == 0) st = BCryptFinishHash(hg.h, out.data(), 20, 0);
    if (st != 0) throw std::runtime_error("BCryptHash SHA1 failed");
    return out;
}

[[nodiscard]] inline std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    BCRYPT_HASH_HANDLE hHash = detail::thread_sha256_handle();
    std::array<uint8_t, 32> out{};
    NTSTATUS st = 0;
    if (len > 0) {
        if (!data) throw std::runtime_error("null SHA256 input");
        size_t off = 0;
        while (off < len) {
            ULONG chunk = static_cast<ULONG>((std::min<size_t>)(len - off, 0x40000000ULL));
            st = BCryptHashData(hHash, const_cast<PUCHAR>(data + off), chunk, 0);
            if (st != 0) break;
            off += chunk;
        }
    }
    NTSTATUS fst = BCryptFinishHash(hHash, out.data(), 32, 0);
    if (st != 0 || fst != 0) throw std::runtime_error("BCryptHash SHA256 failed");
    return out;
}

[[nodiscard]] inline std::string sha256_hex(const uint8_t* data, size_t len) {
    auto digest = sha256_raw(data, len);
    return hex_lower(digest.data(), 32);
}

[[nodiscard]] inline std::string sha256_hex(std::string_view s) {
    uint8_t dummy = 0;
    const uint8_t* ptr = s.empty() ? &dummy : reinterpret_cast<const uint8_t*>(s.data());
    return sha256_hex(ptr, s.size());
}

inline std::string sha256_file_hex_w(const wchar_t* path, uint64_t known_size = UINT64_MAX, bool* is_pe = nullptr) {
    if (is_pe) *is_pe = false;
    if (known_size == 0) {
        return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    }
    nt::NtHandle fh(nt::nt_open_read_file(path));
    if (!fh) return "";

    if (known_size == UINT64_MAX) {
        nt::FILE_STANDARD_INFORMATION fsi{};
        nt::IO_STATUS_BLOCK iosb{};
        if (nt::NtQueryInformationFile(fh.get(), &iosb, &fsi, sizeof(fsi), nt::FileStandardInformationClass) != 0) {
            return "";
        }
        known_size = static_cast<uint64_t>(fsi.EndOfFile.QuadPart);
        if (known_size == 0) {
            return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        }
    }

    BCRYPT_HASH_HANDLE hHash = detail::thread_sha256_handle();
    NTSTATUS st = 0;
    if (known_size <= 262144ULL) {
        thread_local uint8_t buf[65536];
        nt::IO_STATUS_BLOCK iosb{};
        bool first_read = true;
        while (true) {
            const NTSTATUS read_st =
                nt::NtReadFile(fh.get(), nullptr, nullptr, nullptr, &iosb, buf, sizeof(buf), nullptr, nullptr);
            if (read_st != 0) {
                st = read_st;
                break;
            }
            if (iosb.Information == 0) break;  // EOF
            if (first_read) {
                first_read = false;
                if (is_pe && iosb.Information >= 2) {
                    *is_pe = (buf[0] == 'M' && buf[1] == 'Z');
                }
            }
            st = BCryptHashData(hHash, buf, static_cast<ULONG>(iosb.Information), 0);
            if (st != 0) break;
            // A short read means EOF; avoid the extra zero-length syscall.
            if (iosb.Information < sizeof(buf)) break;
        }
    } else {
        try {
            nt::NtMappedFile mf = nt::NtMappedFile::open_handle(std::move(fh), known_size);
            if (is_pe && known_size >= 2) {
                *is_pe = (mf.view[0] == 'M' && mf.view[1] == 'Z');
            }
            size_t off = 0;
            size_t total = static_cast<size_t>(known_size);
            while (off < total) {
                ULONG chunk = static_cast<ULONG>((std::min<size_t>)(total - off, 0x40000000ULL));
                st = BCryptHashData(hHash, const_cast<PUCHAR>(mf.view + off), chunk, 0);
                if (st != 0) break;
                off += chunk;
            }
        } catch (...) {
            st = -1;
        }
    }

    std::array<uint8_t, 32> out{};
    NTSTATUS fst = BCryptFinishHash(hHash, out.data(), 32, 0);
    if (st != 0 || fst != 0) return "";
    return hex_lower(out.data(), 32);
}

[[nodiscard]] inline std::array<uint8_t, 32> hmac_sha256_raw(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* data,
    size_t len
) {
    if (key_len > (std::numeric_limits<ULONG>::max)()) {
        throw std::runtime_error("HMAC key too large");
    }
    uint8_t dummy = 0;
    const uint8_t* key_ptr = (key_len == 0) ? &dummy : key;
    if (key_len > 0 && !key_ptr) throw std::runtime_error("null HMAC key");

    detail::BCryptHashGuard hg;
    if (BCryptCreateHash(
            detail::hmac_sha256_alg(), &hg.h, nullptr, 0,
            const_cast<PUCHAR>(key_ptr), static_cast<ULONG>(key_len), 0) != 0) {
        throw std::runtime_error("BCryptCreateHash HMAC-SHA256 failed");
    }
    std::array<uint8_t, 32> out{};
    NTSTATUS st = 0;
    if (len == 0) {
        st = BCryptHashData(hg.h, &dummy, 0, 0);
    } else {
        if (!data) throw std::runtime_error("null HMAC data");
        size_t off = 0;
        while (off < len) {
            ULONG chunk = static_cast<ULONG>((std::min<size_t>)(len - off, 0x40000000ULL));
            st = BCryptHashData(hg.h, const_cast<PUCHAR>(data + off), chunk, 0);
            if (st != 0) break;
            off += chunk;
        }
    }
    if (st == 0) st = BCryptFinishHash(hg.h, out.data(), 32, 0);
    if (st != 0) throw std::runtime_error("BCryptHash HMAC-SHA256 failed");
    return out;
}

[[nodiscard]] inline std::array<uint8_t, 32> pbkdf2_hmac_sha1_32(
    std::string_view password,
    const uint8_t* salt,
    size_t salt_len,
    uint64_t iterations
) {
    if (password.size() > (std::numeric_limits<ULONG>::max)() ||
        salt_len > (std::numeric_limits<ULONG>::max)()) {
        throw std::runtime_error("PBKDF2 input too large");
    }
    uint8_t dummy = 0;
    PUCHAR pw_ptr = password.empty()
        ? &dummy
        : reinterpret_cast<PUCHAR>(const_cast<char*>(password.data()));
    PUCHAR salt_ptr = (salt_len == 0) ? &dummy : const_cast<PUCHAR>(salt);
    if (salt_len > 0 && !salt_ptr) throw std::runtime_error("null PBKDF2 salt");

    std::array<uint8_t, 32> out{};
    NTSTATUS st = BCryptDeriveKeyPBKDF2(
        detail::hmac_sha1_alg(),
        pw_ptr,
        static_cast<ULONG>(password.size()),
        salt_ptr,
        static_cast<ULONG>(salt_len),
        iterations,
        out.data(),
        32,
        0);
    if (st != 0) throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    return out;
}

[[nodiscard]] inline std::vector<uint8_t> aes256_cbc_decrypt(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* iv,
    const uint8_t* ciphertext,
    size_t ciphertext_len
) {
    if (!key || !iv || !ciphertext || key_len != 32 || ciphertext_len == 0 ||
        (ciphertext_len % 16) != 0 || ciphertext_len > (std::numeric_limits<ULONG>::max)()) {
        throw std::runtime_error("invalid AES-256-CBC parameters");
    }
    detail::BCryptKeyGuard kg;
    if (BCryptGenerateSymmetricKey(
            detail::aes_cbc_alg(), &kg.h, nullptr, 0,
            const_cast<PUCHAR>(key), static_cast<ULONG>(key_len), 0) != 0) {
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");
    }

    uint8_t iv_buf[16];
    SecureZeroGuard iv_guard(iv_buf, sizeof(iv_buf));
    std::memcpy(iv_buf, iv, 16);

    std::vector<uint8_t> out(ciphertext_len);
    ULONG res_len = 0;
    NTSTATUS st = BCryptDecrypt(
        kg.h,
        const_cast<PUCHAR>(ciphertext),
        static_cast<ULONG>(ciphertext_len),
        nullptr,
        iv_buf,
        16,
        out.data(),
        static_cast<ULONG>(out.size()),
        &res_len,
        0);
    if (st != 0) {
        wipe_vector(out);
        throw std::runtime_error("BCryptDecrypt failed");
    }

    if (res_len == 0 || res_len > out.size()) {
        wipe_vector(out);
        throw std::runtime_error("empty or invalid decrypted data");
    }
    out.resize(res_len);
    uint8_t pad = out.back();
    if (pad == 0 || pad > 16 || pad > out.size()) {
        wipe_vector(out);
        throw std::runtime_error("invalid PKCS7 padding");
    }
    for (size_t i = out.size() - pad; i < out.size(); ++i) {
        if (out[i] != pad) {
            wipe_vector(out);
            throw std::runtime_error("invalid PKCS7 padding bytes");
        }
    }
    const size_t pt_len = out.size() - pad;
    wipe_bytes(out.data() + pt_len, pad);
    out.resize(pt_len);
    return out;
}

inline int compare_invariant(std::string_view left, std::string_view right) {
    wchar_t wbuf_l[128], wbuf_r[128];
    std::wstring heap_l, heap_r;
    const wchar_t* pl = nullptr;
    const wchar_t* pr = nullptr;
    int ll = 0, lr = 0;

    if (!left.empty()) {
        if (left.size() < 128) {
            ll = MultiByteToWideChar(CP_UTF8, 0, left.data(), static_cast<int>(left.size()), wbuf_l, 128);
            if (ll > 0) pl = wbuf_l;
        }
        if (!pl) {
            heap_l = utf8_to_wide(left);
            pl = heap_l.data();
            ll = static_cast<int>(heap_l.size());
        }
    } else {
        pl = L"";
    }

    if (!right.empty()) {
        if (right.size() < 128) {
            lr = MultiByteToWideChar(CP_UTF8, 0, right.data(), static_cast<int>(right.size()), wbuf_r, 128);
            if (lr > 0) pr = wbuf_r;
        }
        if (!pr) {
            heap_r = utf8_to_wide(right);
            pr = heap_r.data();
            lr = static_cast<int>(heap_r.size());
        }
    } else {
        pr = L"";
    }

    int res = CompareStringEx(L"", 0, pl, ll, pr, lr, nullptr, nullptr, 0);
    if (res != 0) return res - 2;
    size_t min_len = (std::min)(left.size(), right.size());
    for (size_t i = 0; i < min_len; ++i) {
        unsigned char a = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(left[i])));
        unsigned char b = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(right[i])));
        if (a != b) return (a > b) ? 1 : -1;
    }
    return (left.size() > right.size()) - (left.size() < right.size());
}

inline bool reg_read_wstring(HKEY root, const wchar_t* subkey, const wchar_t* value_name, std::wstring& out) {
    return nt::nt_reg_read_wstring(root, subkey, value_name, out);
}

inline std::vector<std::wstring> reg_enum_subkeys(HKEY root, const wchar_t* subkey) {
    return nt::nt_reg_enum_subkeys(root, subkey);
}

[[nodiscard]] inline std::pair<std::wstring, std::string> file_metadata_w(const wchar_t* path, bool known_pe = false) {
    // Callers that already inspected the file while hashing can pass
    // known_pe=true to avoid a second open/read of the MZ header.
    if (!known_pe) {
        nt::NtHandle fh(nt::nt_open_read_file(path));
        if (!fh) return {L"", ""};
        char mz[2]{};
        nt::IO_STATUS_BLOCK iosb{};
        if (nt::NtReadFile(fh.get(), nullptr, nullptr, nullptr, &iosb, mz, 2, nullptr, nullptr) != 0 ||
            iosb.Information != 2 || mz[0] != 'M' || mz[1] != 'Z') {
            return {L"", ""};
        }
    }

    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return {L"", ""};

    thread_local std::vector<uint8_t> buf;
    if (buf.size() < size) buf.resize(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data())) return {L"", ""};

    LPVOID ptr = nullptr;
    UINT len = 0;
    std::string ver_str;
    if (VerQueryValueW(buf.data(), L"\\", &ptr, &len) && ptr && len >= sizeof(VS_FIXEDFILEINFO)) {
        VS_FIXEDFILEINFO info{};
        std::memcpy(&info, ptr, sizeof(info));
        const unsigned parts[4] = {
            static_cast<unsigned>(info.dwFileVersionMS >> 16),
            static_cast<unsigned>(info.dwFileVersionMS & 0xFFFF),
            static_cast<unsigned>(info.dwFileVersionLS >> 16),
            static_cast<unsigned>(info.dwFileVersionLS & 0xFFFF),
        };
        char vbuf[64];
        char* cur = vbuf;
        char* end = vbuf + sizeof(vbuf);
        for (size_t idx = 0; idx < 4; ++idx) {
            if (idx > 0 && cur < end) *cur++ = '.';
            auto [p, ec] = std::to_chars(cur, end, parts[idx]);
            if (ec != std::errc{}) break;
            cur = p;
        }
        ver_str.assign(vbuf, static_cast<size_t>(cur - vbuf));
    }

    std::wstring orig;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", &ptr, &len) && ptr && len >= 4) {
        WORD words[2]{};
        std::memcpy(words, ptr, sizeof(words));
        constexpr wchar_t hex_w[] = L"0123456789abcdef";
        wchar_t sub[64] = L"\\StringFileInfo\\00000000\\OriginalFilename";
        // Write 8 hex digits starting at offset 16 ("\\StringFileInfo\\")
        for (int s = 0; s < 4; ++s) {
            sub[16 + s] = hex_w[(words[0] >> ((3 - s) * 4)) & 0xFu];
            sub[20 + s] = hex_w[(words[1] >> ((3 - s) * 4)) & 0xFu];
        }
        if (VerQueryValueW(buf.data(), sub, &ptr, &len) && ptr && len > 0) {
            const wchar_t* wptr = static_cast<const wchar_t*>(ptr);
            orig.assign(wptr, wcsnlen(wptr, len));
        }
    }
    return {std::move(orig), std::move(ver_str)};
}

inline std::string signer_thumbprint_w(const wchar_t* path) {
    HCERTSTORE hStore = nullptr;
    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_FILE,
            path,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0, nullptr, nullptr, nullptr, &hStore, nullptr, nullptr) || !hStore) {
        return "";
    }
    std::string result;
    PCCERT_CONTEXT ctx = CertEnumCertificatesInStore(hStore, nullptr);
    if (ctx) {
        uint8_t buf[20]{};
        DWORD sz = 20;
        if (CertGetCertificateContextProperty(ctx, CERT_SHA1_HASH_PROP_ID, buf, &sz)) {
            result = hex_lower(buf, sz);
        }
        CertFreeCertificateContext(ctx);
    }
    CertCloseStore(hStore, 0);
    return result;
}

[[nodiscard]] inline std::string base64_encode(const uint8_t* data, size_t len) {
    if (len == 0) return {};
    if (!data) throw std::runtime_error("null base64_encode input");
    if (len > (std::numeric_limits<DWORD>::max)()) {
        throw std::runtime_error("base64_encode input too large");
    }
    DWORD out_len = 0;
    if (!CryptBinaryToStringA(
            data, static_cast<DWORD>(len),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            nullptr, &out_len)) {
        throw std::runtime_error("CryptBinaryToStringA size failed");
    }
    std::string out(out_len, '\0');
    if (!CryptBinaryToStringA(
            data, static_cast<DWORD>(len),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            out.data(), &out_len)) {
        throw std::runtime_error("CryptBinaryToStringA encode failed");
    }
    out.resize(out_len);
    return out;
}

[[nodiscard]] inline std::vector<uint8_t> base64_decode(std::string_view input) {
    bool has_non_ws = false;
    for (char ch : input) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            has_non_ws = true;
            break;
        }
    }
    if (!has_non_ws) return {};
    if (input.size() > (std::numeric_limits<DWORD>::max)()) {
        throw std::runtime_error("base64_decode input too large");
    }
    DWORD out_len = 0;
    if (!CryptStringToBinaryA(
            input.data(), static_cast<DWORD>(input.size()),
            CRYPT_STRING_BASE64,
            nullptr, &out_len, nullptr, nullptr)) {
        throw std::runtime_error("CryptStringToBinaryA size failed");
    }
    std::vector<uint8_t> out(out_len);
    if (!CryptStringToBinaryA(
            input.data(), static_cast<DWORD>(input.size()),
            CRYPT_STRING_BASE64,
            out.data(), &out_len, nullptr, nullptr)) {
        throw std::runtime_error("CryptStringToBinaryA decode failed");
    }
    out.resize(out_len);
    return out;
}

[[nodiscard]] inline std::string format_plist_date(std::string_view raw) {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    if (raw.size() < 19 || raw[4] != '-' || raw[7] != '-' ||
        !(raw[10] == 'T' || raw[10] == 't' || raw[10] == ' ') || raw[13] != ':' || raw[16] != ':') {
        return std::string(raw);
    }
    auto parse_u16 = [&](size_t pos, size_t len, WORD& out_v) -> bool {
        unsigned long val = 0;
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = static_cast<unsigned char>(raw[pos + i]);
            if (c < '0' || c > '9') return false;
            val = val * 10 + (c - '0');
        }
        out_v = static_cast<WORD>(val);
        return true;
    };
    WORD year = 0, month = 0, day = 0, hour = 0, minute = 0, sec = 0;
    if (!parse_u16(0, 4, year) || !parse_u16(5, 2, month) || !parse_u16(8, 2, day) ||
        !parse_u16(11, 2, hour) || !parse_u16(14, 2, minute) || !parse_u16(17, 2, sec)) {
        return std::string(raw);
    }

    size_t pos = 19;
    uint32_t usec = 0;
    if (pos < raw.size() && raw[pos] == '.') {
        ++pos;
        uint32_t mult = 100000;
        while (pos < raw.size() && std::isdigit(static_cast<unsigned char>(raw[pos]))) {
            if (mult > 0) {
                usec += static_cast<uint32_t>(raw[pos] - '0') * mult;
                mult /= 10;
            }
            ++pos;
        }
    }

    auto parse_exact_u = [](std::string_view sv, int64_t& val) -> bool {
        if (sv.empty()) return false;
        for (char ch : sv) {
            if (ch < '0' || ch > '9') return false;
        }
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return ec == std::errc{} && ptr == sv.data() + sv.size();
    };

    bool has_tz = false;
    int64_t tz_off_sec = 0;
    if (pos < raw.size()) {
        if ((raw[pos] == 'Z' || raw[pos] == 'z') && pos + 1 == raw.size()) {
            has_tz = true;
            tz_off_sec = 0;
        } else if (raw[pos] == '+' || raw[pos] == '-') {
            int64_t sign = (raw[pos] == '-') ? -1 : 1;
            std::string_view rest = raw.substr(pos + 1);
            int64_t hh = 0, mm = 0;
            size_t colon = rest.find(':');
            bool ok = false;
            if (colon != std::string_view::npos) {
                ok = parse_exact_u(rest.substr(0, colon), hh) &&
                     parse_exact_u(rest.substr(colon + 1), mm);
            } else if (rest.size() == 4) {
                ok = parse_exact_u(rest.substr(0, 2), hh) &&
                     parse_exact_u(rest.substr(2, 2), mm);
            } else if (rest.size() == 1 || rest.size() == 2) {
                ok = parse_exact_u(rest, hh);
            }
            if (!ok || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                return std::string(raw);
            }
            has_tz = true;
            tz_off_sec = sign * (hh * 3600 + mm * 60);
        } else {
            return std::string(raw);
        }
    }

    auto append_u_pad = [](std::string& dst, uint64_t v, size_t width) {
        char tmp[32];
        auto [p, ec] = std::to_chars(tmp, tmp + sizeof(tmp), v);
        size_t n = static_cast<size_t>(p - tmp);
        if (n < width) dst.append(width - n, '0');
        dst.append(tmp, n);
    };

    if (!has_tz) {
        std::string out;
        out.reserve(32);
        append_u_pad(out, year, 4); out.push_back('-');
        append_u_pad(out, month, 2); out.push_back('-');
        append_u_pad(out, day, 2); out.push_back('T');
        append_u_pad(out, hour, 2); out.push_back(':');
        append_u_pad(out, minute, 2); out.push_back(':');
        append_u_pad(out, sec, 2); out.push_back('.');
        append_u_pad(out, usec, 6);
        return out;
    }

    SYSTEMTIME st{};
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    st.wMinute = minute;
    st.wSecond = sec;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return std::string(raw);
    uint64_t ft_u64 = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    int64_t tz_ticks = tz_off_sec * 10000000LL;
    if (ft_u64 > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) ||
        (tz_ticks > 0 && static_cast<uint64_t>(tz_ticks) > ft_u64)) {
        return std::string(raw);
    }
    uint64_t utc_u64 = static_cast<uint64_t>(static_cast<int64_t>(ft_u64) - tz_ticks);
    FILETIME utc_ft{};
    utc_ft.dwLowDateTime = static_cast<DWORD>(utc_u64 & 0xFFFFFFFFULL);
    utc_ft.dwHighDateTime = static_cast<DWORD>(utc_u64 >> 32);

    SYSTEMTIME utc_st{}, local_st{};
    FILETIME local_ft{};
    if (!FileTimeToSystemTime(&utc_ft, &utc_st) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc_st, &local_st) ||
        !SystemTimeToFileTime(&local_st, &local_ft)) {
        return std::string(raw);
    }
    uint64_t local_u64 = (static_cast<uint64_t>(local_ft.dwHighDateTime) << 32) | local_ft.dwLowDateTime;
    int64_t local_off_sec = (static_cast<int64_t>(local_u64) - static_cast<int64_t>(utc_u64)) / 10000000LL;
    char sign = (local_off_sec < 0) ? '-' : '+';
    int64_t abs_min = (local_off_sec < 0 ? -local_off_sec : local_off_sec) / 60;
    uint64_t off_h = static_cast<uint64_t>(abs_min / 60);
    uint64_t off_m = static_cast<uint64_t>(abs_min % 60);

    std::string out;
    out.reserve(40);
    append_u_pad(out, local_st.wYear, 4); out.push_back('-');
    append_u_pad(out, local_st.wMonth, 2); out.push_back('-');
    append_u_pad(out, local_st.wDay, 2); out.push_back('T');
    append_u_pad(out, local_st.wHour, 2); out.push_back(':');
    append_u_pad(out, local_st.wMinute, 2); out.push_back(':');
    append_u_pad(out, local_st.wSecond, 2); out.push_back('.');
    append_u_pad(out, usec, 6);
    out.push_back(sign);
    append_u_pad(out, off_h, 2); out.push_back(':');
    append_u_pad(out, off_m, 2);
    if (local_off_sec == 0) out.push_back('Z');
    return out;
}

} // namespace seb::win
