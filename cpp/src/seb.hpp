#pragma once

#include "inflate.hpp"
#include "plist.hpp"
#include "win_crypto.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seb {

inline bool is_dir_w(const wchar_t* path) {
    return nt::nt_is_dir_w(path);
}

inline bool is_file_w(const wchar_t* path) {
    return nt::nt_is_file_w(path);
}

inline bool iequals_w(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

inline bool contains_ci_w(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::towlower(haystack[i + j]) != std::towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

inline std::wstring join_path_w(std::wstring_view base, std::wstring_view child) {
    std::wstring out;
    out.reserve(base.size() + 1 + child.size());
    out.append(base);
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') {
        out.push_back(L'\\');
    }
    out.append(child);
    return out;
}

inline std::wstring_view filename_w(std::wstring_view p) {
    while (p.size() > 1 && (p.back() == L'\\' || p.back() == L'/')) {
        if (p.size() == 3 && p[1] == L':') break;
        p.remove_suffix(1);
    }
    size_t sep = p.find_last_of(L"\\/");
    if (sep == std::wstring_view::npos) return p;
    return p.substr(sep + 1);
}

inline std::wstring_view parent_path_w(std::wstring_view p) {
    while (p.size() > 1 && (p.back() == L'\\' || p.back() == L'/')) {
        if (p.size() == 3 && p[1] == L':') return p;
        p.remove_suffix(1);
    }
    size_t sep = p.find_last_of(L"\\/");
    if (sep == std::wstring_view::npos) return {};
    if (sep == 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, sep);
}

inline std::wstring_view extension_w(std::wstring_view p) {
    std::wstring_view fn = filename_w(p);
    size_t dot = fn.rfind(L'.');
    if (dot == std::wstring_view::npos) return {};
    return fn.substr(dot);
}

inline std::optional<std::wstring> root_from_exe_path_w(std::wstring_view exe_path) {
    std::wstring tmp(exe_path);
    if (!is_file_w(tmp.c_str())) return std::nullopt;
    std::wstring_view parent = parent_path_w(exe_path);
    if (iequals_w(filename_w(parent), L"application")) {
        return std::wstring(parent_path_w(parent));
    }
    return std::wstring(parent);
}

inline std::optional<std::wstring> first_cmd_arg_w(std::wstring_view cmd) {
    size_t i = 0;
    while (i < cmd.size() && std::iswspace(cmd[i])) i++;
    if (i >= cmd.size()) return std::nullopt;
    if (cmd[i] == L'"') {
        size_t end = cmd.find(L'"', i + 1);
        if (end == std::wstring_view::npos) return std::nullopt;
        return std::wstring(cmd.substr(i + 1, end - i - 1));
    }
    size_t end = i;
    while (end < cmd.size() && !std::iswspace(cmd[end])) end++;
    return std::wstring(cmd.substr(i, end - i));
}

inline std::wstring detect_seb_root() {
    for (const wchar_t* sub : {
            L"seb\\shell\\open\\command",
            L"sebs\\shell\\open\\command",
            L"SafeExamBrowser\\shell\\open\\command"}) {
        std::wstring cmd;
        if (win::reg_read_wstring(HKEY_CLASSES_ROOT, sub, L"", cmd)) {
            if (auto exe = first_cmd_arg_w(cmd)) {
                if (auto root = root_from_exe_path_w(*exe)) {
                    if (is_dir_w(root->c_str())) return *root;
                }
            }
        }
    }

    struct HivePath { HKEY hive; const wchar_t* path; };
    const HivePath uninstall_roots[] = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
    };

    for (const auto& [hive, path] : uninstall_roots) {
        for (const auto& sub : win::reg_enum_subkeys(hive, path)) {
            std::wstring full = std::wstring(path) + L"\\" + sub;
            std::wstring display;
            if (!win::reg_read_wstring(hive, full.c_str(), L"DisplayName", display)) continue;
            if (!contains_ci_w(display, L"safe exam browser")) continue;

            for (const wchar_t* val_name : {L"InstallLocation", L"DisplayIcon"}) {
                std::wstring raw;
                if (!win::reg_read_wstring(hive, full.c_str(), val_name, raw)) continue;
                size_t comma = raw.find(L',');
                if (comma != std::wstring::npos) raw.resize(comma);
                size_t b = 0, e = raw.size();
                while (b < e && (raw[b] == L'"' || raw[b] == L' ')) ++b;
                while (e > b && (raw[e - 1] == L'"' || raw[e - 1] == L' ')) --e;
                if (b >= e) continue;
                std::wstring p = raw.substr(b, e - b);
                std::optional<std::wstring> root = iequals_w(extension_w(p), L".exe")
                    ? root_from_exe_path_w(p)
                    : std::optional<std::wstring>(p);
                if (root && is_dir_w(root->c_str())) return *root;
            }
        }
    }

    for (const wchar_t* env_var : {L"ProgramFiles", L"ProgramFiles(x86)", L"LocalAppData"}) {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(env_var, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring candidate = join_path_w(buf, L"SafeExamBrowser");
            if (is_dir_w(candidate.c_str())) return candidate;
        }
    }

    throw std::runtime_error("Could not locate SEB installation; pass --root explicitly.");
}

