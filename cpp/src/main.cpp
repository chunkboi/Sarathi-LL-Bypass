#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "cdp.hpp"
#include "json.hpp"
#include "plist.hpp"
#include "seb.hpp"
#include "win_crypto.hpp"

#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__clang__) && defined(__MINGW32__) && (defined(__x86_64__) || defined(_M_X64))
// MinGW's CRT does not define the MSVC Control Flow Guard dispatch pointers.
// Providing the fallback symbols with [[gnu::used]] keeps them alive through
// ThinLTO/LTO so `clang++ -mguard=cf -fuse-ld=lld` can link CFG-instrumented code.
extern "C" {
[[gnu::naked, gnu::used]] static void __guard_dispatch_icall_nop() {
    __asm__("jmp *%rax");
}
[[gnu::used]] static void __guard_check_icall_nop(uintptr_t) {}
[[gnu::used]] void* __guard_check_icall_fptr = reinterpret_cast<void*>(&__guard_check_icall_nop);
[[gnu::used]] void* __guard_dispatch_icall_fptr = reinterpret_cast<void*>(&__guard_dispatch_icall_nop);
}
#endif

static void enable_process_mitigations() {
    // 1. Block runtime dynamic code generation / VirtualProtect PAGE_EXECUTE_* (ACG)
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp{};
    dcp.ProhibitDynamicCode = 1;
    SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dcp, sizeof(dcp));

    // 2. Raise an exception immediately if an invalid or double-closed HANDLE is used
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY shcp{};
    shcp.RaiseExceptionOnInvalidHandleReference = 1;
    shcp.HandleExceptionsPermanentlyEnabled = 1;
    SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &shcp, sizeof(shcp));

    // 3. Block loading DLLs from remote network shares or untrusted low-integrity folders,
    //    and force System32 to be searched before the application directory.
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp{};
    ilp.NoRemoteImages = 1;
    ilp.NoLowMandatoryLabelImages = 1;
    ilp.PreferSystem32Images = 1;
    SetProcessMitigationPolicy(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

    // 4. Disable legacy Win32 extension points (AppInit_DLLs, global hooks)
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epdp{};
    epdp.DisableExtensionPoints = 1;
    SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));

    // 5. Require all dynamically loaded DLLs to be Microsoft-signed (safe because
    //    seb_keys.exe is statically linked with no external MinGW runtime DLLs).
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY bsp{};
    bsp.MicrosoftSignedOnly = 1;
    SetProcessMitigationPolicy(ProcessSignaturePolicy, &bsp, sizeof(bsp));

    // 6. Block loading untrusted non-system fonts
    PROCESS_MITIGATION_FONT_DISABLE_POLICY fdp{};
    fdp.DisableNonSystemFonts = 1;
    SetProcessMitigationPolicy(ProcessFontDisablePolicy, &fdp, sizeof(fdp));
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
    "[--no-cache] [--launch] [--verbose]\n";

[[noreturn]] static void usage_and_exit(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    std::cerr << USAGE;
    std::exit(2);
}

// Waits for and closes worker threads in the destructor. This keeps the task
// structures alive if unpacking/parsing the .seb file throws before the join.
class ThreadGroup {
public:
    ThreadGroup() = default;
    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    ~ThreadGroup() { join(); }

    HANDLE start(LPTHREAD_START_ROUTINE fn, void* arg) {
        HANDLE h = CreateThread(nullptr, 0, fn, arg, 0, nullptr);
        if (h) handles_.push_back(h);
        return h;
    }

    void join() {
        if (handles_.empty()) return;
        seb::nt::nt_wait_all(handles_.data(), static_cast<ULONG>(handles_.size()));
        for (HANDLE h : handles_) seb::nt::NtClose(h);
        handles_.clear();
    }

    bool empty() const { return handles_.empty(); }

private:
    std::vector<HANDLE> handles_;
};

