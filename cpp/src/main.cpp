#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "cdp.hpp"
#include "json.hpp"
#include "plist.hpp"
#include "seb.hpp"
#include "win_crypto.hpp"

#include <windows.h>
#include <aclapi.h>

#include <algorithm>
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

namespace {

constexpr DWORD kStatusInvalidHandle      = 0xC0000008UL; // StrictHandleCheck
constexpr DWORD kStatusAccessDenied       = 0xC0000022UL; // Win32k syscall / policy block
constexpr DWORD kStatusInvalidImageHash   = 0xC0000428UL; // MicrosoftSignedOnly
constexpr DWORD kStatusDynamicCodeBlocked = 0xC0000604UL; // ACG / ProhibitDynamicCode
constexpr DWORD kDelayLoadModNotFound     = 0xC06D007EUL; // Delay-load DLL blocked/missing
constexpr DWORD kDelayLoadProcNotFound    = 0xC06D007FUL; // Delay-load proc missing

// Stack-only stderr writer safe to call from a Vectored Exception Handler.
void veh_write_stderr(const char* data, size_t len) noexcept {
    HANDLE err_h = GetStdHandle(STD_ERROR_HANDLE);
    if (!err_h || err_h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(err_h, data, static_cast<DWORD>(len), &written, nullptr);
}

void veh_write_cstr(const char* s) noexcept {
    size_t n = 0;
    while (s[n] != '\0') ++n;
    veh_write_stderr(s, n);
}

void veh_write_hex(uintptr_t val, int digits) noexcept {
    static constexpr char kHex[] = "0123456789ABCDEF";
    char buf[18];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < digits; ++i) {
        int shift = (digits - 1 - i) * 4;
        buf[2 + i] = kHex[(val >> shift) & 0xF];
    }
    veh_write_stderr(buf, static_cast<size_t>(digits + 2));
}

LONG CALLBACK mitigation_veh(PEXCEPTION_POINTERS ep) noexcept {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const char* reason = nullptr;

    switch (code) {
        case kStatusInvalidHandle:
            reason = "StrictHandleCheck (invalid or double-closed HANDLE)";
            break;
        case kStatusAccessDenied:
            reason = "Access denied (possible DisallowWin32kSystemCalls / policy trap)";
            break;
        case kStatusInvalidImageHash:
            reason = "Binary signature policy (MicrosoftSignedOnly rejected a DLL)";
            break;
        case kStatusDynamicCodeBlocked:
            reason = "Dynamic code policy (ACG blocked executable memory)";
            break;
        case kDelayLoadModNotFound:
            reason = "Delay-load DLL failed (possible MicrosoftSignedOnly / System32-only block)";
            break;
        case kDelayLoadProcNotFound:
            reason = "Delay-load procedure lookup failed";
            break;
        default:
            // Do not interfere with C++ exceptions (0xE06D7363) or normal runtime exceptions.
            return EXCEPTION_CONTINUE_SEARCH;
    }

    veh_write_cstr("[mitigation] FATAL TRAP code=");
    veh_write_hex(code, 8);
    veh_write_cstr(" addr=");
    veh_write_hex(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress),
                  static_cast<int>(sizeof(uintptr_t) * 2));
    veh_write_cstr(" : ");
    veh_write_cstr(reason);
    veh_write_cstr("\n");

    return EXCEPTION_CONTINUE_SEARCH;
}

struct VehGuard {
    PVOID handle = nullptr;
    VehGuard() noexcept : handle(AddVectoredExceptionHandler(1, mitigation_veh)) {}
    ~VehGuard() {
        if (handle) RemoveVectoredExceptionHandler(handle);
    }
    VehGuard(const VehGuard&) = delete;
    VehGuard& operator=(const VehGuard&) = delete;
};