inline std::optional<std::wstring> find_file_recursive_w(const std::wstring& dir, std::wstring_view target) {
    nt::NtHandle dh(nt::nt_open_dir(dir.c_str()));
    if (!dh) return std::nullopt;

    alignas(8) uint8_t buf[65536];
    std::vector<std::wstring> subdirs;
    std::optional<std::wstring> found;
    BOOLEAN restart = TRUE;

    while (!found) {
        nt::IO_STATUS_BLOCK iosb{};
        NTSTATUS st = nt::NtQueryDirectoryFile(
            dh.get(), nullptr, nullptr, nullptr, &iosb, buf, sizeof(buf),
            nt::FileDirectoryInformationClass, FALSE, nullptr, restart);
        restart = FALSE;
        if (st != 0 && st != nt::STATUS_BUFFER_OVERFLOW) break;

        size_t off = 0;
        size_t processed = 0;
        while (true) {
            ++processed;
            const auto* info = reinterpret_cast<const nt::FILE_DIRECTORY_INFORMATION*>(buf + off);
            std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (!(name == L"." || name == L"..")) {
                if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (!(info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                        subdirs.push_back(join_path_w(dir, name));
                    }
                } else if (iequals_w(name, target)) {
                    found = join_path_w(dir, name);
                    break;
                }
            }
            if (info->NextEntryOffset == 0) break;
            off += info->NextEntryOffset;
        }
        if (st == nt::STATUS_BUFFER_OVERFLOW && processed == 0) break;
    }

    if (found) return found;
    for (const auto& sub : subdirs) {
        if (auto res = find_file_recursive_w(sub, target)) return res;
    }
    return std::nullopt;
}

inline std::wstring find_seb_exe(const std::wstring& root) {
    std::wstring direct = join_path_w(join_path_w(root, L"Application"), L"SafeExamBrowser.exe");
    if (is_file_w(direct.c_str())) return direct;
    if (auto found = find_file_recursive_w(root, L"SafeExamBrowser.exe")) return *found;
    throw std::runtime_error("SafeExamBrowser.exe not found under " + win::wide_to_utf8(root));
}

inline std::wstring find_seb_dll(const std::wstring& root) {
    std::wstring direct = join_path_w(join_path_w(root, L"Application"), L"seb_x64.dll");
    if (is_file_w(direct.c_str())) return direct;
    if (auto found = find_file_recursive_w(root, L"seb_x64.dll")) return *found;
    throw std::runtime_error("seb_x64.dll not found under " + win::wide_to_utf8(root));
}

inline std::string detect_seb_version(const std::wstring& root) {
    std::wstring exe = find_seb_exe(root);
    auto [orig, ver] = win::file_metadata_w(exe.c_str());
    if (ver.empty()) throw std::runtime_error("Could not read version from " + win::wide_to_utf8(exe));
    return ver;
}

