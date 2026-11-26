
# 🛡️ Sarathi Parivahan LL Test: SEB Emulator & Proctor Defuser

![Target](https://img.shields.io/badge/Target-Sarathi%20Parivahan-red)
![Angular](https://img.shields.io/badge/Frontend-Angular%2020-red)
![ONNX](https://img.shields.io/badge/AI%20Defeated-ONNX%20FaceMesh-orange)
![License](https://img.shields.io/badge/License-MIT-green)

A targeted, stealth-automation framework designed to reverse-engineer and emulate Safe Exam Browser (SEB) for the **Sarathi Parivahan Learner's License (LL) Test**. 

It dynamically spoofs the SEB cryptographic handshake to bypass server-side 403 Forbidden errors, and injects a heavily customized CDP (Chrome DevTools Protocol) payload to neutralize the frontend's Angular 20 anti-tamper mechanisms, ONNX AI proctoring, and camera security checks.

> **⚠️ STRICT DISCLAIMER**  
> This repository is for **security research and educational reverse-engineering only**. Circumventing government exam proctoring, altering ONNX AI verdicts, or spoofing SEB integrity keys on live servers is illegal and violates the IT Act. The author assumes zero liability for misuse. Do not run this against live, production endpoints.

---

## 🎯 The Target Architecture

Through reverse-engineering the Sarathi Parivahan web app, the following security layers were identified and defeated:

1. **Server-Side SEB Handshake:** The backend validates the `X-SafeExamBrowser-RequestHash` header against `SHA256(RequestURL + BrowserExamKey)`.
2. **Anti-Debug Traps (`chunk-MNWLHHGD.js`):** A 1-second `setInterval` runs a `debugger;` statement. If the timing delta is >100ms, it assumes DevTools is open and calls `forceLogout()`.
3. **ONNX AI Proctoring (`chunk-2FCFBNO4.js`):** The `Ku` service loads `facemesh` and a custom classifier. It checks for spoofed faces via `runProctorSpoofCheck()`.
4. **Camera Security (`Pu` service):** Checks for virtual cameras (OBS, ManyCam), multiple webcams, and frozen video streams (replay attacks).
5. **Navigation Traps:** Injects `s1_...` dummy `pushState` entries to trap the back button and block Alt+Left/Backspace.
6. **State Wipes:** On detection, it wipes the `LLTestCacheDB` IndexedDB and clears `sessionStorage`.

---

## 🚀 Features & Exploits

- **`__cmp` Angular 20 Component Hijack:** Angular 20 hides component instances via numeric `__ngContext__` IDs. This script hooks `Function.prototype.call` and `.apply` to magically capture the component instance globally as `window.__cmp`.
- **Dynamic Header Injection (Bypasses 403s):** Intercepts every Playwright route request, calculates the exact `SHA256(URL + BEK)` for the specific file, and injects the SEB headers.
- **Total ONNX Bypass:** Overrides the `runProctorSpoofCheck` prototype to instantly return `probReal: 1.0` (LIVE), skipping the heavy AI facemesh computation entirely.
- **Camera Security Killswitch:** Overrides `runCameraSecurityCheck` to always report zero virtual cameras and zero frozen streams.
- **Native PE Hashing:** Uses `ctypes` to read `seb_x64.dll` metadata and Authenticode signatures via `crypt32.dll` (no slow PowerShell spawning).
- **Stealth `.toString()`:** Patches all modified prototype methods to return `[native code]`, blinding Angular's anti-tamper checks.

---

## 🛠️ Prerequisites

1. **Windows OS** (Required to read the local SEB installation).
2. **Python 3.9+**
3. **Safe Exam Browser (SEB) Installed** (e.g., `C:\Program Files\SafeExamBrowser`).
4. **Python Packages:**
   ```bash
   pip install rncryptor pefile patchright
   ```
   *(Note: `patchright` is a stealth-patched fork of Playwright required to bypass advanced Chromium fingerprinting).*

---

## 📖 Usage

You must have the `.seb` configuration file provided by the Sarathi portal.

### Standard Launch
```bash
py -3 seb_keys.py exam.seb --launch
```

### CLI Arguments
| Flag | Description |
| :--- | :--- |
| `seb` | **(Required)** Path to the `.seb` file. |
| `--launch` | Launches Patchright, injects headers/JS, and opens the exam URL. |
| `--root` | Manually specify SEB install path (e.g., `--root "D:\SafeExamBrowser"`). |
| `--password` | Provide a password if the `.seb` is `pwcc` encrypted. |
| `--no-cache` | Forces re-calculation of the SEB installation tree fingerprint. |

---

## 🧠 The `__cmp` Developer Console

Once the exam page loads, open DevTools (F12). The script will output `[defuser] 🎯 __cmp CAPTURED!`. You now have full control over the Angular component.

### 1. Exfiltrate Questions
The app caches questions in IndexedDB (`LLTestCacheDB`). You can dump them directly:
```javascript
// Dump questions to console
await __cmp.indexedDb.getExamData()

// Copy questions to clipboard as JSON
copy(JSON.stringify(await __cmp.indexedDb.getExamData(), null, 2))
```

### 2. Verify the AI is Dead
Check that the ONNX models and camera security checks are fully neutralized:
```javascript
// Should instantly return { spoofDetected: false, probReal: 1.0 }
await __cmp.proctorService.runProctorSpoofCheck(__cmp.videoRef.nativeElement)

// Should instantly return { status: "ok", hasVirtualCamera: false, replayLikely: false }
await __cmp.anti.runCameraSecurityCheck(null, __cmp.videoRef.nativeElement)
```

### 3. Force Submission
If you want to skip the UI and submit immediately:
```javascript
__cmp.onFinalSubmit()
```

### 4. Bypass "Minimum Answer Time"
The exam enforces `tMinAnsTime` (e.g., 30 seconds) before `canFinalSubmit` becomes true. You can bypass this lock:
```javascript
__cmp.canFinalSubmit = true;
__cmp.cd.detectChanges();
```

---

## 🛡️ How the Defuser Works (Under the Hood)

1. **Bypassing the `debugger` Trap:** 
   The script hooks `window.setInterval` and inspects the function source. If it contains `debugger;`, it drops the timer completely. It also overrides `Object.defineProperty` to block the console-getter trap on `Image.prototype.id`.
2. **Defeating the History Trap:**
   The app calls `pushHistoryBuffer()` which pushes dummy state objects like `{ uid: "s1_1795684725204_jzfcgztcn" }`. The script hooks `History.prototype.pushState` and silently drops any state matching `/^s\d+_\d+_[a-z0-9]+$/i`.
3. **Frozen Stream Detection Evasion:**
   The `Pu.detectFrozenStream()` method grabs 5 frames via `getImageData(0,0,64,48)`. The script hooks `CanvasRenderingContext2D.prototype.getImageData` and injects 0-2 random RGB noise pixels into arrays matching `64x48`, ensuring every frame looks unique.
4. **Storage Wipe Protection:**
   The `forceLogout()` method calls `sessionStorage.clear()` and `indexedDB.deleteDatabase('LLTestCacheDB')`. The script hooks `Storage.prototype.clear` to back up and restore `examData`, `answers`, and `loginData`. It also fakes a successful IDB deletion.

---
```

```
