#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
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
#include <limits>
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

inline std::vector<uint8_t> read_file_bytes_w(const wchar_t* path) {
    return nt::nt_read_file_bytes_w(path);
}

inline bool write_file_bytes_w(const wchar_t* path, const void* data, size_t len) {
    return nt::nt_write_file_bytes_w(path, data, len);
}

inline std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

inline std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline constexpr std::array<uint16_t, 256> make_hex_lut() {
    std::array<uint16_t, 256> lut{};
    constexpr char H[] = "0123456789abcdef";
    for (int i = 0; i < 256; ++i) {
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

inline std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    BCRYPT_HASH_HANDLE hHash = detail::thread_sha256_handle();
    std::array<uint8_t, 32> out{};
    NTSTATUS st = 0;
    size_t off = 0;
    while (off < len) {
        ULONG chunk = static_cast<ULONG>((std::min<size_t>)(len - off, 0x40000000ULL));
        st = BCryptHashData(hHash, const_cast<PUCHAR>(data + off), chunk, 0);
        if (st != 0) break;
        off += chunk;
    }
    NTSTATUS fst = BCryptFinishHash(hHash, out.data(), 32, 0);
    if (st != 0 || fst != 0) throw std::runtime_error("BCryptHash SHA256 failed");
    return out;
}

inline std::string sha256_hex(const uint8_t* data, size_t len) {
    auto digest = sha256_raw(data, len);
    return hex_lower(digest.data(), 32);
}

inline std::string sha256_hex(std::string_view s) {
    return sha256_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
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

inline std::array<uint8_t, 32> hmac_sha256_raw(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len) {
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(
            detail::hmac_sha256_alg(), &hHash, nullptr, 0,
            const_cast<PUCHAR>(key), static_cast<ULONG>(key_len), 0) != 0) {
        throw std::runtime_error("BCryptCreateHash HMAC-SHA256 failed");
    }
    std::array<uint8_t, 32> out{};
    NTSTATUS st = 0;
    size_t off = 0;
    while (off < len) {
        ULONG chunk = static_cast<ULONG>((std::min<size_t>)(len - off, 0x40000000ULL));
        st = BCryptHashData(hHash, const_cast<PUCHAR>(data + off), chunk, 0);
        if (st != 0) break;
        off += chunk;
    }
    if (st == 0) st = BCryptFinishHash(hHash, out.data(), 32, 0);
    BCryptDestroyHash(hHash);
    if (st != 0) throw std::runtime_error("BCryptHash HMAC-SHA256 failed");
    return out;
}

inline std::array<uint8_t, 32> pbkdf2_hmac_sha1_32(
    std::string_view password,
    const uint8_t* salt,
    size_t salt_len,
    uint64_t iterations
) {
    std::array<uint8_t, 32> out{};
    NTSTATUS st = BCryptDeriveKeyPBKDF2(
        detail::hmac_sha1_alg(),
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt),
        static_cast<ULONG>(salt_len),
        iterations,
        out.data(),
        32,
        0);
    if (st != 0) throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    return out;
}

inline std::vector<uint8_t> aes256_cbc_decrypt(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* iv,
    const uint8_t* ciphertext,
    size_t ciphertext_len
) {
    if (key_len != 32 || ciphertext_len == 0 || (ciphertext_len % 16) != 0) {
        throw std::runtime_error("invalid AES-256-CBC parameters");
    }
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateSymmetricKey(
            detail::aes_cbc_alg(), &hKey, nullptr, 0,
            const_cast<PUCHAR>(key), static_cast<ULONG>(key_len), 0) != 0) {
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");
    }

    uint8_t iv_buf[16];
    std::memcpy(iv_buf, iv, 16);
    std::vector<uint8_t> out(ciphertext_len);
    ULONG res_len = 0;
    NTSTATUS st = BCryptDecrypt(
        hKey,
        const_cast<PUCHAR>(ciphertext),
        static_cast<ULONG>(ciphertext_len),
        nullptr,
        iv_buf,
        16,
        out.data(),
        static_cast<ULONG>(out.size()),
        &res_len,
        0);
    SecureZeroMemory(iv_buf, sizeof(iv_buf));
    BCryptDestroyKey(hKey);
    if (st != 0) throw std::runtime_error("BCryptDecrypt failed");

    out.resize(res_len);
    if (out.empty()) throw std::runtime_error("empty decrypted data");
    uint8_t pad = out.back();
    if (pad == 0 || pad > 16 || pad > out.size()) {
        throw std::runtime_error("invalid PKCS7 padding");
    }
    for (size_t i = out.size() - pad; i < out.size(); ++i) {
        if (out[i] != pad) throw std::runtime_error("invalid PKCS7 padding bytes");
    }
    out.resize(out.size() - pad);
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