struct LocalAclGuard {
    PACL dacl = nullptr;
    ~LocalAclGuard() {
        if (dacl) LocalFree(dacl);
    }
    LocalAclGuard() = default;
    LocalAclGuard(const LocalAclGuard&) = delete;
    LocalAclGuard& operator=(const LocalAclGuard&) = delete;
};

} // namespace

static std::vector<uint8_t> get_token_user_sid_bytes(HANDLE tok, std::vector<std::string>* warnings = nullptr) {
    auto warn = [&](const std::string& msg) {
        if (warnings) warnings->push_back(msg);
    };

    DWORD sz = 0;
    if (!GetTokenInformation(tok, TokenUser, nullptr, 0, &sz) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        warn("GetTokenInformation(TokenUser, size) failed (err=" + std::to_string(GetLastError()) + ")");
        return {};
    }
    if (sz == 0) {
        warn("GetTokenInformation(TokenUser, size) returned 0");
        return {};
    }

    std::vector<uint8_t> ubuf(sz);
    if (!GetTokenInformation(tok, TokenUser, ubuf.data(), sz, &sz)) {
        warn("GetTokenInformation(TokenUser) failed (err=" + std::to_string(GetLastError()) + ")");
        return {};
    }

    auto* tu = reinterpret_cast<TOKEN_USER*>(ubuf.data());
    DWORD sid_len = GetLengthSid(tu->User.Sid);
    std::vector<uint8_t> sid(sid_len);
    if (!CopySid(sid_len, sid.data(), tu->User.Sid)) {
        warn("CopySid failed (err=" + std::to_string(GetLastError()) + ")");
        return {};
    }
    return sid;
}

static bool build_user_only_dacl(
    PSID user_sid,
    DWORD access_mask,
    DWORD inheritance,
    LocalAclGuard& out_acl,
    DWORD* out_rc = nullptr
) {
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = access_mask;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = inheritance;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(user_sid);

    DWORD rc = SetEntriesInAclW(1, &ea, nullptr, &out_acl.dacl);
    if (out_rc) *out_rc = rc;
    return rc == ERROR_SUCCESS && out_acl.dacl != nullptr;
}

static bool harden_thread_dacl(HANDLE thread_h, PSID user_sid, std::vector<std::string>* warnings = nullptr) {
    if (!thread_h || !user_sid) return false;
    LocalAclGuard acl;
    DWORD acl_rc = ERROR_SUCCESS;
    if (!build_user_only_dacl(
            user_sid,
            SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION,
            NO_INHERITANCE,
            acl,
            &acl_rc)) {
        if (warnings) warnings->push_back("thread SetEntriesInAclW failed (rc=" + std::to_string(acl_rc) + ")");
        return false;
    }
    DWORD sec_rc = SetSecurityInfo(
        thread_h, SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl.dacl, nullptr);
    if (sec_rc != ERROR_SUCCESS) {
        if (warnings) warnings->push_back("thread SetSecurityInfo failed (rc=" + std::to_string(sec_rc) + ")");
        return false;
    }
    return true;
}

static void strip_all_privileges_permanently(HANDLE tok, std::vector<std::string>& warnings) {
    DWORD priv_sz = 0;
    if (!GetTokenInformation(tok, TokenPrivileges, nullptr, 0, &priv_sz) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        warnings.push_back("GetTokenInformation(TokenPrivileges, size) failed (err=" +
                           std::to_string(GetLastError()) + ")");
        return;
    }
    if (priv_sz == 0) return;

    std::vector<uint8_t> pbuf(priv_sz);
    if (!GetTokenInformation(tok, TokenPrivileges, pbuf.data(), priv_sz, &priv_sz)) {
        warnings.push_back("GetTokenInformation(TokenPrivileges) failed (err=" +
                           std::to_string(GetLastError()) + ")");
        return;
    }

    auto* tp = reinterpret_cast<TOKEN_PRIVILEGES*>(pbuf.data());
    if (tp->PrivilegeCount == 0) return;

    for (DWORD i = 0; i < tp->PrivilegeCount; ++i) {
        tp->Privileges[i].Attributes = SE_PRIVILEGE_REMOVED;
    }
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(tok, FALSE, tp, priv_sz, nullptr, nullptr) ||
        GetLastError() != ERROR_SUCCESS) {
        warnings.push_back("AdjustTokenPrivileges(SE_PRIVILEGE_REMOVED) failed (err=" +
                           std::to_string(GetLastError()) + ")");
    }
}

