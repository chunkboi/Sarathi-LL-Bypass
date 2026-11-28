#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "cdp.hpp"
#include "json.hpp"
#include "plist.hpp"
#include "seb.hpp"
#include "win_crypto.hpp"

#include <aclapi.h>
#include <shellapi.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__clang__) && defined(__MINGW32__) && (defined(__x86_64__) || defined(_M_X64))
// MinGW's CRT does not provide _load_config_used or the Control Flow Guard
// dispatch pointers. Define the COFF IMAGE_LOAD_CONFIG_DIRECTORY64 so lld
// populates the PE Load Configuration Directory and Windows ntdll activates CFG.
// Under GCC-built MinGW-w64 11.1.0, crt2.o and libstdc++.a lack .gfids$y tables,
// so GuardFlags uses IMAGE_GUARD_CF_INSTRUMENTED (0x100) by default (define
// SEB_CFG_FUNCTION_TABLE=1 when building against an llvm-mingw CRT/libc++
// compiled with -mguard=cf to enable 0x10500).
extern "C" {
extern void* __guard_fids_table[];
extern char __guard_fids_count[];
extern void* __guard_iat_table[];
extern char __guard_iat_count[];
extern void* __guard_longjmp_table[];
extern char __guard_longjmp_count[];

[[gnu::naked, gnu::used]] void __guard_dispatch_icall_nop() {
    __asm__("jmp *%rax");
}
[[gnu::used]] void __guard_check_icall_nop(uintptr_t) {}

[[gnu::used, gnu::section(".00cfg")]] void* __guard_check_icall_fptr =
    reinterpret_cast<void*>(&__guard_check_icall_nop);
[[gnu::used, gnu::section(".00cfg")]] void* __guard_dispatch_icall_fptr =
    reinterpret_cast<void*>(&__guard_dispatch_icall_nop);
[[gnu::used]] uint64_t __security_cookie = 0x00002B992DDFA232ULL;

struct alignas(8) CoffLoadConfig64 {
    uint32_t Size;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t GlobalFlagsClear;
    uint32_t GlobalFlagsSet;
    uint32_t CriticalSectionDefaultTimeout;
    uint64_t DeCommitFreeBlockThreshold;
    uint64_t DeCommitTotalFreeThreshold;
    uint64_t LockPrefixTable;
    uint64_t MaximumAllocationSize;
    uint64_t VirtualMemoryThreshold;
    uint64_t ProcessAffinityMask;
    uint32_t ProcessHeapFlags;
    uint16_t CSDVersion;
    uint16_t DependentLoadFlags;
    uint64_t EditList;
    uint64_t SecurityCookie;
    uint64_t SEHandlerTable;
    uint64_t SEHandlerCount;
    uint64_t GuardCFCheckFunctionPointer;
    uint64_t GuardCFDispatchFunctionPointer;
    uint64_t GuardCFFunctionTable;
    uint64_t GuardCFFunctionCount;
    uint32_t GuardFlags;
    uint8_t CodeIntegrity[12];
    uint64_t GuardAddressTakenIatEntryTable;
    uint64_t GuardAddressTakenIatEntryCount;
    uint64_t GuardLongJumpTargetTable;
    uint64_t GuardLongJumpTargetCount;
};

#ifdef SEB_CFG_FUNCTION_TABLE
inline constexpr uint32_t SEB_GUARD_FLAGS = 0x00010500u;
#else
inline constexpr uint32_t SEB_GUARD_FLAGS = 0x00000100u;
#endif

extern const CoffLoadConfig64 _load_config_used;
[[gnu::used]] const CoffLoadConfig64 _load_config_used = {
    sizeof(CoffLoadConfig64),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    reinterpret_cast<uint64_t>(&__security_cookie),
    0, 0,
    reinterpret_cast<uint64_t>(&__guard_check_icall_fptr),
    reinterpret_cast<uint64_t>(&__guard_dispatch_icall_fptr),
    reinterpret_cast<uint64_t>(__guard_fids_table),
    reinterpret_cast<uint64_t>(__guard_fids_count),
    SEB_GUARD_FLAGS,
    {},
    reinterpret_cast<uint64_t>(__guard_iat_table),
    reinterpret_cast<uint64_t>(__guard_iat_count),
    reinterpret_cast<uint64_t>(__guard_longjmp_table),
    reinterpret_cast<uint64_t>(__guard_longjmp_count),
};
}
#endif