static double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main() {
    enable_process_mitigations();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) usage_and_exit("CommandLineToArgvW failed");

    std::optional<std::wstring> seb_path;
    std::optional<std::wstring> root_arg;
    std::string password;
    std::string route_pattern = "https://sarathi.parivahan.gov.in/*";
    bool no_cache = false;
    bool launch = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring_view warg(argv[i]);
        if (warg == L"--root") {
            if (++i >= argc) usage_and_exit("--root requires a value");
            root_arg = std::wstring(argv[i]);
        } else if (warg.rfind(L"--root=", 0) == 0) {
            root_arg = std::wstring(warg.substr(7));
        } else if (warg == L"--password") {
            if (++i >= argc) usage_and_exit("--password requires a value");
            password = seb::win::wide_to_utf8(argv[i]);
        } else if (warg.rfind(L"--password=", 0) == 0) {
            password = seb::win::wide_to_utf8(warg.substr(11));
        } else if (warg == L"--route") {
            if (++i >= argc) usage_and_exit("--route requires a value");
            route_pattern = seb::win::wide_to_utf8(argv[i]);
        } else if (warg.rfind(L"--route=", 0) == 0) {
            route_pattern = seb::win::wide_to_utf8(warg.substr(8));
        } else if (warg == L"--no-cache") {
            no_cache = true;
        } else if (warg == L"--launch") {
            launch = true;
        } else if (warg == L"-v" || warg == L"--verbose") {
            verbose = true;
        } else if (warg == L"-h" || warg == L"--help") {
            LocalFree(argv);
            std::cout << USAGE;
            return 0;
        } else if (!warg.empty() && warg[0] == L'-') {
            std::string arg = seb::win::wide_to_utf8(warg);
            LocalFree(argv);
            usage_and_exit("unknown option: " + arg);
        } else {
            if (seb_path.has_value()) {
                std::string arg = seb::win::wide_to_utf8(warg);
                LocalFree(argv);
                usage_and_exit("unexpected positional argument: " + arg);
            }
            seb_path = std::wstring(warg);
        }
    }
    LocalFree(argv);

    if (!seb_path.has_value()) usage_and_exit("the following arguments are required: seb");
    if (!seb::is_file_w(seb_path->c_str())) usage_and_exit("file not found: " + seb::win::wide_to_utf8(*seb_path));
    if (route_pattern.rfind("https://", 0) != 0 && route_pattern.rfind("http://", 0) != 0) {
        usage_and_exit("--route must start with http:// or https://");
    }

    wchar_t exe_buf[MAX_PATH]{};
    DWORD mod_len = GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    std::wstring exe_dir(seb::parent_path_w(std::wstring_view(exe_buf, mod_len)));
    std::wstring cache_path = seb::join_path_w(exe_dir, L".seb_tree_cache.json");
    std::wstring profile_dir = seb::join_path_w(exe_dir, L".seb_chrome_profile");

    std::string version;
    std::string config_key;
    std::string browser_key;
    std::string start_url;

    try {
        std::wstring root = root_arg.has_value() ? *root_arg : seb::detect_seb_root();
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
                auto bytes = seb::win::read_file_bytes_w(cache_path.c_str());
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
                        uint64_t parsed = 0;
                        auto [ptr, ec] = std::from_chars(v->as_str()->data(), v->as_str()->data() + v->as_str()->size(), parsed);
                        return ec == std::errc{} && parsed == expected;
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
                    v_ver && v_ver->as_str() &&
                    v_salt && v_salt->as_str() &&
                    v_tree && v_tree->as_str()) {
                    try {
                        cached_tree = seb::win::base64_decode(*v_tree->as_str());
                        cached_version = *v_ver->as_str();
                        cached_salt_const = *v_salt->as_str();
                        install_cached = !cached_tree.empty() &&
                                         !cached_version.empty() &&
                                         !cached_salt_const.empty();
                        // New caches carry a checksum for the tree blob. If it
                        // does not match, treat the install cache as a miss
                        // rather than silently deriving a wrong browser key.
                        if (install_cached && v_tree_hash && v_tree_hash->as_str() &&
                            seb::win::sha256_hex(cached_tree.data(), cached_tree.size()) !=
                                *v_tree_hash->as_str()) {
                            install_cached = false;
                            cached_tree.clear();
                            cache_state = "miss";
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
                    if (v_ck && v_ck->as_str() && v_bk && v_bk->as_str() &&
                        v_su && v_su->as_str()) {
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
            try {
                raw = seb::win::read_file_bytes_w(seb_path->c_str());
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
                auto settings = seb::plist::load_settings_xml_bytes(xml);
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
                    return 0;
                }, &ver_task);
                const HANDLE th_salt = workers.start([](LPVOID p) -> DWORD {
                    auto* t = static_cast<SaltTask*>(p);
                    try { t->result = seb::extract_salt_const(*t->root); }
                    catch (const std::exception& e) { t->error = e.what(); }
                    return 0;
                }, &salt_task);
                const HANDLE th_tree = workers.start([](LPVOID p) -> DWORD {
                    auto* t = static_cast<TreeTask*>(p);
                    try { t->result = seb::tree_fingerprint_bytes(*t->root); }
                    catch (const std::exception& e) { t->error = e.what(); }
                    return 0;
                }, &tree_task);

                // Thread creation can fail under extreme handle pressure; run
                // the work inline in that case so the result is still produced.
                if (!th_ver) ver_task.result = seb::detect_seb_version(root);
                if (!th_salt) salt_task.result = seb::extract_salt_const(root);
                if (!th_tree) tree_task.result = seb::tree_fingerprint_bytes(root);

                parse_exam();
                workers.join();
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
                    seb::win::write_file_bytes_w(cache_path.c_str(), out_json.data(), out_json.size());
                } catch (...) {}
            }
        }
    } catch (const std::exception& ex) {
        if (!password.empty()) {
            SecureZeroMemory(password.data(), password.size());
            password.clear();
        }
        usage_and_exit(ex.what());
    }

    if (!password.empty()) {
        SecureZeroMemory(password.data(), password.size());
        password.clear();
    }

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