static bool is_reparse_point_w(const wchar_t* path) noexcept {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static bool write_file_atomic_user_only_w(
    const std::wstring& target_path,
    const void* data,
    size_t size,
    PSID user_sid
) {
    if (is_reparse_point_w(target_path.c_str())) {
        return false;
    }

    LocalAclGuard acl;
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{};
    SECURITY_ATTRIBUTES* psa = nullptr;
    if (user_sid &&
        build_user_only_dacl(user_sid, FILE_ALL_ACCESS, NO_INHERITANCE, acl) &&
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&sd, TRUE, acl.dacl, FALSE) &&
        SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        psa = &sa;
    }

    const std::wstring tmp_path =
        target_path + L"." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64()) + L".tmp";

    {
        HANDLE raw_fh = CreateFileW(
            tmp_path.c_str(),
            GENERIC_WRITE,
            0,
            psa,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (raw_fh == INVALID_HANDLE_VALUE) return false;
        seb::nt::NtHandle fh(raw_fh);

        const auto* bytes = static_cast<const uint8_t*>(data);
        size_t off = 0;
        while (off < size) {
            DWORD chunk = static_cast<DWORD>((std::min<size_t>)(size - off, 0x40000000ULL));
            DWORD written = 0;
            if (!WriteFile(fh.get(), bytes + off, chunk, &written, nullptr) || written == 0) {
                DeleteFileW(tmp_path.c_str());
                return false;
            }
            off += written;
        }
        FlushFileBuffers(fh.get());
    }

    // Re-check target immediately before atomic swap.
    if (is_reparse_point_w(target_path.c_str()) ||
        !MoveFileExW(tmp_path.c_str(), target_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp_path.c_str());
        return false;
    }
    return true;
}