inline std::pair<std::wstring, std::string> file_metadata_w(const wchar_t* path, bool known_pe = false) {
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
        auto* info = static_cast<const VS_FIXEDFILEINFO*>(ptr);
        char vbuf[64];
        int n = std::snprintf(
            vbuf, sizeof(vbuf), "%u.%u.%u.%u",
            static_cast<unsigned>(info->dwFileVersionMS >> 16),
            static_cast<unsigned>(info->dwFileVersionMS & 0xFFFF),
            static_cast<unsigned>(info->dwFileVersionLS >> 16),
            static_cast<unsigned>(info->dwFileVersionLS & 0xFFFF));
        if (n > 0) ver_str.assign(vbuf, static_cast<size_t>(n));
    }

    std::wstring orig;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", &ptr, &len) && ptr && len >= 4) {
        auto* words = static_cast<const WORD*>(ptr);
        wchar_t sub[64];
        swprintf(sub, 64, L"\\StringFileInfo\\%04x%04x\\OriginalFilename", words[0], words[1]);
        if (VerQueryValueW(buf.data(), sub, &ptr, &len) && ptr && len > 0) {
            orig.assign(static_cast<const wchar_t*>(ptr));
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

inline std::string base64_encode(const uint8_t* data, size_t len) {
    if (len == 0) return {};
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

inline std::vector<uint8_t> base64_decode(std::string_view input) {
    bool has_non_ws = false;
    for (unsigned char c : input) {
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

inline std::string format_plist_date(std::string_view raw) {
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

    bool has_tz = false;
    int64_t tz_off_sec = 0;
    if (pos < raw.size()) {
        if (raw[pos] == 'Z' || raw[pos] == 'z') {
            has_tz = true;
            tz_off_sec = 0;
        } else if (raw[pos] == '+' || raw[pos] == '-') {
            int64_t sign = (raw[pos] == '-') ? -1 : 1;
            std::string_view rest = raw.substr(pos + 1);
            int64_t hh = 0, mm = 0;
            if (!rest.empty()) {
                size_t colon = rest.find(':');
                if (colon != std::string_view::npos) {
                    std::from_chars(rest.data(), rest.data() + colon, hh);
                    std::from_chars(rest.data() + colon + 1, rest.data() + rest.size(), mm);
                } else if (rest.size() >= 4) {
                    std::from_chars(rest.data(), rest.data() + 2, hh);
                    std::from_chars(rest.data() + 2, rest.data() + 4, mm);
                } else {
                    std::from_chars(rest.data(), rest.data() + rest.size(), hh);
                }
            }
            has_tz = true;
            tz_off_sec = sign * (hh * 3600 + mm * 60);
        }
    }

    char buf[64];
    if (!has_tz) {
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%06u",
                      year, month, day, hour, minute, sec, usec);
        return buf;
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
    uint64_t utc_u64 = static_cast<uint64_t>(static_cast<int64_t>(ft_u64) - tz_off_sec * 10000000LL);
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
    int64_t off_h = abs_min / 60;
    int64_t off_m = abs_min % 60;
    const char* z_suffix = (local_off_sec == 0) ? "Z" : "";
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%06u%c%02lld:%02lld%s",
                  local_st.wYear, local_st.wMonth, local_st.wDay,
                  local_st.wHour, local_st.wMinute, local_st.wSecond,
                  usec, sign, static_cast<long long>(off_h), static_cast<long long>(off_m), z_suffix);
    return buf;
}

} // namespace seb::win