static void enable_process_mitigations(std::vector<std::string>& warnings) {
    auto warn_fail = [&](const char* name) {
        warnings.push_back(std::string(name) + " failed (err=" + std::to_string(GetLastError()) + ")");
    };

    // 1. Block runtime dynamic code generation / VirtualProtect PAGE_EXECUTE_* (ACG)
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp{};
    dcp.ProhibitDynamicCode = 1;
    if (!SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dcp, sizeof(dcp))) {
        warn_fail("ProcessDynamicCodePolicy");
    }

    // 2. Raise an exception immediately if an invalid or double-closed HANDLE is used
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY shcp{};
    shcp.RaiseExceptionOnInvalidHandleReference = 1;
    shcp.HandleExceptionsPermanentlyEnabled = 1;
    if (!SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &shcp, sizeof(shcp))) {
        warn_fail("ProcessStrictHandleCheckPolicy");
    }

    // 3. Block loading DLLs from remote network shares or untrusted low-integrity folders,
    //    and force System32 to be searched before the application directory.
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp{};
    ilp.NoRemoteImages = 1;
    ilp.NoLowMandatoryLabelImages = 1;
    ilp.PreferSystem32Images = 1;
    if (!SetProcessMitigationPolicy(ProcessImageLoadPolicy, &ilp, sizeof(ilp))) {
        warn_fail("ProcessImageLoadPolicy");
    }

    // 4. Disable legacy Win32 extension points (AppInit_DLLs, global hooks)
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epdp{};
    epdp.DisableExtensionPoints = 1;
    if (!SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp))) {
        warn_fail("ProcessExtensionPointDisablePolicy");
    }

    // 5. Require all dynamically loaded DLLs to be Microsoft-signed (safe because
    //    seb_keys.exe is statically linked with no external MinGW runtime DLLs).
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY bsp{};
    bsp.MicrosoftSignedOnly = 1;
    if (!SetProcessMitigationPolicy(ProcessSignaturePolicy, &bsp, sizeof(bsp))) {
        warn_fail("ProcessSignaturePolicy");
    }

    // 6. Block loading untrusted non-system fonts
    PROCESS_MITIGATION_FONT_DISABLE_POLICY fdp{};
    fdp.DisableNonSystemFonts = 1;
    if (!SetProcessMitigationPolicy(ProcessFontDisablePolicy, &fdp, sizeof(fdp))) {
        warn_fail("ProcessFontDisablePolicy");
    }

    // Note: High-entropy ASLR is enforced at image load via PE flags
    // (--dynamicbase --high-entropy-va); ProcessASLRPolicy is invalid post-init.

    // 7. Terminate immediately on Windows heap metadata corruption
    if (!HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0)) {
        warn_fail("HeapEnableTerminationOnCorruption");
    }

    // 8. Restrict dynamic DLL search path to System32 only
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        warn_fail("SetDefaultDllDirectories");
    }

    // 9. Restrict current process DACL to strip PROCESS_VM_READ/WRITE/DUP_HANDLE
    //    from external non-elevated processes while keeping PROCESS_TERMINATE
    //    and SYNCHRONIZE intact for normal CLI/parent termination.
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        warn_fail("OpenProcessToken");
        return;
    }
    seb::nt::NtHandle tok_h(tok);
    DWORD sz = 0;
    (void)GetTokenInformation(tok_h.get(), TokenUser, nullptr, 0, &sz);
    if (sz == 0) {
        warn_fail("GetTokenInformation(size)");
        return;
    }
    std::vector<uint8_t> ubuf(sz);
    if (!GetTokenInformation(tok_h.get(), TokenUser, ubuf.data(), sz, &sz)) {
        warn_fail("GetTokenInformation");
        return;
    }
    auto* tu = reinterpret_cast<TOKEN_USER*>(ubuf.data());
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(tu->User.Sid);
    PACL dacl = nullptr;
    DWORD acl_rc = SetEntriesInAclW(1, &ea, nullptr, &dacl);
    if (acl_rc != ERROR_SUCCESS || !dacl) {
        warnings.push_back("SetEntriesInAclW failed (rc=" + std::to_string(acl_rc) + ")");
        return;
    }
    DWORD sec_rc = SetSecurityInfo(
        GetCurrentProcess(), SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, dacl, nullptr);
    LocalFree(dacl);
    if (sec_rc != ERROR_SUCCESS) {
        warnings.push_back("SetSecurityInfo failed (rc=" + std::to_string(sec_rc) + ")");
    }
}