static void ensure_secure_profile_dir_w(const std::wstring& dir, PSID user_sid) {
    if (is_reparse_point_w(dir.c_str())) {
        throw std::runtime_error("refusing to use Chrome profile path: target is a symlink/reparse point");
    }

    LocalAclGuard acl;
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{};
    SECURITY_ATTRIBUTES* psa = nullptr;
    if (user_sid &&
        build_user_only_dacl(user_sid, FILE_ALL_ACCESS,
                             CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE, acl) &&
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&sd, TRUE, acl.dacl, FALSE) &&
        SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        psa = &sa;
    }

    if (!CreateDirectoryW(dir.c_str(), psa)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            throw std::runtime_error("CreateDirectoryW(profile_dir) failed (err=" + std::to_string(err) + ")");
        }
    }

    // Open handle without following reparse points (TOCTOU-safe) and verify + tighten DACL.
    HANDLE raw_dh = CreateFileW(
        dir.c_str(),
        WRITE_DAC | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (raw_dh == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to open profile directory handle (err=" +
                                 std::to_string(GetLastError()) + ")");
    }
    seb::nt::NtHandle dh(raw_dh);

    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(dh.get(), &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error("profile directory failed reparse-point / directory verification");
    }
    if (acl.dacl) {
        DWORD sec_rc = SetSecurityInfo(
            dh.get(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, acl.dacl, nullptr);
        if (sec_rc != ERROR_SUCCESS) {
            throw std::runtime_error("SetSecurityInfo(profile_dir) failed (rc=" + std::to_string(sec_rc) + ")");
        }
    }
}

static void enable_process_mitigations(
    std::vector<std::string>& warnings,
    std::vector<uint8_t>& out_user_sid
) {
    auto warn_fail = [&](const char* name) {
        warnings.push_back(std::string(name) + " failed (err=" + std::to_string(GetLastError()) + ")");
    };

    // 0. Suppress interactive OS / WER error popups and permanently remove CWD
    //    from legacy DLL / SearchPath resolution.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!SetDllDirectoryW(L"")) {
        warn_fail("SetDllDirectoryW");
    }
    if (!SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT)) {
        warn_fail("SetSearchPathMode");
    }

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
    //    seb_keys.exe is statically linked with the MSVC /MT runtime).
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY bsp{};
    bsp.MicrosoftSignedOnly = 1;
    if (!SetProcessMitigationPolicy(ProcessSignaturePolicy, &bsp, sizeof(bsp))) {
        warn_fail("ProcessSignaturePolicy");
    }

    // 6. Strict Control Flow Guard: reject dynamically loaded images without CFG.
    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfgp{};
    cfgp.EnableControlFlowGuard = 1;
    cfgp.StrictMode = 1;
    if (!SetProcessMitigationPolicy(ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            warn_fail("ProcessControlFlowGuardPolicy");
        }
    }

    // 7. Block loading untrusted non-system fonts
    PROCESS_MITIGATION_FONT_DISABLE_POLICY fdp{};
    fdp.DisableNonSystemFonts = 1;
    if (!SetProcessMitigationPolicy(ProcessFontDisablePolicy, &fdp, sizeof(fdp))) {
        warn_fail("ProcessFontDisablePolicy");
    }

    // 8. Block NTFS junction / symlink redirection when loading images (Windows 11+).
    PROCESS_MITIGATION_REDIRECTION_TRUST_POLICY rtp{};
    rtp.EnforceRedirectionTrust = 1;
    if (!SetProcessMitigationPolicy(ProcessRedirectionTrustPolicy, &rtp, sizeof(rtp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            warn_fail("ProcessRedirectionTrustPolicy");
        }
    }

    // 9. Strict CET shadow stack when the CPU/OS reports an active user shadow stack.
    PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY uss{};
    if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessUserShadowStackPolicy, &uss, sizeof(uss))) {
        if (uss.EnableUserShadowStack) {
            uss.BlockNonCetBinaries = 1;
            uss.SetContextIpValidation = 1;
            uss.CetDynamicApisOutOfProcOnly = 1;
            if (!SetProcessMitigationPolicy(ProcessUserShadowStackPolicy, &uss, sizeof(uss))) {
                warn_fail("ProcessUserShadowStackPolicy");
            }
        }
    }

    // 10. Side-channel isolation for secrets in memory.
    PROCESS_MITIGATION_SIDE_CHANNEL_ISOLATION_POLICY scp{};
    scp.SmtBranchTargetIsolation = 1;
    scp.IsolateSecurityDomain = 1;
    scp.DisablePageCombine = 1;
    scp.SpeculativeStoreBypassDisable = 1;
    if (!SetProcessMitigationPolicy(ProcessSideChannelIsolationPolicy, &scp, sizeof(scp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            warn_fail("ProcessSideChannelIsolationPolicy");
        }
    }

    // Note: DisallowWin32kSystemCalls is intentionally applied in
    // enable_win32k_disable_mitigation() (key-printing mode) or post-launch in
    // cdp.hpp (--launch mode), because Windows inherits ProcessSystemCallDisablePolicy
    // across CreateProcessW and breaks Chrome's main browser process.

    // Note: High-entropy ASLR is enforced at image load via PE flags
    // (/DYNAMICBASE /HIGHENTROPYVA); ProcessASLRPolicy is invalid post-init.

    // 11. Terminate immediately on Windows heap metadata corruption
    if (!HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0)) {
        warn_fail("HeapEnableTerminationOnCorruption");
    }

    // 12. Restrict dynamic DLL search path to System32 only
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        warn_fail("SetDefaultDllDirectories");
    }

    // 13. Permanently remove all token privileges and restrict Process + Main Thread DACLs
    //     to strip VM_READ/WRITE/DUP_HANDLE and THREAD_GET/SET_CONTEXT from external
    //     non-elevated processes.
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &tok)) {
        warn_fail("OpenProcessToken");
        return;
    }
    seb::nt::NtHandle tok_h(tok);

    strip_all_privileges_permanently(tok_h.get(), warnings);

    out_user_sid = get_token_user_sid_bytes(tok_h.get(), &warnings);
    if (out_user_sid.empty()) return;
    PSID user_sid = out_user_sid.data();

    LocalAclGuard proc_acl;
    DWORD acl_rc = ERROR_SUCCESS;
    if (!build_user_only_dacl(
            user_sid,
            PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            NO_INHERITANCE,
            proc_acl,
            &acl_rc)) {
        warnings.push_back("SetEntriesInAclW failed (rc=" + std::to_string(acl_rc) + ")");
        return;
    }
    DWORD sec_rc = SetSecurityInfo(
        GetCurrentProcess(), SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, proc_acl.dacl, nullptr);
    if (sec_rc != ERROR_SUCCESS) {
        warnings.push_back("SetSecurityInfo(process) failed (rc=" + std::to_string(sec_rc) + ")");
    }

    harden_thread_dacl(GetCurrentThread(), user_sid, &warnings);
}