inline uint16_t u16_le(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
inline uint32_t u32_le(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline int32_t i32_le(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline std::string extract_salt_const(const std::wstring& root) {
    std::wstring dll_path = find_seb_dll(root);
    win::MappedFile mf = win::MappedFile::open(dll_path.c_str());
    const uint8_t* pe = mf.view;
    size_t pe_sz = static_cast<size_t>(mf.size);

    if (pe_sz < 0x40 || pe[0] != 'M' || pe[1] != 'Z') {
        throw std::runtime_error("invalid MZ header");
    }
    size_t e_lfanew = u32_le(pe + 0x3C);
    if (e_lfanew + 24 > pe_sz || std::memcmp(pe + e_lfanew, "PE\0\0", 4) != 0) {
        throw std::runtime_error("invalid PE signature");
    }
    uint16_t num_sec = u16_le(pe + e_lfanew + 6);
    uint16_t opt_sz = u16_le(pe + e_lfanew + 20);
    size_t sec_start = e_lfanew + 24 + opt_sz;

    struct Sec {
        char name[9]{};
        uint32_t vsize, vaddr, raw_sz, raw_ptr;
    };
    std::vector<Sec> sections;
    sections.reserve(num_sec);
    for (uint16_t i = 0; i < num_sec; ++i) {
        size_t off = sec_start + i * 40;
        if (off + 40 > pe_sz) throw std::runtime_error("truncated section table");
        Sec s;
        std::memcpy(s.name, pe + off, 8);
        s.vsize = u32_le(pe + off + 8);
        s.vaddr = u32_le(pe + off + 12);
        s.raw_sz = u32_le(pe + off + 16);
        s.raw_ptr = u32_le(pe + off + 20);
        sections.push_back(s);
    }

    auto rva_ptr = [&](uint32_t rva, size_t len) -> const uint8_t* {
        for (const auto& s : sections) {
            uint64_t end_rva = static_cast<uint64_t>(s.vaddr) + s.raw_sz;
            if (rva >= s.vaddr && static_cast<uint64_t>(rva) + len <= end_rva) {
                size_t file_off = static_cast<size_t>(s.raw_ptr) + (rva - s.vaddr);
                if (file_off <= pe_sz && len <= pe_sz - file_off) return pe + file_off;
            }
        }
        return nullptr;
    };

    const Sec* text_sec = nullptr;
    for (const auto& s : sections) {
        if (std::strcmp(s.name, ".text") == 0) {
            text_sec = &s;
            break;
        }
    }
    if (!text_sec) throw std::runtime_error(".text section not found in " + win::wide_to_utf8(dll_path));
    if (static_cast<size_t>(text_sec->raw_ptr) + text_sec->raw_sz > pe_sz) {
        throw std::runtime_error("truncated .text section");
    }

    const uint8_t* text = pe + text_sec->raw_ptr;
    size_t text_len = text_sec->raw_sz;
    uint32_t text_rva = text_sec->vaddr;

    for (size_t pos = 0; pos + 15 <= text_len;) {
        const void* hit = std::memchr(text + pos, 0x66, text_len - pos - 14);
        if (!hit) break;
        pos = static_cast<const uint8_t*>(hit) - text;

        if (u32_le(text + pos) == 0x056f0f66u &&
            text[pos + 8] == 0x45 && text[pos + 9] == 0x33 && text[pos + 10] == 0xc0 &&
            u32_le(text + pos + 11) == 0x0d6f0f66u) {
            size_t win_len = (std::min)(static_cast<size_t>(0x90), text_len - pos);
            const uint8_t* win = text + pos;

            uint8_t movdqas[8][16]{};
            size_t mov_cnt = 0;
            for (size_t i = 0; i + 8 <= win_len && mov_cnt < 8; ++i) {
                if (win[i] == 0x66 && win[i + 1] == 0x0f && win[i + 2] == 0x6f &&
                    (win[i + 3] == 0x05 || win[i + 3] == 0x0d || win[i + 3] == 0x15 || win[i + 3] == 0x1d)) {
                    int32_t disp = i32_le(win + i + 4);
                    uint32_t target_rva = static_cast<uint32_t>(
                        static_cast<int64_t>(text_rva) + static_cast<int64_t>(pos) + static_cast<int64_t>(i + 8) + disp);
                    if (const uint8_t* p16 = rva_ptr(target_rva, 16)) {
                        std::memcpy(movdqas[mov_cnt++], p16, 16);
                    }
                }
            }

            uint8_t dwords[8][4]{};
            size_t dw_cnt = 0;
            for (size_t j = 0; j + 7 <= win_len && dw_cnt < 8; ++j) {
                if (win[j] == 0xc7 && win[j + 1] == 0x45) {
                    std::memcpy(dwords[dw_cnt++], win + j + 3, 4);
                }
            }

            if (mov_cnt >= 4 && dw_cnt >= 4) {
                uint8_t buf_a[40], buf_b[40];
                std::memcpy(buf_a, movdqas[0], 16);
                std::memcpy(buf_a + 16, movdqas[1], 16);
                std::memcpy(buf_a + 32, dwords[0], 4);
                std::memcpy(buf_a + 36, dwords[1], 4);

                std::memcpy(buf_b, movdqas[2], 16);
                std::memcpy(buf_b + 16, movdqas[3], 16);
                std::memcpy(buf_b + 32, dwords[2], 4);
                std::memcpy(buf_b + 36, dwords[3], 4);

                std::string cand(40, '\0');
                bool ok = true;
                for (int k = 0; k < 40; ++k) {
                    uint8_t c = buf_a[k] ^ buf_b[k];
                    cand[k] = static_cast<char>(c);
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                        ok = false;
                        break;
                    }
                }
                if (ok) return cand;
            }
        }
        ++pos;
    }

    throw std::runtime_error("Could not locate integrity salt in " + win::wide_to_utf8(dll_path));
}

inline void append_wide_truncated_w(std::vector<uint8_t>& dst, std::wstring_view w) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(w.data());
    dst.insert(dst.end(), raw, raw + w.size());
}

inline void append_ascii_to_wide(std::wstring& dst, std::string_view ascii) {
    for (unsigned char c : ascii) {
        dst.push_back(static_cast<wchar_t>(c));
    }
}

inline std::vector<uint8_t> tree_fingerprint_bytes(const std::wstring& root) {
    struct Event {
        bool is_dir;
        std::wstring dir_name;
        size_t file_idx = 0;
    };
    struct FileItem {
        std::wstring path;
        std::wstring name;
        uint64_t size = 0;
    };
    struct FileMeta {
        std::string cert;
        std::string content_hash;
        std::wstring original;
        std::string version;
        uint64_t size = 0;
    };

    std::vector<Event> events;
    std::vector<FileItem> file_items;
    events.reserve(512);
    file_items.reserve(512);

    auto scan = [&](auto& self, const std::wstring& dir, std::wstring_view dir_name) -> void {
        events.push_back({true, std::wstring(dir_name), 0});
        nt::NtHandle dh(nt::nt_open_dir(dir.c_str()));
        if (!dh) return;

        alignas(8) uint8_t buf[65536];
        std::vector<std::pair<std::wstring, std::wstring>> subdirs;
        std::vector<FileItem> files;
        BOOLEAN restart = TRUE;

        while (true) {
            nt::IO_STATUS_BLOCK iosb{};
            NTSTATUS st = nt::NtQueryDirectoryFile(
                dh.get(), nullptr, nullptr, nullptr, &iosb, buf, sizeof(buf),
                nt::FileDirectoryInformationClass, FALSE, nullptr, restart);
            restart = FALSE;
            if (st != 0 && st != nt::STATUS_BUFFER_OVERFLOW) break;

            size_t off = 0;
            size_t processed = 0;
            while (true) {
                ++processed;
                const auto* info = reinterpret_cast<const nt::FILE_DIRECTORY_INFORMATION*>(buf + off);
                std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                if (!(name == L"." || name == L"..") && !(info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        subdirs.emplace_back(join_path_w(dir, name), std::wstring(name));
                    } else {
                        uint64_t sz = static_cast<uint64_t>(info->EndOfFile.QuadPart);
                        files.push_back({join_path_w(dir, name), std::wstring(name), sz});
                    }
                }
                if (info->NextEntryOffset == 0) break;
                off += info->NextEntryOffset;
            }
            if (st == nt::STATUS_BUFFER_OVERFLOW && processed == 0) break;
        }

        for (const auto& [sub_path, sub_name] : subdirs) {
            self(self, sub_path, sub_name);
        }
        for (auto& item : files) {
            size_t idx = file_items.size();
            events.push_back({false, {}, idx});
            file_items.push_back(std::move(item));
        }
    };
    scan(scan, root, filename_w(root));

    std::vector<FileMeta> results(file_items.size());
    std::atomic<size_t> next_idx{0};

    // Hash the largest files first. This keeps the long pole (libcef.dll is
    // usually >200 MB) from starting after a worker has already chewed
    // through a batch of small files. The fingerprint order itself is still
    // driven by `events`/`file_items`, so this only changes scheduling.
    std::vector<size_t> work_order(file_items.size());
    for (size_t i = 0; i < work_order.size(); ++i) work_order[i] = i;
    std::sort(work_order.begin(), work_order.end(), [&](size_t a, size_t b) {
        return file_items[a].size > file_items[b].size;
    });

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD cpus = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 4;
    size_t workers = (std::min<size_t>)(8, cpus + 2);
    if (workers > file_items.size() && !file_items.empty()) workers = file_items.size();
    if (file_items.empty()) workers = 1;

    struct WorkerCtx {
        const std::vector<FileItem>* items;
        std::vector<FileMeta>* results;
        std::atomic<size_t>* next_idx;
        const std::vector<size_t>* order;
    } ctx{&file_items, &results, &next_idx, &work_order};

    auto worker_proc = [](LPVOID param) -> DWORD {
        auto* c = static_cast<WorkerCtx*>(param);
        while (true) {
            size_t work = c->next_idx->fetch_add(1, std::memory_order_relaxed);
            if (work >= c->order->size()) break;
            const size_t idx = (*c->order)[work];
            const auto& item = (*c->items)[idx];
            FileMeta m;
            m.size = item.size;
            bool is_pe = false;
            m.content_hash = win::sha256_file_hex_w(item.path.c_str(), item.size, &is_pe);
            if (is_pe) {
                // Non-PE files cannot carry a version resource, so the
                // original filename/version are always empty for them.
                auto [orig, ver] = win::file_metadata_w(item.path.c_str(), true);
                m.original = std::move(orig);
                m.version = std::move(ver);
            }
            std::wstring_view ext = extension_w(item.name);
            if (iequals_w(ext, L".dll") || iequals_w(ext, L".exe")) {
                m.cert = win::signer_thumbprint_w(item.path.c_str());
            }
            (*c->results)[idx] = std::move(m);
        }
        return 0;
    };

    if (workers <= 1) {
        worker_proc(&ctx);
    } else {
        std::vector<HANDLE> threads;
        threads.reserve(workers);
        for (size_t w = 0; w < workers; ++w) {
            HANDLE th = CreateThread(nullptr, 0, worker_proc, &ctx, 0, nullptr);
            if (th) threads.push_back(th);
        }
        if (threads.empty()) {
            worker_proc(&ctx);
        } else {
            nt::nt_wait_all(threads.data(), static_cast<ULONG>(threads.size()));
            for (HANDLE th : threads) nt::NtClose(th);
        }
    }

    // sha256_file_hex_w returns an empty string on I/O failure. Fail loudly
    // instead of silently deriving a browser key from a partial fingerprint.
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].content_hash.empty()) {
            throw std::runtime_error("failed to hash " + win::wide_to_utf8(file_items[i].path));
        }
    }

    std::vector<uint8_t> parts;
    parts.reserve(file_items.size() * 128);
    std::wstring combined;
    combined.reserve(256);

    for (const auto& ev : events) {
        if (ev.is_dir) {
            append_wide_truncated_w(parts, ev.dir_name);
        } else {
            const auto& item = file_items[ev.file_idx];
            const auto& m = results[ev.file_idx];
            combined.clear();
            append_ascii_to_wide(combined, m.cert);
            append_ascii_to_wide(combined, m.content_hash);
            combined.append(item.name);
            combined.append(m.original);
            char sz_buf[32];
            auto [ptr, ec] = std::to_chars(sz_buf, sz_buf + sizeof(sz_buf), m.size);
            append_ascii_to_wide(combined, std::string_view(sz_buf, static_cast<size_t>(ptr - sz_buf)));
            append_ascii_to_wide(combined, m.version);
            append_wide_truncated_w(parts, combined);
        }
    }
    return parts;
}

