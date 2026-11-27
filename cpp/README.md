# `seb_keys` — Native C++26 SEB Key Derivation & CDP Launcher

A standalone, zero-external-dependency Windows x64 C++26 implementation of the Safe Exam Browser (SEB) key derivation engine and stealth Chrome DevTools Protocol (CDP) launcher.

It produces a single static executable (`seb_keys.exe`, ~2.0 MB) that depends **only on built-in Windows OS DLLs** (`ntdll.dll`, `bcrypt.dll`, `crypt32.dll`, `kernel32.dll`, `ws2_32.dll`, `advapi32.dll`, `shell32.dll`, `version.dll`, `msvcrt.dll`) — no Python, no Playwright, and no GCC/MinGW runtime DLLs required.

---

## Architecture Overview

```
cpp/src/
├── main.cpp        CLI parsing, OS process mitigations, cache orchestration, worker pool
├── win_nt.hpp      Direct NT syscall wrappers (ntdll.dll) for files, sections, registry, waits
├── win_crypto.hpp  Windows CNG (bcrypt.dll) & Crypt32 (crypt32.dll) crypto, Base64, PE metadata
├── seb.hpp         SEB container unpacking (pwcc/pswd/plnd/gzip), PE salt extraction, tree hash
├── plist.hpp       XML plist parser & .NET-compatible canonical JSON serializer (ConfigKey)
├── inflate.hpp     RFC 1951/1952 Inflate/Gzip decompressor with 128 MiB bomb guard
├── json.hpp        Minimal JSON parser/serializer with depth guard (128 levels)
└── cdp.hpp         WinSock2 HTTP/WebSocket CDP client, Job Object guard, stealth & DEFUSER_JS
```

### How It Works

1. **OS Hardening (`main.cpp`)**
   At startup, `enable_process_mitigations()` locks down the process using `SetProcessMitigationPolicy`:
   - `ProcessDynamicCodePolicy` (`ProhibitDynamicCode = 1`): Blocks runtime dynamic code generation and `VirtualProtect(..., PAGE_EXECUTE_*)` (ACG).
   - `ProcessStrictHandleCheckPolicy`: Immediately raises an exception on invalid or double-closed `HANDLE`s.
   - `ProcessImageLoadPolicy`: Blocks DLL loading from remote UNC shares and low-integrity folders.
   - `ProcessExtensionPointDisablePolicy`: Disables legacy `AppInit_DLLs` and global hooks.

2. **SEB Configuration & Key Derivation (`seb.hpp`, `plist.hpp`, `inflate.hpp`)**
   - **Container Unpacking**: Validates the input `.seb` file (capped at 64 MiB), then iteratively unwraps `pwcc`/`pswd` (RNCryptor v2/v3 via PBKDF2-HMAC-SHA1 + HMAC-SHA256 + AES-256-CBC in `bcrypt.dll`), `plnd`, and gzip layers (`inflate.hpp`, capped at 128 MiB).
   - **ConfigKey (`exam_configuration_key`)**: Parses the XML plist, normalizes dates (`SystemTimeToTzSpecificLocalTime`) and doubles (`.NET` formatting), sorts dictionary keys with Windows `CompareStringEx` invariant culture, omits `originatorVersion` and empty dicts, and computes SHA-256.
   - **Installation Tree Fingerprint & Salt Extraction**:
     - Checks `.seb_tree_cache.json` (keyed by SEB root path, `SafeExamBrowser.exe` mtime/size, and SHA-256 integrity checksum).
     - On cache miss, spawns parallel worker threads to:
       1. Read `SafeExamBrowser.exe` version via `GetFileVersionInfoW`.
       2. Memory-map `seb_x64.dll` via `NtCreateSection` / `NtMapViewOfSection` and scan `.text` for the XOR-obfuscated 40-char integrity salt constant.
       3. Enumerate the SEB install directory via `NtQueryDirectoryFile`, schedule largest files first across up to 8 worker threads, hash contents with reusable `BCRYPT_HASH_REUSABLE_FLAG` SHA-256 handles, read PE version resources, and extract Authenticode SHA-1 thumbprints via `CryptQueryObject`.
   - **BrowserExamKey**: Computes `SHA256(tree_bytes + configKey + examKeySalt + integrity_salt_const)`.