static std::pair<uint64_t, uint64_t> file_mtime_and_size_w(const wchar_t* p) {
    uint64_t unix_ns = 0, sz = 0;
    if (!seb::nt::nt_stat_w(p, unix_ns, sz)) {
        throw std::runtime_error("failed to stat " + seb::win::wide_to_utf8(p));
    }
    return {unix_ns, sz};
}

static std::wstring full_path_w(const wchar_t* p) {
    DWORD needed = GetFullPathNameW(p, 0, nullptr, nullptr);
    if (needed == 0) return p;
    std::wstring out(needed, L'\0');
    DWORD written = GetFullPathNameW(p, needed, out.data(), nullptr);
    if (written == 0) return p;
    out.resize(written);
    return out;
}

inline constexpr const char* USAGE =
    "usage: seb_keys <seb> [--root <path>] [--password <pw>] [--route <pattern>] "
    "[--no-cache] [--launch] [-v|--verbose] [-h|--help]\n";

static int print_usage_err(std::string_view msg) {
    std::cerr << "error: " << msg << "\n" << USAGE;
    return 2;
}

// Waits for and closes worker threads in the destructor. This keeps the task
// structures alive if unpacking/parsing the .seb file throws before the join.
class ThreadGroup {
public:
    ThreadGroup() = default;
    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    ~ThreadGroup() { join_nothrow(); }

    HANDLE start(LPTHREAD_START_ROUTINE fn, void* arg) {
        seb::nt::NtHandle h(CreateThread(nullptr, 0, fn, arg, 0, nullptr));
        if (!h) return nullptr;
        HANDLE raw = h.get();
        handles_.push_back(std::move(h));
        return raw;
    }

    void join_checked() {
        if (handles_.empty()) return;
        std::vector<HANDLE> raw;
        raw.reserve(handles_.size());
        for (const auto& h : handles_) raw.push_back(h.get());
        const bool ok = seb::nt::nt_wait_all(raw.data(), static_cast<ULONG>(raw.size()));
        handles_.clear();
        if (!ok) {
            throw std::runtime_error("nt_wait_all failed waiting for worker threads");
        }
    }

    void join_nothrow() noexcept {
        if (handles_.empty()) return;
        std::vector<HANDLE> raw;
        raw.reserve(handles_.size());
        for (const auto& h : handles_) raw.push_back(h.get());
        (void)seb::nt::nt_wait_all(raw.data(), static_cast<ULONG>(raw.size()));
        handles_.clear();
    }

    bool empty() const { return handles_.empty(); }

private:
    std::vector<seb::nt::NtHandle> handles_;
};

static double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

static std::wstring get_module_path_w() {
    std::vector<wchar_t> buf(1024);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            throw std::runtime_error("GetModuleFileNameW failed (err=" + std::to_string(GetLastError()) + ")");
        }
        if (len < buf.size()) {
            return std::wstring(buf.data(), len);
        }
        if (buf.size() >= 32768) {
            throw std::runtime_error("executable path exceeds 32768 wchars");
        }
        buf.resize(buf.size() * 2);
    }
}

static bool is_lower_hex_len(std::string_view s, size_t expected_len) noexcept {
    if (s.size() != expected_len) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool is_valid_version_str(std::string_view s) noexcept {
    if (s.empty() || s.size() > 64) return false;
    bool prev_dot = true;
    for (char c : s) {
        if (c == '.') {
            if (prev_dot) return false;
            prev_dot = true;
        } else if (c >= '0' && c <= '9') {
            prev_dot = false;
        } else {
            return false;
        }
    }
    return !prev_dot;
}

static bool is_valid_http_url(std::string_view s) noexcept {
    if (!(s.starts_with("https://") || s.starts_with("http://"))) return false;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c == 0x7F) return false;
    }
    return true;
}