inline std::vector<uint8_t> rncryptor_decrypt(const uint8_t* data, size_t len, std::string_view password) {
    if (len < 34 + 32) throw std::runtime_error("rncryptor payload too short");
    if ((data[0] != 2 && data[0] != 3) || data[1] != 1) {
        throw std::runtime_error("unsupported rncryptor version or options");
    }
    const uint8_t* enc_salt = data + 2;
    const uint8_t* hmac_salt = data + 10;
    const uint8_t* iv = data + 18;
    const uint8_t* ciphertext = data + 34;
    size_t ct_len = len - 34 - 32;
    const uint8_t* mac = data + len - 32;

    auto hmac_key = win::pbkdf2_hmac_sha1_32(password, hmac_salt, 8, 10000);
    win::MemoryLockGuard hmac_lock(hmac_key.data(), hmac_key.size());
    auto expected_mac = win::hmac_sha256_raw(hmac_key.data(), hmac_key.size(), data, len - 32);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= expected_mac[i] ^ mac[i];
    if (diff != 0) throw std::runtime_error("rncryptor HMAC verification failed (wrong password?)");

    auto enc_key = win::pbkdf2_hmac_sha1_32(password, enc_salt, 8, 10000);
    win::MemoryLockGuard enc_lock(enc_key.data(), enc_key.size());
    return win::aes256_cbc_decrypt(enc_key.data(), enc_key.size(), iv, ciphertext, ct_len);
}