static void enable_win32k_disable_mitigation(std::vector<std::string>& warnings) {
    // Disallow Win32k system calls in this process. Deferred in --launch mode
    // until after CreateProcessW (inside cdp.hpp) so Chrome does not inherit it.
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY scdp{};
    scdp.DisallowWin32kSystemCalls = 1;
    if (!SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &scdp, sizeof(scdp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            warnings.push_back("ProcessSystemCallDisablePolicy failed (err=" + std::to_string(err) + ")");
        }
    }
}

static void enable_child_process_mitigation(std::vector<std::string>& warnings) {
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY cpp{};
    cpp.NoChildProcessCreation = 1;
    if (!SetProcessMitigationPolicy(ProcessChildProcessPolicy, &cpp, sizeof(cpp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            warnings.push_back("ProcessChildProcessPolicy failed (err=" + std::to_string(err) + ")");
        }
    }
}

static void enable_self_job_lockdown(std::vector<std::string>& warnings) {
    // In key-printing mode, bind this process to a one-process job and remove
    // all external UI hooks. This prevents accidental shelling out and makes
    // any unhandled exception terminate the whole process tree immediately.
    seb::nt::NtHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        warnings.push_back("CreateJobObjectW failed (err=" + std::to_string(GetLastError()) + ")");
        return;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        warnings.push_back("SetInformationJobObject(extended) failed (err=" + std::to_string(GetLastError()) + ")");
        return;
    }

    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
    if (!SetInformationJobObject(job.get(), JobObjectBasicUIRestrictions, &ui, sizeof(ui))) {
        warnings.push_back("SetInformationJobObject(ui) failed (err=" + std::to_string(GetLastError()) + ")");
    }

    if (!AssignProcessToJobObject(job.get(), GetCurrentProcess())) {
        warnings.push_back("AssignProcessToJobObject failed (err=" + std::to_string(GetLastError()) + ")");
    }
}

