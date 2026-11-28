# 🛡️ Sarathi Parivahan LL Test: Native C++26 SEB Emulator & Proctor Defuser

![Target](https://img.shields.io/badge/Target-Sarathi%20Parivahan-red)
![Language](https://img.shields.io/badge/C%2B%2B-26%20%2F%2020-blue)
![Angular](https://img.shields.io/badge/Frontend-Angular%2020-red)
![ONNX](https://img.shields.io/badge/AI%20Defeated-ONNX%20FaceMesh-orange)
![License](https://img.shields.io/badge/License-MIT-green)

A standalone, zero-dependency Windows x64 C++26 implementation of the **Sarathi Parivahan Learner's License (LL) Test** Safe Exam Browser (SEB) emulator and stealth Chrome DevTools Protocol (CDP) defuser.

It compiles into a single ~1.2 MB static executable (`seb_keys.exe`) that depends **only on built-in Windows OS DLLs** — no Python, no Playwright/Patchright, and no external compiler runtime DLLs required. It emulates the exact SEB cryptographic handshake for `sarathi.parivahan.gov.in` to bypass 403 Forbidden errors, and injects `DEFUSER_JS` over a raw WinSock2 CDP proxy to neutralize Angular 20 anti-tamper checks, ONNX AI face proctoring, and camera security checks.

> **⚠️ STRICT DISCLAIMER**  
> This repository is for **security research and educational reverse-engineering only**. Circumventing government exam proctoring, altering ONNX AI verdicts, or spoofing SEB integrity keys on live servers is illegal and violates the IT Act. The author assumes zero liability for misuse. Do not run this against live, production endpoints.

---

## 🎯 Sarathi Target Architecture & Defeated Layers

Through reverse-engineering the Sarathi Parivahan web app (`https://sarathi.parivahan.gov.in/onlinelltestbnd/exam/sebCheck`), the following security layers were identified and neutralized:

1. **Server-Side SEB Handshake:** The Sarathi backend validates `X-SafeExamBrowser-ConfigKeyHash` and `X-SafeExamBrowser-RequestHash` (`SHA256(RequestURL + Key)`) plus the `SEB/<version> (x64)` User-Agent.
2. **Anti-Debug Traps:** Timer-based `debugger;` loops and `Image.prototype.id` getter traps trigger `forceLogout()` when DevTools is opened.
3. **ONNX AI Proctoring:** The `proctorService` loads `FaceMesh` / `Solution` and runs `runProctorSpoofCheck()` against the webcam feed.
4. **Camera Security (`anti` service):** `runCameraSecurityCheck()` inspects `MediaStreamTrack.prototype.label` and checks for virtual cameras (OBS, ManyCam), multiple webcams, and replay streams.
5. **Navigation & History Traps:** Pushes dummy `s1_...` `History.pushState` entries, blocks `Alt+Left` / `Backspace`, and redirects to `SebSetupInstructions` or `/logout` on failure.
6. **SEB Environment Checks:** Probes `window.SafeExamBrowser`, `navigator.webdriver`, automation globals (`cdc_`, `__playwright`, etc.), WebGPU/WebNN (`navigator.gpu`, `navigator.ml`), and `localStorage.getItem('seb_error_reason')`.

---

## ⚡ Improvements Over the Python Version (`main` branch)

| Area | Python (`seb_keys.py`) | Native C++26 (`seb_keys.exe`) |
| :--- | :--- | :--- |
| **Runtime Dependencies** | Python 3.9+, `rncryptor`, `pefile`, `patchright` + Chromium | Single ~1.2 MB static `.exe` (Windows OS DLLs only) |
| **Warm CLI Latency** | ~270 ms | **~21 ms** (**12.6x faster**) |
| **Cold CLI Latency (`--no-cache`)** | ~1,723 ms | **~488 ms** (**3.53x faster**) |
| **PE Salt Extraction (`seb_x64.dll`)** | ~31.07 ms (`pefile`) | **~0.97 ms** (**32.0x faster**, `NtCreateSection` + `.text` scan) |
| **Request Hash (10K iterations)** | ~11.74 ms (`hashlib`) | **~3.23 ms** (**3.64x faster**, reusable `BCRYPT_HASH_REUSABLE_FLAG`) |
| **Sarathi Browser Launch (First Req)** | ~3,563 ms (Patchright/Node driver) | **~1,616 ms** (**2.20x faster**, raw WinSock2 CDP + 50 ms non-blocking connect) |
| **Browser Process Cleanup** | Can orphan Chrome if killed | Kernel-enforced `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` kills Chrome on exit |
| **Certificate Bypass** | `--ignore-certificate-errors` CLI flag (triggers Chrome warning bar) | CDP `Security.setIgnoreCertificateErrors` (zero infobar warning) |
| **Process Hardening** | Standard Python interpreter | `SetProcessMitigationPolicy` (ACG, strict handles, no remote/low-IL DLLs, no extension points) + Control Flow Guard (`-mguard=cf`) |
| **Low-Level I/O** | Win32 / Python `os` wrappers | Direct `ntdll.dll` syscalls (`NtCreateFile`, `NtQueryDirectoryFile`, `NtMapViewOfSection`, `NtOpenKey`, `NtWaitForMultipleObjects`) |
| **Diagnostics** | Basic stdout | `-v` / `--verbose` logs timing, cache hits, job binding, and CDP traffic to `stderr` |

---

## 🧩 Architecture Overview

```
cpp/src/
├── main.cpp        CLI parsing, OS process mitigations, cache orchestration, worker pool
├── win_nt.hpp      Direct NT syscall wrappers (ntdll.dll) for files, sections, registry, waits
├── win_crypto.hpp  Windows CNG (bcrypt.dll) & Crypt32 (crypt32.dll) crypto, Base64, PE metadata
├── seb.hpp         SEB container unpacking (pwcc/pswd/plnd/gzip), PE salt extraction, tree hash
├── plist.hpp       XML plist parser & .NET-compatible canonical JSON serializer (ConfigKey)
├── inflate.hpp     RFC 1951/1952 Inflate/Gzip decompressor with 128 MiB bomb guard
├── json.hpp        Minimal JSON parser/serializer with depth guard (128 levels)
└── cdp.hpp         WinSock2 HTTP/WebSocket CDP proxy, Job Object guard, stealth & DEFUSER_JS
```

### How It Works

1. **OS Process Hardening (`main.cpp`)**
   At startup, `enable_process_mitigations()` locks down `seb_keys.exe` via `SetProcessMitigationPolicy` (checking return values and logging any OS-policy rejections under `-v`/`--verbose`):
   - `ProcessDynamicCodePolicy` (`ProhibitDynamicCode = 1`): Requests ACG blocking runtime dynamic code generation and `VirtualProtect(..., PAGE_EXECUTE_*)` (logged if rejected post-init by host Windows policy).
   - `ProcessStrictHandleCheckPolicy`: Raises an immediate exception on invalid or double-closed `HANDLE`s.
   - `ProcessImageLoadPolicy` (`NoRemoteImages`, `NoLowMandatoryLabelImages`, `PreferSystem32Images`): Blocks remote/low-IL DLLs and searches `System32` first to prevent DLL hijacking.
   - `ProcessExtensionPointDisablePolicy`: Disables legacy `AppInit_DLLs` and global hooks.
   - `ProcessSignaturePolicy` (`MicrosoftSignedOnly = 1`): Blocks any unsigned DLL from loading at runtime (enabled by full static linking).
   - `ProcessFontDisablePolicy` (`DisableNonSystemFonts = 1`): Blocks untrusted non-system fonts.
   - **PE Header & Control Flow Guard (CFG) Hardening**: Enforces 64-bit high-entropy ASLR, DEP, and Terminal Server awareness via PE linker flags (`--nxcompat --dynamicbase --high-entropy-va --tsaware`), and compiles with `-mguard=cf` paired with a COFF `IMAGE_LOAD_CONFIG_DIRECTORY64` `_load_config_used` (`.00cfg` merged into read-only `.rdata`) so `ntdll` activates `LdrpValidateUserCallTarget` at load time (`GuardFlags = 0x100` `CF_INSTRUMENTED` for GCC-built MinGW-w64 11.1.0 `crt2.o`/`libstdc++.a` compatibility, or `-DSEB_CFG_FUNCTION_TABLE=1` for `0x10500` on `llvm-mingw`).
   - **Dynamic DLL Search & Process DACL Hardening**: Calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` to harden **dynamic** DLL loads (static IAT DLLs are resolved by the Windows loader prior to `main()`) and restricts the process DACL via `SetSecurityInfo` (stripping `PROCESS_VM_READ`/`WRITE`/`DUP_HANDLE` from external non-elevated processes while preserving `PROCESS_TERMINATE` and `SYNCHRONIZE`).
   - **Heap, Stack, RAM-Pinning & Bounds Protection**: Calls `HeapSetInformation(..., HeapEnableTerminationOnCorruption, ...)`, compiles with `-fstack-protector-strong`, `-ftrivial-auto-var-init=zero`, `-D_GLIBCXX_ASSERTIONS`, and `-s -Wl,--gc-sections`, pins RNCryptor keys in page-isolated `VirtualAlloc` + `VirtualLock` `LockedBuffer`s, wipes `--password` `argv` slots before `LocalFree`, wipes decrypted `.seb` XML/containers across reallocations and exceptions with `SecureZeroMemory`, and writes `.seb_tree_cache.json` atomically after reparse-point checks.

2. **SEB Key Derivation (`seb.hpp`, `plist.hpp`, `inflate.hpp`, `win_crypto.hpp`)**
   - **Container Unpacking**: Reads the Sarathi `.seb` file (capped at 64 MiB) and unwraps `pwcc`/`pswd` (RNCryptor v2/v3 via PBKDF2-HMAC-SHA1 + HMAC-SHA256 + AES-256-CBC in `bcrypt.dll`), `plnd`, and gzip layers (with full RFC 1952 `FHCRC`, `CRC-32`, and `ISIZE` verification plus 128 MiB stored/compressed inflate caps).
   - **ConfigKey**: Parses XML plist, normalizes `.NET` dates (`SystemTimeToTzSpecificLocalTime`) and doubles, sorts keys via `CompareStringEx` invariant culture, and hashes with SHA-256.
   - **SEB Tree & Salt**: Checks `.seb_tree_cache.json` (validating `seb_version`, `integrity_salt`, hex keys, `start_url`, `mtime_ns`/`size`, and `tree_sha256`). On cache miss, parallel worker threads extract `seb_x64.dll`'s XOR-obfuscated 40-char salt via `NtMapViewOfSection` and iteratively traverse/hash the SEB install tree using `NtQueryDirectoryFile`, reusable `BCRYPT_HASH_REUSABLE_FLAG` SHA-256 handles, PE version info, and `CryptQueryObject` Authenticode thumbprints.
   - **BrowserExamKey**: `SHA256(tree_bytes + configKey + examKeySalt + integrity_salt_const)`.

3. **Stealth Chrome Launcher & Sarathi CDP Proxy (`cdp.hpp`)**
   - **Job-Bound Chrome + Child Mitigations (`JobGuard` / `STARTUPINFOEXW`)**: Spawns Chrome/Edge with `lpApplicationName`, `CommandLineToArgvW`-safe argument quoting, `--remote-debugging-port=0` (`DevToolsActivePort` discovery), `CREATE_SUSPENDED`, child process mitigations (`HEAP_TERMINATE`, `BOTTOM_UP_ASLR`, `HIGH_ENTROPY_ASLR`, `FORCE_RELOCATE_IMAGES`, `PREFER_SYSTEM32`) via `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY`, and binds it to a `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job before resuming — killing `seb_keys.exe` from the CLI kills Chrome immediately.
   - **Stealth & Certs**: Launches with `--disable-blink-features=AutomationControlled`, connects to the browser-level CDP WebSocket with RFC 6455 `Sec-WebSocket-Accept` validation and `Target.setAutoAttach`, injects `STEALTH_JS` via `Page.addScriptToEvaluateOnNewDocument` (without `Runtime.enable`, preventing CDP stack-leak probes), and enables `Security.setIgnoreCertificateErrors` over CDP.
   - **Traffic Interception (`https://sarathi.parivahan.gov.in/*`)**: Uses `Fetch.enable` and `Target.setAutoAttach` (for popups/tabs) over a raw WinSock2 WebSocket client (`BCryptGenRandom` RFC 6455 masking, 256 MiB cumulative message cap):
     - Answers CORS `OPTIONS` preflights with `204` and sanitized `Origin`.
     - Computes and injects `X-SafeExamBrowser-ConfigKeyHash` and `X-SafeExamBrowser-RequestHash` per URL.
     - Strips CSP/encoding headers on HTML `Document` responses and injects `DEFUSER_JS` right after `<head>`.

---

## 🛠️ How to Compile

### Prerequisites
- **Windows 10 / 11 (x64)**
- **Exact Official Safe Exam Browser (SEB) from the Sarathi Portal** *(required)*: You **must** install the exact SEB build provided by the Sarathi Parivahan portal (e.g., `3.10.0.826` in `C:\Program Files\SafeExamBrowser`). `BrowserExamKey` hashes the entire SEB installation directory, PE versions, Authenticode signatures, and `seb_x64.dll`'s integrity salt — any different SEB version will produce the wrong key and fail server validation.
- **Browser for `--launch`**: **Google Chrome** is recommended (auto-detected), with **Microsoft Edge** (built into Windows 10/11) automatically used as a fallback if Chrome is not installed. *(No browser is required if running without `--launch` to only print keys.)*
- **Build tools (only to compile from source)** — any one of:
  - **Clang/LLVM 22+** (`clang++.exe` + `lld`) with **MinGW-w64 11+** headers/static libraries. Tested with LLVM `22.1.8` + MinGW-w64 `11.1.0`.
  - **MinGW-w64 GCC 11+** (`g++`). Tested with GCC `11.1.0`.
  - **MSVC 2022+** via CMake.
  - **CMake 3.25+** and **Ninja** for Option 2.
- Running the compiled `seb_keys.exe` requires **no** compiler or runtime installs.

> The commands below are PowerShell and assume the repository root is the current directory. Create the output directory first; the compiler will not create it:
> ```powershell
> New-Item -ItemType Directory -Force cpp\build | Out-Null
> ```

### Option 1: Clang + LLVM `lld` (Recommended — C++26, CFG, Static Windows-Only Binary)

Adjust `$LLVM` / `$MINGW` to your install locations, then run:

```powershell
$LLVM  = "C:\Program Files\LLVM\bin"
$MINGW = "C:\Users\comic\.choosenim\toolchains\mingw64"
$env:PATH = "$LLVM;$MINGW\bin;$env:PATH"
New-Item -ItemType Directory -Force cpp\build | Out-Null

clang++ --target=x86_64-w64-mingw32 -std=c++26 -O3 -march=x86-64-v3 -flto `
  -finput-charset=UTF-8 -fexec-charset=UTF-8 `
  -mguard=cf -fstack-protector-strong -ftrivial-auto-var-init=zero `
  -ffunction-sections -fdata-sections -fuse-ld=lld `
  -static -static-libgcc -static-libstdc++ -pthread -s `
  "-Wl,--gc-sections,--no-insert-timestamp,--nxcompat,--dynamicbase,--high-entropy-va,--tsaware,-Xlink=-merge:.00cfg=.rdata" `
  -Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion -Wformat=2 -Wundef -Wnull-dereference -Werror `
  -D_GLIBCXX_ASSERTIONS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -D_CRT_SECURE_NO_WARNINGS `
  cpp\src\main.cpp -o cpp\build\seb_keys.exe `
  -lntdll -lbcrypt -lcrypt32 -lversion -lws2_32 -ladvapi32 -lshell32
```

> **Header discovery**: Clang's `--target=x86_64-w64-mingw32` needs the MinGW-w64 `bin` directory on `PATH` (as shown) so it can locate the sysroot and headers. If you get `winsock2.h file not found`, verify `$MINGW\bin` is on `PATH`.
>
> **CFG note**: With GCC-built MinGW-w64 11.1.0 (`crt2.o`/`libstdc++.a` lack `.gfids$y` tables), `main.cpp` sets `GuardFlags = 0x100` (`IMAGE_GUARD_CF_INSTRUMENTED`), so `ld.lld` emits one advisory `GuardFlags not set correctly in '_load_config_used'` warning while Windows `ntdll` still wires `LdrpValidateUserCallTarget` cleanly. This linker warning does not fail the build. If building against `llvm-mingw` (CRT/`libc++` compiled with `-mguard=cf`), pass `-DSEB_CFG_FUNCTION_TABLE=1` to enable `0x10500` (`CF_FUNCTION_TABLE_PRESENT | CF_LONGJUMP_TABLE_PRESENT`) with zero linker warnings.

### Option 2: CMake (Clang, GCC, or MSVC)

Install CMake 3.25+ and Ninja, and put the chosen compiler on `PATH`. Use a separate build directory per compiler.

**Clang + MinGW-w64 (C++26):**
```powershell
$env:PATH = "C:\Program Files\LLVM\bin;C:\Users\comic\.choosenim\toolchains\mingw64\bin;$env:PATH"
cmake -S cpp -B cpp/build-clang -G Ninja `
  -DCMAKE_CXX_COMPILER=clang++ -DSEB_MINGW_TARGET=ON `
  -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build-clang --config Release
```

**MinGW-w64 GCC (C++20):**
```powershell
cmake -S cpp -B cpp/build-gcc -G Ninja `
  -DCMAKE_CXX_COMPILER=g++ -DSEB_CXX_STANDARD=20 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build-gcc --config Release
```

**MSVC 2022 (C++23/latest):**
```powershell
cmake -S cpp -B cpp/build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DSEB_MINGW_TARGET=OFF -DSEB_CXX_STANDARD=23 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build-msvc --config Release
```

*(CMake 3.29+ is required for `SEB_CXX_STANDARD=26`; with older CMake, pass `-DSEB_CXX_STANDARD=20` or `23` explicitly. `SEB_MINGW_TARGET` only affects Clang and should be `OFF` for MSVC/Clang-cl. On a machine without MSVC, use Option 1 or Option 3 instead.)*

### Option 3: MinGW-w64 GCC Fallback (C++20)

```powershell
$env:PATH = "C:\Users\comic\.choosenim\toolchains\mingw64\bin;$env:PATH"
New-Item -ItemType Directory -Force cpp\build | Out-Null

g++ -std=c++20 -O3 -march=native -flto -static -static-libgcc -static-libstdc++ -pthread `
  -Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion `
  -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -D_CRT_SECURE_NO_WARNINGS `
  cpp\src\main.cpp -o cpp\build\seb_keys.exe `
  -lntdll -lbcrypt -lcrypt32 -lversion -lws2_32 -ladvapi32 -lshell32
```

The resulting executable is `cpp\build\seb_keys.exe`. Do not run it against live production endpoints; see the disclaimer above.

---

## 📖 Usage

### Launch Sarathi Exam in Stealth Chrome
```powershell
cpp\build\seb_keys.exe exam.seb --launch --verbose
```

### CLI Arguments

| Flag | Description |
| :--- | :--- |
| `<seb>` | **(Required)** Path to the Sarathi `.seb` configuration file. |
| `--launch` | Launches Chrome/Edge with CDP SEB header injection, stealth, and `DEFUSER_JS`. |
| `--root <path>` | Manually specify SEB install directory (auto-detected from registry/Program Files by default). |
| `--password <pw>` | Password for `pwcc` / `pswd` encrypted `.seb` files. |
| `--route <pattern>` | `http://` or `https://` URL pattern for CDP `Fetch.enable` (default: `https://sarathi.parivahan.gov.in/*`). |
| `--no-cache` | Bypass reading/writing `.seb_tree_cache.json` and recompute the SEB tree fingerprint. |
| `-v`, `--verbose` | Log timing, cache state, Job Object PID binding, and CDP traffic to `stderr`. |
| `-h`, `--help` | Print CLI usage summary and exit with code `0`. |

---

## 🧠 The `__cmp` Developer Console (Sarathi Angular 20)

Once the Sarathi exam page loads, open DevTools (`F12`). `DEFUSER_JS` hooks `Function.prototype.call` / `.apply` (restoring the natives immediately after capture) and exposes the Angular 20 component globally as `window.__cmp`, logging `[defuser] 🎯 __cmp CAPTURED!` along with intercepted WebCrypto PBKDF2 passphrases, salts, and encrypted/decrypted payloads (`window.__SEB_PASSPHRASE`, `window.__SEB_SALT`).

```javascript
// 1. Dump cached exam questions from IndexedDB (LLTestCacheDB)
copy(JSON.stringify(await __cmp.indexedDb.getExamData(), null, 2))

// 2. Verify ONNX AI proctoring & camera security are bypassed
await __cmp.proctorService.runProctorSpoofCheck(__cmp.videoRef.nativeElement)
await __cmp.anti.runCameraSecurityCheck(null, __cmp.videoRef.nativeElement)

// 3. Bypass minimum answer timer lock & submit
__cmp.canFinalSubmit = true;
__cmp.cd.detectChanges();
__cmp.onFinalSubmit();
```