// Cheap first-byte validation so an obviously invalid file can be rejected
// before the expensive SEB tree scan starts. This mirrors the first iteration
// of unpack_container exactly.
inline bool looks_like_seb_container(std::string_view data) {
    size_t start = 0;
    auto byte_at = [&](size_t i) { return static_cast<unsigned char>(data[i]); };
    while (start < data.size() && std::isspace(static_cast<unsigned char>(data[start]))) ++start;
    if (data.size() - start >= 3 && byte_at(start) == 0xEF && byte_at(start + 1) == 0xBB &&
        byte_at(start + 2) == 0xBF) {
        start += 3;
        while (start < data.size() && std::isspace(static_cast<unsigned char>(data[start]))) ++start;
    }
    auto starts = [&](const char* pat, size_t plen) {
        return data.size() - start >= plen && std::memcmp(data.data() + start, pat, plen) == 0;
    };
    if (starts("<?xml", 5) || starts("<!DOCTYPE", 9) || starts("<plist", 6)) return true;
    if (data.size() >= 2 && byte_at(0) == 0x1f && byte_at(1) == 0x8b) return true;
    if (data.size() >= 4) {
        std::string_view hdr(data.data(), 4);
        if (hdr == "pwcc" || hdr == "pswd" || hdr == "plnd") return true;
    }
    return false;
}