// Verifies that neither an inherited mitigation policy nor an enclosing Job Object
// will block spawning Chrome (or break its UI process via Win32k inheritance).
static std::optional<std::string> check_child_process_blockers() {
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY cpp{};
    if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessChildProcessPolicy, &cpp, sizeof(cpp)) &&
        cpp.NoChildProcessCreation) {
        return "ProcessChildProcessPolicy (NoChildProcessCreation=1) is active on this process";
    }

    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY scdp{};
    if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSystemCallDisablePolicy, &scdp, sizeof(scdp)) &&
        scdp.DisallowWin32kSystemCalls) {
        return "ProcessSystemCallDisablePolicy (DisallowWin32kSystemCalls=1) is active and would be inherited by Chrome";
    }

    BOOL in_job = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job) {
        JOBOBJECT_BASIC_LIMIT_INFORMATION jli{};
        if (QueryInformationJobObject(nullptr, JobObjectBasicLimitInformation, &jli, sizeof(jli), nullptr)) {
            const bool active_limit = (jli.LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) != 0;
            const bool can_breakaway = (jli.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK |
                                                          JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) != 0;
            if (active_limit && jli.ActiveProcessLimit <= 1 && !can_breakaway) {
                return "Process is inside a Job Object with ActiveProcessLimit=" +
                       std::to_string(jli.ActiveProcessLimit) + " and no breakaway permission";
            }
        }
    }
    return std::nullopt;
}

static void diagnose_launch_error(const std::exception& ex, DWORD last_err, bool verbose) {
    std::cerr << "launch error: " << ex.what() << "\n";

    if (auto blocker = check_child_process_blockers()) {
        std::cerr << "[mitigation] DIAGNOSIS: " << *blocker << "\n";
    }

    if (last_err == ERROR_ACCESS_DENIED) {
        std::cerr << "[mitigation] HINT: last Win32 error was ERROR_ACCESS_DENIED (5) — "
                     "check NoChildProcessCreation, DisallowWin32kSystemCalls (avoid ShellExecuteW/COM), "
                     "or executable ACL/AV.\n";
    } else if (last_err == ERROR_NOT_ENOUGH_QUOTA) {
        std::cerr << "[mitigation] HINT: last Win32 error was ERROR_NOT_ENOUGH_QUOTA (1816) — "
                     "typically caused by a Job Object ActiveProcessLimit.\n";
    } else if (last_err == ERROR_INVALID_IMAGE_HASH) {
        std::cerr << "[mitigation] HINT: last Win32 error was ERROR_INVALID_IMAGE_HASH (577) — "
                     "MicrosoftSignedOnly blocked loading an unsigned/non-MS DLL.\n";
    }

    if (verbose) {
        PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY scdp{};
        if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSystemCallDisablePolicy, &scdp, sizeof(scdp)) &&
            scdp.DisallowWin32kSystemCalls) {
            std::cerr << "[mitigation] NOTE: DisallowWin32kSystemCalls=1 is active "
                         "(CreateProcessW works, ShellExecuteW/user32/gdi32 will fail).\n";
        }
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY bsp{};
        if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSignaturePolicy, &bsp, sizeof(bsp)) &&
            bsp.MicrosoftSignedOnly) {
            std::cerr << "[mitigation] NOTE: MicrosoftSignedOnly=1 is active "
                         "(only Microsoft-signed DLLs can load in this process).\n";
        }
        std::cerr << "[mitigation] TIP: Check OS mitigation blocks with:\n"
                  << "  Get-WinEvent -LogName 'Microsoft-Windows-Security-Mitigations/KernelMode' -MaxEvents 10\n";
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
    if (written == 0 || written >= needed) return p;
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
    explicit ThreadGroup(PSID user_sid = nullptr) noexcept : user_sid_(user_sid) {}
    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    ~ThreadGroup() { join_nothrow(); }

    HANDLE start(LPTHREAD_START_ROUTINE fn, void* arg) {
        handles_.reserve(handles_.size() + 1);
        raw_.reserve(raw_.size() + 1);
        const DWORD flags = user_sid_ ? CREATE_SUSPENDED : 0;
        seb::nt::NtHandle h(CreateThread(nullptr, 0, fn, arg, flags, nullptr));
        if (!h) return nullptr;
        if (user_sid_) {
            (void)harden_thread_dacl(h.get(), user_sid_);
            if (ResumeThread(h.get()) == static_cast<DWORD>(-1)) {
                TerminateThread(h.get(), 1);
                return nullptr;
            }
        }
        HANDLE raw = h.get();
        handles_.push_back(std::move(h));
        raw_.push_back(raw);
        return raw;
    }

    void join_checked() {
        if (handles_.empty()) return;
        const bool ok = seb::nt::nt_wait_all(raw_.data(), static_cast<ULONG>(raw_.size()));
        if (!ok) {
            join_each_fallback();
            throw std::runtime_error("nt_wait_all failed waiting for worker threads");
        }
        raw_.clear();
        handles_.clear();
    }

    void join_nothrow() noexcept {
        if (handles_.empty()) return;
        if (!seb::nt::nt_wait_all(raw_.data(), static_cast<ULONG>(raw_.size()))) {
            join_each_fallback();
            return;
        }
        raw_.clear();
        handles_.clear();
    }

    bool empty() const noexcept { return handles_.empty(); }