struct ArgvGuard {
    LPWSTR* argv = nullptr;
    int argc = 0;

    ~ArgvGuard() {
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (!argv[i]) continue;
                std::wstring_view w(argv[i]);
                if (w == L"--password" && i + 1 < argc && argv[i + 1]) {
                    SecureZeroMemory(argv[i + 1], std::wcslen(argv[i + 1]) * sizeof(wchar_t));
                } else if (w.starts_with(L"--password=")) {
                    SecureZeroMemory(argv[i], w.size() * sizeof(wchar_t));
                }
            }
            LocalFree(argv);
        }
    }
    ArgvGuard() = default;
    ArgvGuard(const ArgvGuard&) = delete;
    ArgvGuard& operator=(const ArgvGuard&) = delete;
};

int main() {
    std::vector<std::string> mitigation_warnings;
    enable_process_mitigations(mitigation_warnings);

    std::optional<std::wstring> seb_path;
    std::optional<std::wstring> root_arg;
    std::string password;
    seb::win::SecureStringGuard pwd_wipe_guard(password);
    std::string route_pattern = "https://sarathi.parivahan.gov.in/*";
    bool no_cache = false;
    bool launch = false;
    bool verbose = false;

    {
        ArgvGuard ag;
        ag.argv = CommandLineToArgvW(GetCommandLineW(), &ag.argc);
        if (!ag.argv) return print_usage_err("CommandLineToArgvW failed");

        for (int i = 1; i < ag.argc; ++i) {
            std::wstring_view warg(ag.argv[i]);
            if (warg == L"--root") {
                if (++i >= ag.argc) return print_usage_err("--root requires a value");
                root_arg = std::wstring(ag.argv[i]);
            } else if (warg.starts_with(L"--root=")) {
                root_arg = std::wstring(warg.substr(7));
            } else if (warg == L"--password") {
                if (++i >= ag.argc) return print_usage_err("--password requires a value");
                password = seb::win::wide_to_utf8(ag.argv[i]);
            } else if (warg.starts_with(L"--password=")) {
                password = seb::win::wide_to_utf8(warg.substr(11));
            } else if (warg == L"--route") {
                if (++i >= ag.argc) return print_usage_err("--route requires a value");
                route_pattern = seb::win::wide_to_utf8(ag.argv[i]);
            } else if (warg.starts_with(L"--route=")) {
                route_pattern = seb::win::wide_to_utf8(warg.substr(8));
            } else if (warg == L"--no-cache") {
                no_cache = true;
            } else if (warg == L"--launch") {
                launch = true;
            } else if (warg == L"-v" || warg == L"--verbose") {
                verbose = true;
            } else if (warg == L"-h" || warg == L"--help") {
                std::cout << USAGE;
                return 0;
            } else if (!warg.empty() && warg[0] == L'-') {
                return print_usage_err("unknown option: " + seb::win::wide_to_utf8(warg));
            } else {
                if (seb_path.has_value()) {
                    return print_usage_err("unexpected positional argument: " + seb::win::wide_to_utf8(warg));
                }
                seb_path = std::wstring(warg);
            }
        }
    }

    if (verbose) {
        for (const auto& w : mitigation_warnings) {
            std::cerr << "[mitigation] WARNING: " << w << "\n";
        }
    }

    if (!seb_path.has_value()) return print_usage_err("the following arguments are required: seb");
    if (!seb::is_file_w(seb_path->c_str())) return print_usage_err("file not found: " + seb::win::wide_to_utf8(*seb_path));
    if (!std::string_view(route_pattern).starts_with("https://") &&
        !std::string_view(route_pattern).starts_with("http://")) {
        return print_usage_err("--route must start with http:// or https://");
    }

    std::wstring exe_path = get_module_path_w();
    std::wstring exe_dir(seb::parent_path_w(exe_path));
    std::wstring cache_path = seb::join_path_w(exe_dir, L".seb_tree_cache.json");
    std::wstring profile_dir = seb::join_path_w(exe_dir, L".seb_chrome_profile");

    std::string version;
    std::string config_key;
    std::string browser_key;
    std::string start_url;

    try {
        std::wstring root = root_arg.has_value() ? *root_arg : seb::detect_seb_root();
        seb::win::MemoryLockGuard pwd_lock(password.empty() ? nullptr : password.data(), password.size());
        auto [seb_mtime, seb_size] = file_mtime_and_size_w(seb_path->c_str());
        if (seb_size > 64ULL * 1024 * 1024) {
            throw std::runtime_error("input .seb file exceeds 64 MiB limit");
        }
        std::string pwd_hash = seb::win::sha256_hex(password);
        std::wstring exe = seb::find_seb_exe(root);
        auto [exe_mtime, exe_size] = file_mtime_and_size_w(exe.c_str());

        std::string root_key = seb::win::wide_to_utf8(full_path_w(root.c_str()));
        std::string seb_abs = seb::win::wide_to_utf8(full_path_w(seb_path->c_str()));

        bool install_cached = false;
        bool exam_cached = false;
        std::string cached_version;
        std::string cached_salt_const;
        std::vector<uint8_t> cached_tree;
        std::string cache_state = "miss";

        if (no_cache) {
            if (verbose) std::cerr << "[seb] cache=disabled root=" << root_key << "\n";
        } else if (seb::is_file_w(cache_path.c_str())) {
            try {
                uint64_t cache_mt = 0, cache_sz = 0;
                if (!seb::nt::nt_stat_w(cache_path.c_str(), cache_mt, cache_sz) || cache_sz > 16ULL * 1024 * 1024) {
                    throw std::runtime_error("cache file invalid or too large");
                }
                auto bytes = seb::nt::nt_read_file_bytes_w(cache_path.c_str(), 16ULL * 1024 * 1024);
                auto cached = seb::json::parse(
                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                const auto* last = cached.get("last_seb");
                auto str_eq = [](const seb::json::JsonVal* v, const std::string& expected) {
                    return v && v->as_str() && *v->as_str() == expected;
                };
                auto u64_eq = [](const seb::json::JsonVal* v, uint64_t expected) {
                    uint64_t got = 0;
                    if (v && v->get_u64(got)) return got == expected;
                    if (v && v->as_str()) {
                        const std::string& s = *v->as_str();
                        if (s.empty()) return false;
                        uint64_t parsed = 0;
                        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                        return ec == std::errc{} && ptr == s.data() + s.size() && parsed == expected;
                    }
                    return false;
                };

                const auto* v_ver = cached.get("seb_version");
                const auto* v_salt = cached.get("integrity_salt");
                const auto* v_tree = cached.get("prefix_b64");
                if (!v_tree) v_tree = cached.get("tree_b64");
                const auto* v_tree_hash = cached.get("tree_sha256");

                if (str_eq(cached.get("root"), root_key) &&
                    u64_eq(cached.get("mtime_ns"), exe_mtime) &&
                    u64_eq(cached.get("size"), exe_size) &&
                    v_ver && v_ver->as_str() && is_valid_version_str(*v_ver->as_str()) &&
                    v_salt && v_salt->as_str() && is_lower_hex_len(*v_salt->as_str(), 40) &&
                    v_tree && v_tree->as_str()) {
                    try {
                        cached_tree = seb::win::base64_decode(*v_tree->as_str());
                        cached_version = *v_ver->as_str();
                        cached_salt_const = *v_salt->as_str();
                        install_cached = !cached_tree.empty();
                        if (install_cached && v_tree_hash) {
                            if (!v_tree_hash->as_str() ||
                                !is_lower_hex_len(*v_tree_hash->as_str(), 64) ||
                                seb::win::sha256_hex(cached_tree.data(), cached_tree.size()) !=
                                    *v_tree_hash->as_str()) {
                                install_cached = false;
                                cached_tree.clear();
                                cache_state = "miss";
                            }
                        }
                    } catch (...) {
                        install_cached = false;
                        cached_tree.clear();
                        cache_state = "miss";
                    }
                }

                if (install_cached && last &&
                    str_eq(last->get("path"), seb_abs) &&
                    u64_eq(last->get("mtime_ns"), seb_mtime) &&
                    u64_eq(last->get("size"), seb_size) &&
                    str_eq(last->get("pwd_hash"), pwd_hash)) {
                    const auto* v_ck = last->get("config_key");
                    const auto* v_bk = last->get("browser_key");
                    const auto* v_su = last->get("start_url");
                    if (v_ck && v_ck->as_str() && is_lower_hex_len(*v_ck->as_str(), 64) &&
                        v_bk && v_bk->as_str() && is_lower_hex_len(*v_bk->as_str(), 64) &&
                        v_su && v_su->as_str() && is_valid_http_url(*v_su->as_str())) {
                        version = cached_version;
                        config_key = *v_ck->as_str();
                        browser_key = *v_bk->as_str();
                        start_url = *v_su->as_str();
                        exam_cached = true;
                        cache_state = "hit";
                    }
                }
                if (!exam_cached && install_cached) cache_state = "install-hit";
            } catch (...) {
                install_cached = false;
                exam_cached = false;
                cached_tree.clear();
                cache_state = "miss";
            }
            if (verbose) {
                std::cerr << "[seb] cache=" << cache_state << " root=" << root_key;
                if (install_cached) std::cerr << " version=" << cached_version;
                std::cerr << "\n";
            }
        } else if (verbose) {
            std::cerr << "[seb] cache=miss root=" << root_key << "\n";
        }

        if (!exam_cached) {
            // Read and sanity-check the .seb once, before the expensive tree
            // scan starts. An obviously invalid file now fails in a few
            // milliseconds instead of waiting for the full fingerprint.
            std::vector<uint8_t> raw;
            seb::win::SecureVectorGuard raw_guard(raw);
            try {
                raw = seb::nt::nt_read_file_bytes_w(seb_path->c_str(), 64ULL * 1024 * 1024);
            } catch (const std::exception& e) {
                throw std::runtime_error("failed to read " + seb::win::wide_to_utf8(*seb_path) +
                                         ": " + e.what());
            }
            if (!seb::looks_like_seb_container(
                    std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()))) {
                throw std::runtime_error("input is not a valid SEB configuration");
            }

            std::string salt;
            std::string salt_const;
            std::vector<uint8_t> tree;
            double parse_ms = 0.0;

            auto parse_exam = [&] {
                const auto parse_start = std::chrono::steady_clock::now();
                auto [xml, mode] = seb::unpack_container(std::move(raw), password);
                seb::win::SecureVectorGuard xml_guard(xml);
                auto settings = seb::plist::load_settings_xml_bytes(xml);
                seb::win::wipe_vector(xml);
                auto purpose = seb::detect_purpose(mode, settings);
                salt = seb::salt_hex(settings);
                config_key = seb::plist::exam_configuration_key(settings, purpose);
                start_url = seb::get_start_url(settings).value_or(seb::cdp::DEFAULT_URL);
                parse_ms = elapsed_ms(parse_start);
            };

            if (install_cached) {
                // The SEB installation is unchanged, so the expensive tree
                // fingerprint (and its version/salt) can be reused. Only the
                // new .seb file has to be parsed.
                version = cached_version;
                salt_const = cached_salt_const;
                tree = std::move(cached_tree);
                parse_exam();
                if (verbose) {
                    std::cerr << "[seb] reused_tree=" << tree.size()
                              << " bytes config_parse=" << parse_ms << "ms\n";
                }
            } else {
                struct VerTask {
                    const std::wstring* root;
                    std::string result;
                    std::string error;
                } ver_task{&root, {}, {}};

                struct SaltTask {
                    const std::wstring* root;
                    std::string result;
                    std::string error;
                } salt_task{&root, {}, {}};

                struct TreeTask {
                    const std::wstring* root;
                    std::vector<uint8_t> result;
                    std::string error;
                } tree_task{&root, {}, {}};

                const auto analysis_start = std::chrono::steady_clock::now();
                ThreadGroup workers;
                const HANDLE th_ver = workers.start([](LPVOID p) -> DWORD {
                    auto* t = static_cast<VerTask*>(p);
                    try { t->result = seb::detect_seb_version(*t->root); }
                    catch (const std::exception& e) { t->error = e.what(); }
                    catch (...) { t->error = "unknown worker error"; }
                    return 0;
                }, &ver_task);
                const HANDLE th_salt = workers.start([](LPVOID p) -> DWORD {
                    auto* t = static_cast<SaltTask*>(p);
                    try { t->result = seb::extract_salt_const(*t->root); }
                    catch (const std::exception& e) { t->error = e.what(); }
                    catch (...) { t->error = "unknown worker error"; }
                    return 0;
                }, &salt_task);
                const HANDLE th_tree = workers.start([](LPVOID p) -> DWORD {
                    auto* t = static_cast<TreeTask*>(p);
                    try { t->result = seb::tree_fingerprint_bytes(*t->root); }
                    catch (const std::exception& e) { t->error = e.what(); }
                    catch (...) { t->error = "unknown worker error"; }
                    return 0;
                }, &tree_task);

                // Thread creation can fail under extreme handle pressure; run
                // the work inline in that case so the result is still produced.
                if (!th_ver) ver_task.result = seb::detect_seb_version(root);
                if (!th_salt) salt_task.result = seb::extract_salt_const(root);
                if (!th_tree) tree_task.result = seb::tree_fingerprint_bytes(root);

                parse_exam();
                workers.join_checked();
                if (verbose) {
                    std::cerr << "[seb] config_parse=" << parse_ms
                              << "ms analysis_total=" << elapsed_ms(analysis_start) << "ms\n";
                }

                if (!ver_task.error.empty()) throw std::runtime_error(ver_task.error);
                if (!salt_task.error.empty()) throw std::runtime_error(salt_task.error);
                if (!tree_task.error.empty()) throw std::runtime_error(tree_task.error);

                version = std::move(ver_task.result);
                salt_const = std::move(salt_task.result);
                tree = std::move(tree_task.result);
            }

            browser_key = seb::compute_browser_key(tree, config_key, salt, salt_const);

            if (!no_cache) {
                try {
                    seb::json::JsonObj last_seb;
                    last_seb.emplace_back("path", seb_abs);
                    last_seb.emplace_back("mtime_ns", seb_mtime);
                    last_seb.emplace_back("size", seb_size);
                    last_seb.emplace_back("pwd_hash", pwd_hash);
                    last_seb.emplace_back("config_key", config_key);
                    last_seb.emplace_back("browser_key", browser_key);
                    last_seb.emplace_back("start_url", start_url);

                    seb::json::JsonObj payload;
                    payload.emplace_back("root", root_key);
                    payload.emplace_back("mtime_ns", exe_mtime);
                    payload.emplace_back("size", exe_size);
                    payload.emplace_back("seb_version", version);
                    payload.emplace_back("integrity_salt", salt_const);
                    // Keep the tree fingerprint so a new .seb file can reuse it
                    // without re-hashing the entire SEB installation. The
                    // checksum lets later runs reject a corrupted tree blob.
                    payload.emplace_back("prefix_b64", seb::win::base64_encode(tree.data(), tree.size()));
                    payload.emplace_back("tree_sha256", seb::win::sha256_hex(tree.data(), tree.size()));
                    payload.emplace_back("last_seb", std::move(last_seb));

                    std::string out_json = seb::json::stringify(seb::json::JsonVal(std::move(payload)));
                    if (!seb::win::write_file_bytes_w(cache_path.c_str(), out_json.data(), out_json.size()) && verbose) {
                        std::cerr << "[seb] WARNING: failed to write cache file\n";
                    }
                } catch (...) {}
            }
        }
    } catch (const std::exception& ex) {
        return print_usage_err(ex.what());
    }

    seb::win::wipe_string(password);

    std::cout << "configKey=" << config_key << "\n"
              << "browserExamKey=" << browser_key << "\n" << std::flush;

    if (launch) {
        try {
            seb::cdp::launch_cdp_browser(start_url, version, config_key, browser_key, profile_dir, route_pattern, verbose);
        } catch (const std::exception& ex) {
            std::cerr << "launch error: " << ex.what() << "\n";
            return 1;
        }
    }
    return 0;
}