inline std::pair<std::vector<uint8_t>, std::string> unpack_container(
    std::vector<uint8_t> current,
    std::string_view password
) {
    std::string mode = "xml";

    for (int iter = 0; iter < 8; ++iter) {
        if (current.empty()) break;
        size_t start = 0;
        while (start < current.size() && std::isspace(static_cast<unsigned char>(current[start]))) start++;
        if (current.size() - start >= 3 && current[start] == 0xEF && current[start + 1] == 0xBB && current[start + 2] == 0xBF) {
            start += 3;
            while (start < current.size() && std::isspace(static_cast<unsigned char>(current[start]))) start++;
        }
        auto starts = [&](const char* pat, size_t plen) {
            return current.size() - start >= plen && std::memcmp(current.data() + start, pat, plen) == 0;
        };
        if (starts("<?xml", 5) || starts("<!DOCTYPE", 9) || starts("<plist", 6)) {
            if (start > 0) current.erase(current.begin(), current.begin() + static_cast<ptrdiff_t>(start));
            return {std::move(current), mode};
        }
        if (current.size() >= 2 && current[0] == 0x1f && current[1] == 0x8b) {
            current = inflate::gzip_decompress(current);
            continue;
        }
        if (current.size() >= 4) {
            std::string_view hdr(reinterpret_cast<const char*>(current.data()), 4);
            if (hdr == "pwcc" || hdr == "pswd") {
                mode = std::string(hdr);
                std::string effective_buf;
                std::string_view effective = password;
                if (hdr == "pwcc" && !password.empty()) {
                    effective_buf = win::sha256_hex(password);
                    for (char& c : effective_buf) {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    }
                    effective = effective_buf;
                }
                current = rncryptor_decrypt(current.data() + 4, current.size() - 4, effective);
                if (!effective_buf.empty()) {
                    SecureZeroMemory(effective_buf.data(), effective_buf.size());
                }
                continue;
            }
            if (hdr == "plnd") {
                mode = "plnd";
                current.erase(current.begin(), current.begin() + 4);
                continue;
            }
        }
        break;
    }

    throw std::runtime_error("input is not a valid SEB configuration");
}