private:
    void join_each_fallback() noexcept {
        for (HANDLE h : raw_) {
            (void)WaitForSingleObject(h, INFINITE);
        }
        raw_.clear();
        handles_.clear();
    }

    PSID user_sid_ = nullptr;
    std::vector<seb::nt::NtHandle> handles_;
    std::vector<HANDLE> raw_;
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

struct ArgvPasswordGuard {
    int argc = 0;
    wchar_t** argv = nullptr;

    ArgvPasswordGuard(int argc_, wchar_t** argv_) noexcept : argc(argc_), argv(argv_) {}

    ~ArgvPasswordGuard() {
        for (int i = 1; i < argc; ++i) {
            if (!argv || !argv[i]) continue;
            std::wstring_view w(argv[i]);
            if (w == L"--password" && i + 1 < argc && argv[i + 1]) {
                SecureZeroMemory(argv[i + 1], std::wcslen(argv[i + 1]) * sizeof(wchar_t));
            } else if (w.starts_with(L"--password=")) {
                SecureZeroMemory(argv[i], w.size() * sizeof(wchar_t));
            }
        }
    }
    ArgvPasswordGuard(const ArgvPasswordGuard&) = delete;
    ArgvPasswordGuard& operator=(const ArgvPasswordGuard&) = delete;
};

