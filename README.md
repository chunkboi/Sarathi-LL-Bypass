# 🛡️ Sarathi SEB Emulator & Proctoring Defuser

![Python Version](https://img.shields.io/badge/Python-3.9%2B-blue)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green)

A standalone, automated Python tool designed to emulate **Safe Exam Browser (SEB)** for reverse-engineering and security research on the Sarathi Parivahan LL Test. 

It dynamically generates valid SEB integrity keys, intercepts network requests to bypass server-side SEB validation, and injects a stealth JavaScript payload via CDP (Chrome DevTools Protocol) to neutralize client-side anti-debug and proctoring mechanisms.

> **⚠️ DISCLAIMER**  
> This tool is provided strictly for **educational and security research purposes**. Bypassing exam security mechanisms, altering proctoring data, or cheating on official government examinations is illegal and violates terms of service. The author assumes no liability for any misuse of this software. Do not use this on live exams.

---

## ✨ Features

- **Native Windows API Hashing:** Utilizes `ctypes` to natively read PE file metadata and Authenticode signatures via `crypt32.dll` and `version.dll` (fast, reliable, no PowerShell spawning).
- **Multithreaded Tree Fingerprinting:** Computes the SEB installation tree hash in parallel using `ThreadPoolExecutor`.
- **Smart Caching:** Caches the SEB tree state and `.seb` configuration keys to skip heavy computation on subsequent runs.
- **Dynamic HTTP Header Injection:** Intercepts every network request via Playwright routing and dynamically calculates the `X-SafeExamBrowser-RequestHash` for the exact URL, bypassing server-side 403 Forbidden errors.
- **CDP Stealth Injection:** Injects a custom JavaScript payload at `document-start` using `patchright`.
- **Anti-Debug & Tamper Bypass:** Defeats `debugger` timing traps, `Function.toString()` checks, history traps, and forced logouts.
- **Proctoring Bypass:** Completely disables the ONNX face-spoofing model and virtual camera detection systems.
- **Developer Console (`__cmp`):** Exposes the Angular component instance for manual manipulation of questions, answers, and proctoring state.

---

## 🛠️ Prerequisites

1. **Windows OS:** Required to read the local SEB installation (`seb_x64.dll` and `SafeExamBrowser.exe`).
2. **Python 3.9+**
3. **Safe Exam Browser (SEB) Installed:** The script uses your local SEB installation to calculate the correct integrity keys.
4. **Required Python Packages:**
   ```bash
   pip install rncryptor pefile patchright
   ```
   *(Note: `patchright` is a stealth-patched version of Playwright, required to bypass advanced browser fingerprinting).*

---

## 🚀 Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/sarathi-seb-emulator.git
   cd sarathi-seb-emulator
   ```

2. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

3. Place your `.seb` exam configuration file in the project directory.

---

## 📖 Usage

The script requires a `.seb` configuration file to extract the exam settings and starting URL. 

### Basic Launch
```bash
py -3 seb_keys.py exam.seb --launch
```

### CLI Arguments
| Argument | Description |
| :--- | :--- |
| `seb` | **(Required)** Path to the `.seb` configuration file. |
| `--launch` | Launch Chrome via Patchright with SEB headers and open the start URL. |
| `--root` | Manually specify the SEB installation directory (e.g., `"C:\Program Files\SafeExamBrowser"`). |
| `--password` | Provide a password if the `.seb` file is `pwcc` encrypted. |
| `--no-cache` | Force the script to recalculate the SEB installation tree hash. |

### Example Output
```text
configKey=4a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b
browserExamKey=f1e2d3c4b5a6...
Launched Chrome via Patchright -> https://sarathi.parivahan.gov.in/...
[Python] Dynamic RequestHasher and Defuser JS injected.
```

---

## 🧠 How It Works

The script operates in three distinct phases:

### 1. Key Extraction & Tree Hashing
The script scans your local SEB installation and the provided `.seb` file.
- Extracts the integrity salt from `seb_x64.dll`.
- Calculates the SHA-256 hash of the SEB installation tree (including PE versions and digital signatures).
- Decrypts the `.seb` file (handling `gzip`, `plnd`, `pwcc`, `pswd`) to generate the `ConfigKey` and `BrowserExamKey (BEK)`.

### 2. Dynamic Network Spoofing
When Patchright launches Chrome, the script intercepts all outgoing HTTP traffic to the target domain.
- Real SEB generates a unique `RequestHash` for *every single URL* it requests.
- The script intercepts the request, strips the URL fragment, calculates `SHA256(URL + BEK)`, and injects the `X-SafeExamBrowser-RequestHash` header before letting the request proceed. This tricks the server into thinking a legitimate SEB client is browsing.

### 3. JavaScript Injection (CDP)
The script injects a JS payload into the browser context before the page loads.
- **Stealth Patches:** Overrides `Function.prototype.toString()` to return `[native code]`, hiding the fact that functions have been monkey-patched.
- **Fake SEB:** Creates a fake `window.SafeExamBrowser` object.
- **Trap Disabling:** Blocks `debugger` statements in timers, stops storage wipes, and prevents history traps.
- **ONNX/Camera Killswitch:** Overrides `runProctorSpoofCheck` and `runCameraSecurityCheck` to instantly return clean, passing results without loading the heavy AI models.

---

## 💻 Developer Console (`__cmp`)

Because the frontend Angular app is running, the script captures the main component instance and exposes it globally as `window.__cmp`. You can use the DevTools Console (F12) to manipulate the exam.

**Check Proctoring Status:**
```javascript
// Returns a fake, clean result instantly
__cmp.proctorService.runProctorSpoofCheck() 

// Returns a fake, clean result instantly
__cmp.anti.runCameraSecurityCheck()
```

**Access Exam Data:**
```javascript
// Dump all questions to the console
__cmp.questions

// Export questions to clipboard (copy to a text file)
copy(JSON.stringify(__cmp.questions, null, 2))

// View your current selected answers
__cmp.selectedAnswers
```

**Trigger Submit:**
```javascript
// Force the exam to submit
__cmp.onFinalSubmit()
```

---

## 🔧 Troubleshooting

**"Could not locate SEB installation"**
Pass the root path manually: `--root "C:\Program Files\SafeExamBrowser"`

**403 Forbidden Errors on specific API calls**
Some backends are strict about URL query strings (`?t=1234`). If you get 403s during the exam, open the script, find `route_handler`, and change:
`clean_url = req_url.split("#", 1)[0]` to `clean_url = req_url.split("#")[0].split("?")[0]`

**Debugger still pausing execution?**
If DevTools is open, the browser pauses *before* the script can strip the `debugger` word from the JS. Keep DevTools closed until the page loads, or use the DevTools "Deactivate breakpoints" (Ctrl+F8) immediately upon opening.

**The script is ignoring my `.seb` changes**
Delete the `.seb_tree_cache.json` file in the same directory, or run with `--no-cache`.

---

## 📄 License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.