inline std::string detect_purpose(std::string_view mode, const plist::PlistDict& settings) {
    if (mode == "pwcc") return "configuring-client";
    return plist::find_key(settings, "sebConfigPurpose") ? "as-is" : "starting-exam";
}

inline std::string salt_hex(const plist::PlistDict& settings) {
    const auto* v = plist::find_key(settings, "examKeySalt");
    if (!v) throw std::runtime_error("examKeySalt missing");
    if (auto* bd = std::get_if<plist::PlistData>(&v->data)) {
        return win::hex_lower(bd->bytes);
    }
    if (auto* s = std::get_if<plist::PlistStr>(&v->data)) {
        if (s->text && !s->text->empty()) return *s->text;
    }
    throw std::runtime_error("examKeySalt missing");
}

inline std::optional<std::string> get_start_url(const plist::PlistDict& settings) {
    const auto* v = plist::find_key(settings, "startURL");
    if (!v) return std::nullopt;
    if (auto* s = std::get_if<plist::PlistStr>(&v->data)) {
        if (s->text && !s->text->empty()) return *s->text;
    }
    return std::nullopt;
}

inline std::string compute_browser_key(
    const std::vector<uint8_t>& tree,
    std::string_view config_key,
    std::string_view salt,
    std::string_view salt_const
) {
    BCRYPT_HASH_HANDLE hHash = win::detail::thread_sha256_handle();
    NTSTATUS st = 0;
    if (!tree.empty()) st = BCryptHashData(hHash, const_cast<PUCHAR>(tree.data()), static_cast<ULONG>(tree.size()), 0);
    if (st == 0 && !config_key.empty()) st = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(config_key.data())), static_cast<ULONG>(config_key.size()), 0);
    if (st == 0 && !salt.empty()) st = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(salt.data())), static_cast<ULONG>(salt.size()), 0);
    if (st == 0 && !salt_const.empty()) st = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(salt_const.data())), static_cast<ULONG>(salt_const.size()), 0);
    std::array<uint8_t, 32> out{};
    NTSTATUS fst = BCryptFinishHash(hHash, out.data(), 32, 0);
    if (st != 0 || fst != 0) throw std::runtime_error("BCryptHash SHA256 failed");
    return win::hex_lower(out.data(), 32);
}

inline std::string request_hash(std::string_view url, std::string_view key_hex) {
    size_t hash_pos = url.find('#');
    std::string_view clean = (hash_pos == std::string_view::npos) ? url : url.substr(0, hash_pos);
    BCRYPT_HASH_HANDLE hHash = win::detail::thread_sha256_handle();
    NTSTATUS st = 0;
    if (!clean.empty()) st = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(clean.data())), static_cast<ULONG>(clean.size()), 0);
    if (st == 0 && !key_hex.empty()) st = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(key_hex.data())), static_cast<ULONG>(key_hex.size()), 0);
    std::array<uint8_t, 32> out{};
    NTSTATUS fst = BCryptFinishHash(hHash, out.data(), 32, 0);
    if (st != 0 || fst != 0) throw std::runtime_error("BCryptHash SHA256 failed");
    return win::hex_lower(out.data(), 32);
}

} // namespace seb