int wmain(int argc, wchar_t** argv) {
    ArgvPasswordGuard argv_guard{argc, argv};
    VehGuard veh_guard;

    std::vector<std::string> mitigation_warnings;
    std::vector<uint8_t> user_sid_buf;
    enable_process_mitigations(mitigation_warnings, user_sid_buf);
    PSID user_sid = user_sid_buf.empty() ? nullptr : user_sid_buf.data();

    std::optional<std::wstring> seb_path;
    std::optional<std::wstring> root_arg;
    std::string password;
    seb::win::SecureStringGuard pwd_wipe_guard(password);
    std::string route_pattern = "https://sarathi.parivahan.gov.in/*";
    bool no_cache = false;
    bool launch = false;
    bool verbose = false;
    bool end_of_opts = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring_view warg(argv[i]);
        if (!end_of_opts && warg == L"--") {
            end_of_opts = true;
        } else if (!end_of_opts && warg == L"--root") {
            if (++i >= argc) return print_usage_err("--root requires a value");
            root_arg = std::wstring(argv[i]);
        } else if (!end_of_opts && warg.starts_with(L"--root=")) {
            root_arg = std::wstring(warg.substr(7));
        } else if (!end_of_opts && warg == L"--password") {
            if (++i >= argc) return print_usage_err("--password requires a value");
            password = seb::win::wide_to_utf8(argv[i]);
        } else if (!end_of_opts && warg.starts_with(L"--password=")) {
            password = seb::win::wide_to_utf8(warg.substr(11));
        } else if (!end_of_opts && warg == L"--route") {
            if (++i >= argc) return print_usage_err("--route requires a value");
            route_pattern = seb::win::wide_to_utf8(argv[i]);
        } else if (!end_of_opts && warg.starts_with(L"--route=")) {
            route_pattern = seb::win::wide_to_utf8(warg.substr(8));
        } else if (!end_of_opts && warg == L"--no-cache") {
            no_cache = true;
        } else if (!end_of_opts && warg == L"--launch") {
            launch = true;
        } else if (!end_of_opts && (warg == L"-v" || warg == L"--verbose")) {
            verbose = true;
        } else if (!end_of_opts && (warg == L"-h" || warg == L"--help")) {
            std::cout << USAGE;
            return 0;
        } else if (!end_of_opts && !warg.empty() && warg[0] == L'-') {
            return print_usage_err("unknown option: " + seb::win::wide_to_utf8(warg));
        } else {
            if (seb_path.has_value()) {
                return print_usage_err("unexpected positional argument: " + seb::win::wide_to_utf8(warg));
            }
            seb_path = std::wstring(warg);
        }
    }

    if (!launch) {
        enable_win32k_disable_mitigation(mitigation_warnings);
        enable_child_process_mitigation(mitigation_warnings);
        enable_self_job_lockdown(mitigation_warnings);
    }

    if (verbose) {
        for (const auto& w : mitigation_warnings) {
            std::cerr << "[mitigation] WARNING: " << w << "\n";
        }
    }

    if (!seb_path.has_value()) return print_usage_err("the following arguments are required: seb");
    if (!seb::is_file_w(seb_path->c_str())) return print_usage_err("file not found: " + seb::win::wide_to_utf8(*seb_path));
    if (!is_valid_http_url(route_pattern)) {
        return print_usage_err("--route must be a valid http:// or https:// URL without spaces or control chars");
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
        // Constructed AFTER pwd_lock -> destroyed (wiped) BEFORE pwd_lock unlocks the page.
        seb::win::SecureStringGuard inner_pwd_wipe(password);

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
        } else if (seb::is_file_w(cache_path.c_str()) && !is_reparse_point_w(cache_path.c_str())) {
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
                    v_tree && v_tree->as_str() &&
                    v_tree_hash && v_tree_hash->as_str() && is_lower_hex_len(*v_tree_hash->as_str(), 64)) {
                    try {
                        cached_tree = seb::win::base64_decode(*v_tree->as_str());
                        cached_version = *v_ver->as_str();
                        cached_salt_const = *v_salt->as_str();
                        install_cached = !cached_tree.empty() &&
                            seb::win::sha256_hex(cached_tree.data(), cached_tree.size()) == *v_tree_hash->as_str();
                        if (!install_cached) {
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
            if (is_reparse_point_w(cache_path.c_str())) {
                std::cerr << "[seb] WARNING: ignoring cache file because it is a symlink/reparse point\n";
            }
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
                ThreadGroup workers(user_sid);
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
                    if (!write_file_atomic_user_only_w(cache_path, out_json.data(), out_json.size(), user_sid) && verbose) {
                        std::cerr << "[seb] WARNING: failed to atomically write cache file\n";
                    }
                } catch (...) {}
            }
        }
    } catch (const std::exception& ex) {
        return print_usage_err(ex.what());
    }

    std::cout << "configKey=" << config_key << "\n"
              << "browserExamKey=" << browser_key << "\n" << std::flush;

    if (launch) {
        if (auto blocker = check_child_process_blockers()) {
            std::cerr << "launch error: cannot spawn browser — " << *blocker << "\n";
            return 1;
        }

        SetLastError(ERROR_SUCCESS);
        try {
            ensure_secure_profile_dir_w(profile_dir, user_sid);
            seb::cdp::launch_cdp_browser(start_url, version, config_key, browser_key, profile_dir, route_pattern, verbose);
        } catch (const std::exception& ex) {
            const DWORD last_err = GetLastError();
            diagnose_launch_error(ex, last_err, verbose);
            return 1;
        }
    }
    return 0;
}