3. **Stealth Browser & CDP Proxy (`cdp.hpp`)**
   - **Child Process Lifecycle (`JobGuard`)**: Creates a Win32 Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, spawns Chrome/Edge with `CREATE_SUSPENDED`, assigns it to the job, and resumes it — ensuring the browser is automatically terminated by the kernel if `seb_keys.exe` exits or is killed from the CLI.
   - **Stealth**: Launches with `--disable-blink-features=AutomationControlled`, injects `STEALTH_JS` via `Page.addScriptToEvaluateOnNewDocument` (without calling `Runtime.enable`, avoiding CDP stack-leak detection), and enables `Security.setIgnoreCertificateErrors` via CDP (avoiding the `--ignore-certificate-errors` infobar).
   - **Request/Response Interception**: Uses `Fetch.enable` and `Target.setAutoAttach` (for popups/tabs) over a raw WinSock2 WebSocket client (`BCryptGenRandom` RFC 6455 masking):
     - Handles CORS `OPTIONS` preflight requests with `204` responses (sanitizing `Origin`).
     - Strips existing SEB headers and injects `X-SafeExamBrowser-ConfigKeyHash` and `X-SafeExamBrowser-RequestHash` (`SHA256(clean_url + key)`), cached per URL.
     - On HTML `Document` responses, strips CSP/encoding headers and injects `DEFUSER_JS` right after `<head>`.

---

## Build Instructions

### Option 1: Clang + LLVM `lld` (Recommended — C++26, CFG, Static Windows-Only Binary)

**Prerequisites:**
- **LLVM / Clang 22+** (`C:\Program Files\LLVM\bin\clang++.exe` and `lld`)
- **MinGW-w64 headers & static libs** (e.g. `C:\Users\comic\.choosenim\toolchains\mingw64\bin`)

```powershell
$env:PATH = "C:\Program Files\LLVM\bin;C:\Users\comic\.choosenim\toolchains\mingw64\bin;$env:PATH"

clang++ --target=x86_64-w64-mingw32 -std=c++26 -O3 -march=native -flto `
  -mguard=cf -fuse-ld=lld -static -static-libgcc -static-libstdc++ -pthread `
  -Wall -Wextra -Wpedantic -Wshadow `
  -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -D_CRT_SECURE_NO_WARNINGS `
  cpp\src\main.cpp -o cpp\build\seb_keys.exe `
  -lntdll -lbcrypt -lcrypt32 -lversion -lws2_32 -ladvapi32 -lshell32
```

### Option 2: CMake (Clang, GCC, or MSVC)

```powershell
cmake -S cpp -B cpp/build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build --config Release
```

*(Override the C++ standard with `-DSEB_CXX_STANDARD=23` if using an older compiler.)*

---

## Usage

```powershell
cpp\build\seb_keys.exe exam.seb [--root <path>] [--password <pw>] [--route <pattern>] [--no-cache] [--launch] [--verbose]
```

| Flag | Description |
| :--- | :--- |
| `<seb>` | **(Required)** Path to the `.seb` configuration file. |
| `--root <path>` | Override auto-detected SEB installation directory. |
| `--password <pw>` | Password for `pwcc`/`pswd` encrypted `.seb` files. |
| `--route <pattern>` | `http://` or `https://` URL pattern for CDP `Fetch.enable` (default: `https://sarathi.parivahan.gov.in/*`). |
| `--no-cache` | Bypass reading/writing `.seb_tree_cache.json`. |
| `--launch` | Launch Chrome/Edge with CDP header injection, stealth, and `DEFUSER_JS`. |
| `-v`, `--verbose` | Log timing, cache state, job binding, and CDP events to `stderr` (keeps `stdout` clean). |
