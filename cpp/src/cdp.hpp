#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "json.hpp"
#include "plist.hpp"
#include "seb.hpp"
#include "win_crypto.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seb::cdp {

inline constexpr const char* DEFAULT_URL = "https://sarathi.parivahan.gov.in/onlinelltestbnd/exam/sebCheck";

inline constexpr const char* DEFUSER_JS = R"JS(
(() => {
  'use strict';
// --- STEALTH UTILITY (must come first) ---
  const stealth = (fn, name) => {
    const nativeStr = `function ${name}() { [native code] }`;
    Object.defineProperty(fn, 'toString', { value: () => nativeStr, configurable: true, writable: true });
    return fn;
  };
const _origFetch = window.fetch;
 window.fetch = function (input, init) {
  const url = typeof input === "string" ? input : input?.url || "";
  if (url.includes("favicon.ico")) {
    return Promise.resolve(new Response("", { status: 200, statusText: "OK" }));
  }
  return Reflect.apply(_origFetch, this, arguments);
};
  // 1. FAKE SEB ENVIRONMENT
  window.SafeExamBrowser = {
    version: "3.10.0",
    security: { isScreenCaptureAllowed: () => false, isVirtualMachine: () => false },
    browser: { isRunning: () => true }
  };

  // 2. CRYPTO INTERCEPTOR (Dump Passphrase, Salt, and Payloads)
  (function interceptCrypto() {
     if (!window.crypto || !window.crypto.subtle) return;
 
     const origImportKey = crypto.subtle.importKey.bind(crypto.subtle);
     crypto.subtle.importKey = stealth(async function (format, keyData, algorithm, extractable, keyUsages) {
       if (format === 'raw' && algorithm && algorithm.name === 'PBKDF2') {
         try {
           const passphrase = new TextDecoder().decode(keyData);
           if (!window.__SEB_PASSPHRASE) {
             window.__SEB_PASSPHRASE = passphrase;
             console.log('%c[defuser] 🔑 CAPTURED PASSPHRASE:', 'color:#ff0;font-weight:bold;font-size:14px', passphrase);
           }
         } catch (e) {}
       }
       return origImportKey(format, keyData, algorithm, extractable, keyUsages);
     }, 'importKey');
 
     const origDeriveKey = crypto.subtle.deriveKey.bind(crypto.subtle);
     crypto.subtle.deriveKey = stealth(async function (algorithm, baseKey, derivedKeyType, extractable, keyUsages) {
       if (algorithm && algorithm.name === 'PBKDF2' && algorithm.salt) {
         try {
           const salt = new TextDecoder().decode(algorithm.salt);
           if (!window.__SEB_SALT) {
             window.__SEB_SALT = salt;
             console.log('%c[defuser] 🧂 CAPTURED SALT:', 'color:#ff0;font-weight:bold;font-size:14px', salt);
           }
         } catch (e) {}
       }
       return origDeriveKey(algorithm, baseKey, derivedKeyType, extractable, keyUsages);
     }, 'deriveKey');
 
     const origEncrypt = crypto.subtle.encrypt.bind(crypto.subtle);
     crypto.subtle.encrypt = stealth(async function (algorithm, key, data) {
       try {
         const text = new TextDecoder().decode(data);
         if (text.includes('{')) {
           console.log('%c[defuser] 🔒 ENCRYPTING PAYLOAD:', 'color:#f80', JSON.parse(text));
         }
       } catch (e) {}
       return origEncrypt(algorithm, key, data);
     }, 'encrypt');
 
     const origDecrypt = crypto.subtle.decrypt.bind(crypto.subtle);
     crypto.subtle.decrypt = stealth(async function (algorithm, key, data) {
       const result = await origDecrypt(algorithm, key, data);
       try {
         const text = new TextDecoder().decode(result);
         if (text.includes('{')) {
           console.log('%c[defuser] 🔓 DECRYPTED PAYLOAD:', 'color:#0f0', JSON.parse(text));
         }
       } catch (e) {}
       return result;
     }, 'decrypt');
   })();
  // 3. EXPOSE ANGULAR COMPONENT (__cmp)
  (function captureComponent() {
    const origCall = Function.prototype.call;
    const origApply = Function.prototype.apply;
    const check = (thisArg) => {
      if (window.__cmp || !thisArg || typeof thisArg !== 'object') return;
      if (thisArg.forceLogout || thisArg.onFinalSubmit || thisArg.pollFrame || thisArg.proctorService) {
        window.__cmp = thisArg;
        console.log('%c[defuser] 🎯 __cmp CAPTURED!', 'color:#0f0;font-weight:bold');
        // Capture is one-time. Restore the native methods so the page does
        // not pay for the hook on every subsequent call/apply.
        Function.prototype.call = origCall;
        Function.prototype.apply = origApply;
      }
    };
    Function.prototype.call = stealth(function (thisArg) {
      check(thisArg);
      // `arguments` is already array-like; avoid allocating [thisArg, ...args]
      // on every .call() in the page.
      return Reflect.apply(origCall, this, arguments);
    }, 'call');

    Function.prototype.apply = stealth(function (thisArg, args) {
      check(thisArg);
      return Reflect.apply(origApply, this, arguments);
    }, 'apply');
  })();
 

  // 4. PATCH SERVICES (ONNX & CAMERA)
   (function patchProctoringServices() {
     const interval = setInterval(() => {
       if (window.__cmp && window.__cmp.proctorService && window.__cmp.anti) {
         clearInterval(interval);
 
         const proctorProto = Object.getPrototypeOf(window.__cmp.proctorService);
         if (typeof proctorProto.runProctorSpoofCheck === 'function' &&
             !proctorProto.runProctorSpoofCheck.__patched) {
           proctorProto.runProctorSpoofCheck = stealth(async function () {
             return { finalDecision: true, bufferReady: true, spoofDetected: false, probReal: 1.0 };
           }, 'runProctorSpoofCheck');
           proctorProto.runProctorSpoofCheck.__patched = true;
           console.log('%c[defuser] 🧠 ONNX Proctor bypassed.', 'color:#0f0');
         }
 
         const antiProto = Object.getPrototypeOf(window.__cmp.anti);
         if (typeof antiProto.runCameraSecurityCheck === 'function' &&
             !antiProto.runCameraSecurityCheck.__patched) {
           antiProto.runCameraSecurityCheck = stealth(async function () {
             return { status: "ok", hasVirtualCamera: false, hasMultipleCameras: false, suspiciousDevices: [], replayLikely: false, browserTampered: false };
           }, 'runCameraSecurityCheck');
           antiProto.runCameraSecurityCheck.__patched = true;
           console.log('%c[defuser] 📷 Camera Security bypassed.', 'color:#0f0');
         }
       }
     }, 500);
   })();

  // 5. DEFUSE TIMERS & CONTEXT MENU
  (function neutralizeTraps() {
    const hasDebugger = (src) => /\bdebugger\b/.test(src);
    const antiDebugCache = new WeakMap();
    function isAntiDebugFn(fn) {
      if (typeof fn !== 'function') return false;
      const cached = antiDebugCache.get(fn);
      if (cached !== undefined) return cached;
      let result = false;
      try {
        // Reflect.apply avoids the page-wide Function.prototype.call hook.
        result = hasDebugger(Reflect.apply(Function.prototype.toString, fn, []));
      } catch {}
      antiDebugCache.set(fn, result);
      return result;
    }
    function wrapTimer(orig, name) {
      return stealth(function (fn) {
        if (isAntiDebugFn(fn)) return 0;
        return Reflect.apply(orig, this, arguments);
      }, name);
    }

    window.setInterval = wrapTimer(window.setInterval, 'setInterval');
    window.setTimeout  = wrapTimer(window.setTimeout,  'setTimeout');
    window.requestAnimationFrame = wrapTimer(window.requestAnimationFrame, 'requestAnimationFrame');
    window.addEventListener('contextmenu', (e) => e.stopImmediatePropagation(), { capture: true });

    const origDP = Object.defineProperty;
    Object.defineProperty = stealth(function (obj, key, desc) {
      if (key === 'id' && desc && typeof desc.get === 'function' && obj instanceof Image) return obj;
      return Reflect.apply(origDP, this, arguments);
    }, 'defineProperty');
  })();

  // 6. DEFUSE KEYS & HISTORY & STORAGE
  window.addEventListener('keydown', (e) => {
    const tag = e.target?.tagName;
    if ((e.altKey && e.key === 'ArrowLeft') || (e.key === 'Backspace' && tag !== 'INPUT' && tag !== 'TEXTAREA')) {
      e.stopImmediatePropagation();
    }
  }, { capture: true });

  const DUMMY_UID = /^s\d+_\d+_[a-z0-9]+$/i;
  const origPush = History.prototype.pushState;
  History.prototype.pushState = stealth(function (state, title, url) {
    if (state && typeof state === 'object' && DUMMY_UID.test(state.uid || '')) return;
    return Reflect.apply(origPush, this, arguments);
  }, 'pushState');

  const origAssign = Location.prototype.assign;
  Location.prototype.assign = stealth(function (u) {
    if (/SebSetupInstructions|\/logout/i.test(String(u))) return;
    return Reflect.apply(origAssign, this, arguments);
  }, 'assign');

  const origGet = Storage.prototype.getItem;
  Storage.prototype.getItem = stealth(function (k) {
    if (k === 'seb_error_reason') return null;
    return Reflect.apply(origGet, this, arguments);
  }, 'getItem');

  // 7. DEFUSE FINGERPRINTS & CAMERA LABELS
   try {
     const trackDesc = Object.getOwnPropertyDescriptor(MediaStreamTrack.prototype, 'label');
     if (trackDesc && trackDesc.get) {
       Object.defineProperty(MediaStreamTrack.prototype, 'label', {
         get: stealth(function () { return 'Integrated Webcam'; }, 'get'),
         configurable: true
       });
     }
   } catch {}
 
   try { if (navigator.webdriver) Object.defineProperty(Navigator.prototype, 'webdriver', { get: () => false, configurable: true, enumerable: true }); } catch {}


  // 8. Neutralize WebGPU / WebNN availability for isolated testing
  try {
    Object.defineProperty(navigator, 'gpu', { value: undefined, configurable: true });
  } catch {}
  try {
    Object.defineProperty(navigator, 'ml', { value: undefined, configurable: true });
  } catch {}

  // 9. Stub heavy ML constructors cleanly without background intervals
  const noopAsync = async () => {};
  const makeStub = (name) =>
    stealth(function () {
      console.log('[defuser] stub-ctor', name);
      return {
        setOptions: () => {},
        onResults: () => {},
        initialize: noopAsync,
        reset: () => {},
        send: noopAsync,
        close: noopAsync,
      };
    }, name);

  for (const clsName of ['Solution', 'FaceMesh']) {
    if (typeof globalThis[clsName] === 'function') {
      try {
        globalThis[clsName] = makeStub(clsName);
      } catch {}
    }
  }

  // . Disable CSS animations/transitions to reduce frame/repaint noise during tests
  const style = document.createElement('style');
  style.id = 'test-defuser-no-anim';
  style.textContent = `
    *, *::before, *::after {
      animation: none !important;
      transition: none !important;
    }
  `;
  (document.head || document.documentElement).appendChild(style);

  console.log('[defuser] All stealth bypasses active. Waiting for crypto payloads...');
})();
)JS";

inline constexpr const char* STEALTH_JS = R"JS(
(() => {
  'use strict';
  try {
    if (navigator.webdriver) {
      Object.defineProperty(Navigator.prototype, 'webdriver', {
        get: () => false,
        configurable: true,
        enumerable: true
      });
    }
  } catch {}
  try {
    if (!window.chrome) {
      Object.defineProperty(window, 'chrome', {
        value: { runtime: {}, app: { isInstalled: false }, csi: () => ({}), loadTimes: () => ({}) },
        configurable: true,
        enumerable: true,
        writable: true
      });
    }
  } catch {}
  try {
    if (!navigator.languages || navigator.languages.length === 0) {
      Object.defineProperty(Navigator.prototype, 'languages', {
        get: () => ['en-US', 'en'],
        configurable: true
      });
    }
  } catch {}
  try {
    for (const obj of [window, document]) {
      for (const k of Object.getOwnPropertyNames(obj)) {
        if (/^(\$?cdc_|__webdriver|__selenium|__fxdriver|__playwright|domAutomation)/.test(k)) {
          try { delete obj[k]; } catch {}
        }
      }
    }
  } catch {}
})();
)JS";

inline std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// ASCII-only case-insensitive search. This matches the previous
// "lowercase the whole document, then find()" behavior without allocating a
// second copy of every HTML document and script.
inline size_t find_ci_ascii(std::string_view hay, std::string_view needle, size_t pos = 0) {
    if (needle.empty()) return pos <= hay.size() ? pos : std::string_view::npos;
    if (pos > hay.size() || hay.size() - pos < needle.size()) return std::string_view::npos;
    auto lower = [](unsigned char c) -> unsigned char {
        return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
    };
    const unsigned char first = lower(static_cast<unsigned char>(needle[0]));
    for (size_t i = pos; i + needle.size() <= hay.size(); ++i) {
        if (lower(static_cast<unsigned char>(hay[i])) != first) continue;
        size_t j = 1;
        while (j < needle.size() &&
               lower(static_cast<unsigned char>(hay[i + j])) == lower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == needle.size()) return i;
    }
    return std::string_view::npos;
}

inline std::string build_defuser_tag() {
    std::string js(DEFUSER_JS);
    std::string replaced;
    replaced.reserve(js.size() + 16);
    size_t last = 0;
    constexpr std::string_view needle = "</script>";
    while (true) {
        size_t pos = find_ci_ascii(js, needle, last);
        if (pos == std::string::npos) break;
        replaced.append(js, last, pos - last);
        replaced += R"(<\/script>)";
        last = pos + needle.size();
    }
    replaced.append(js, last, js.size() - last);
    return "<script>" + replaced + "</script>";
}

[[nodiscard]] inline std::string inject_defuser_html(const std::string& html, const std::string& defuser_tag) {
    size_t pos = 0;
    while ((pos = find_ci_ascii(html, "<head", pos)) != std::string::npos) {
        char next = (pos + 5 < html.size()) ? html[pos + 5] : '>';
        if (next == '>' || std::isspace(static_cast<unsigned char>(next))) {
            size_t gt = std::string::npos;
            char quote = 0;
            for (size_t p = pos + 5; p < html.size(); ++p) {
                char ch = html[p];
                if (quote != 0) {
                    if (ch == quote) quote = 0;
                } else if (ch == '"' || ch == '\'') {
                    quote = ch;
                } else if (ch == '>') {
                    gt = p;
                    break;
                }
            }
            if (gt != std::string::npos) {
                std::string out;
                out.reserve(html.size() + defuser_tag.size());
                out.append(html, 0, gt + 1);
                out += defuser_tag;
                out.append(html, gt + 1, std::string::npos);
                return out;
            }
        }
        pos += 5;
    }
    std::string out;
    out.reserve(defuser_tag.size() + html.size());
    out += defuser_tag;
    out += html;
    return out;
}

[[nodiscard]] inline std::wstring find_chrome_exe() {
    const wchar_t* fixed_candidates[] = {
        LR"(C:\Program Files\Google\Chrome\Application\chrome.exe)",
        LR"(C:\Program Files (x86)\Google\Chrome\Application\chrome.exe)",
    };
    for (const wchar_t* c : fixed_candidates) {
        if (seb::is_file_w(c)) return c;
    }
    DWORD need = GetEnvironmentVariableW(L"LocalAppData", nullptr, 0);
    if (need > 0 && need < 32768) {
        std::wstring local(need, L'\0');
        DWORD n = GetEnvironmentVariableW(L"LocalAppData", local.data(), need);
        if (n > 0 && n < need) {
            local.resize(n);
            std::wstring p1 = seb::join_path_w(local, LR"(Google\Chrome\Application\chrome.exe)");
            if (seb::is_file_w(p1.c_str())) return p1;
            std::wstring p2 = seb::join_path_w(local, LR"(Microsoft\Edge\Application\msedge.exe)");
            if (seb::is_file_w(p2.c_str())) return p2;
        }
    }
    const wchar_t* edge_candidates[] = {
        LR"(C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe)",
        LR"(C:\Program Files\Microsoft\Edge\Application\msedge.exe)",
    };
    for (const wchar_t* c : edge_candidates) {
        if (seb::is_file_w(c)) return c;
    }
    throw std::runtime_error("Could not locate chrome.exe or msedge.exe");
}

inline void create_directories_w(const std::wstring& dir) {
    if (dir.empty() || seb::is_dir_w(dir.c_str())) return;
    std::wstring_view parent = seb::parent_path_w(dir);
    if (!parent.empty() && parent.size() < dir.size()) {
        create_directories_w(std::wstring(parent));
    }
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS || !seb::is_dir_w(dir.c_str())) {
            throw std::runtime_error("CreateDirectoryW failed (err=" + std::to_string(err) + ")");
        }
    }
}

struct WsaGuard {
    WsaGuard() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WsaGuard() { WSACleanup(); }
    WsaGuard(const WsaGuard&) = delete;
    WsaGuard& operator=(const WsaGuard&) = delete;
};

struct SocketGuard {
    SOCKET s = INVALID_SOCKET;
    SocketGuard() = default;
    explicit SocketGuard(SOCKET sock) noexcept : s(sock) {}
    ~SocketGuard() { reset(); }
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
    SocketGuard(SocketGuard&& o) noexcept : s(std::exchange(o.s, INVALID_SOCKET)) {}
    SocketGuard& operator=(SocketGuard&& o) noexcept {
        if (this != &o) {
            reset();
            s = std::exchange(o.s, INVALID_SOCKET);
        }
        return *this;
    }
    void reset() noexcept {
        if (s != INVALID_SOCKET) {
            closesocket(s);
            s = INVALID_SOCKET;
        }
    }
    SOCKET get() const noexcept { return s; }
    SOCKET release() noexcept { return std::exchange(s, INVALID_SOCKET); }
};

[[nodiscard]] inline uint16_t free_local_port() {
    SocketGuard sg(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (sg.get() == INVALID_SOCKET) throw std::runtime_error("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sg.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error("bind failed");
    }
    int len = sizeof(addr);
    if (getsockname(sg.get(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        throw std::runtime_error("getsockname failed");
    }
    return ntohs(addr.sin_port);
}

[[nodiscard]] inline SOCKET connect_local(uint16_t port, DWORD timeout_ms = 0) {
    SocketGuard sg(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (sg.get() == INVALID_SOCKET) throw std::runtime_error("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (timeout_ms > 0) {
        u_long nb = 1;
        ioctlsocket(sg.get(), static_cast<long>(FIONBIO), &nb);
        int rc = connect(sg.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc != 0) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                throw std::runtime_error("connect failed");
            }
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(sg.get(), &wfds);
            FD_SET(sg.get(), &efds);
            timeval tv{};
            tv.tv_sec = static_cast<long>(timeout_ms / 1000);
            tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
            int sel = select(0, nullptr, &wfds, &efds, &tv);
            if (sel <= 0 || FD_ISSET(sg.get(), &efds) || !FD_ISSET(sg.get(), &wfds)) {
                throw std::runtime_error("connect timeout/refused");
            }
            // select() write-ready doesn't guarantee success on Windows —
            // a RST can briefly appear writable. Verify via SO_ERROR.
            int sock_err = 0;
            int soe_len = sizeof(sock_err);
            if (getsockopt(sg.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sock_err), &soe_len) != 0 || sock_err != 0) {
                throw std::runtime_error("connect failed (SO_ERROR)");
            }
        }
        u_long blk = 0;
        ioctlsocket(sg.get(), static_cast<long>(FIONBIO), &blk);
        return sg.release();
    }
    if (connect(sg.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error("connect failed");
    }
    return sg.release();
}

[[nodiscard]] inline bool send_all(SOCKET s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int chunk = static_cast<int>((std::min<size_t>)(len - sent, 0x40000000ULL));
        int n = send(s, reinterpret_cast<const char*>(data + sent), chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

[[nodiscard]] inline bool recv_exact(SOCKET s, uint8_t* dst, size_t len) {
    size_t got = 0;
    while (got < len) {
        int chunk = static_cast<int>((std::min<size_t>)(len - got, 0x40000000ULL));
        int n = recv(s, reinterpret_cast<char*>(dst + got), chunk, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

[[nodiscard]] inline std::string http_get_local(uint16_t port, const std::string& path) {
    SocketGuard sg(connect_local(port, 50));
    DWORD tv = 1000;
    setsockopt(sg.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nConnection: close\r\n\r\n";
    if (!send_all(sg.get(), reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
        throw std::runtime_error("HTTP send failed");
    }
    std::string raw;
    char buf[4096];
    size_t sep = std::string::npos;
    size_t scan_pos = 0;
    size_t content_len = std::string::npos;
    while (true) {
        int n = recv(sg.get(), buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > 64ULL * 1024 * 1024) {
            throw std::runtime_error("HTTP response too large");
        }
        if (sep == std::string::npos) {
            sep = raw.find("\r\n\r\n", scan_pos);
            if (sep == std::string::npos) {
                scan_pos = (raw.size() >= 3) ? (raw.size() - 3) : 0;
            } else {
                std::string hdr_lower = to_lower_ascii(raw.substr(0, sep));
                size_t line_end = hdr_lower.find("\r\n");
                std::string_view status_line = (line_end == std::string::npos)
                    ? std::string_view(hdr_lower)
                    : std::string_view(hdr_lower).substr(0, line_end);
                if (!status_line.starts_with("http/1.") || status_line.size() < 12) {
                    throw std::runtime_error("invalid HTTP status line");
                }
                size_t sp1 = status_line.find(' ');
                if (sp1 == std::string_view::npos) throw std::runtime_error("invalid HTTP status line");
                while (sp1 < status_line.size() && status_line[sp1] == ' ') ++sp1;
                int status_code = 0;
                auto [sp_ptr, sp_ec] = std::from_chars(
                    status_line.data() + sp1, status_line.data() + status_line.size(), status_code);
                if (sp_ec != std::errc{} || status_code < 200 || status_code >= 300) {
                    throw std::runtime_error("HTTP error status: " + std::to_string(status_code));
                }

                // Parse headers line by line
                size_t hpos = (line_end == std::string::npos) ? hdr_lower.size() : line_end + 2;
                while (hpos < hdr_lower.size()) {
                    size_t next_eol = hdr_lower.find("\r\n", hpos);
                    std::string_view line = (next_eol == std::string::npos)
                        ? std::string_view(hdr_lower).substr(hpos)
                        : std::string_view(hdr_lower).substr(hpos, next_eol - hpos);
                    if (line.starts_with("transfer-encoding:") &&
                        line.find("chunked") != std::string_view::npos) {
                        throw std::runtime_error("unsupported chunked transfer-encoding");
                    }
                    if (line.starts_with("content-length:")) {
                        std::string_view val = line.substr(15);
                        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.remove_suffix(1);
                        uint64_t parsed_cl = 0;
                        auto [cl_ptr, cl_ec] = std::from_chars(val.data(), val.data() + val.size(), parsed_cl);
                        if (cl_ec != std::errc{} || cl_ptr != val.data() + val.size() ||
                            parsed_cl > 64ULL * 1024 * 1024) {
                            throw std::runtime_error("invalid HTTP Content-Length");
                        }
                        content_len = static_cast<size_t>(parsed_cl);
                    }
                    if (next_eol == std::string::npos) break;
                    hpos = next_eol + 2;
                }
            }
        }
        if (sep != std::string::npos && content_len != std::string::npos &&
            raw.size() - (sep + 4) >= content_len) {
            break;
        }
    }
    if (sep == std::string::npos) throw std::runtime_error("invalid HTTP response");
    if (content_len != std::string::npos) {
        if (raw.size() - (sep + 4) < content_len) {
            throw std::runtime_error("truncated HTTP response body");
        }
        return raw.substr(sep + 4, content_len);
    }
    return raw.substr(sep + 4);
}

struct WsClient {
    SOCKET s = INVALID_SOCKET;
    std::vector<uint8_t> rx_buf;
    size_t rx_off = 0;

    WsClient() = default;
    ~WsClient() { reset(); }
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;
    WsClient(WsClient&& o) noexcept
        : s(std::exchange(o.s, INVALID_SOCKET)),
          rx_buf(std::move(o.rx_buf)),
          rx_off(std::exchange(o.rx_off, 0)) {}
    WsClient& operator=(WsClient&& o) noexcept {
        if (this != &o) {
            reset();
            s = std::exchange(o.s, INVALID_SOCKET);
            rx_buf = std::move(o.rx_buf);
            rx_off = std::exchange(o.rx_off, 0);
        }
        return *this;
    }

    void reset() noexcept {
        if (s != INVALID_SOCKET) {
            closesocket(s);
            s = INVALID_SOCKET;
        }
        rx_buf.clear();
        rx_off = 0;
    }

    [[nodiscard]] bool read_exact_buffered(uint8_t* dst, size_t len) {
        size_t got = 0;
        if (rx_off < rx_buf.size()) {
            size_t avail = rx_buf.size() - rx_off;
            size_t take = (std::min)(avail, len);
            std::memcpy(dst, rx_buf.data() + rx_off, take);
            rx_off += take;
            got += take;
            if (rx_off == rx_buf.size()) {
                rx_buf.clear();
                rx_off = 0;
            }
        }
        if (got < len) {
            return recv_exact(s, dst + got, len - got);
        }
        return true;
    }

    [[nodiscard]] static WsClient connect_ws(const std::string& ws_url) {
        const std::string_view prefix = "ws://";
        if (!std::string_view(ws_url).starts_with(prefix)) {
            throw std::runtime_error("only ws:// supported for local CDP");
        }
        std::string_view rest = std::string_view(ws_url).substr(prefix.size());
        size_t slash = rest.find('/');
        std::string_view host_port = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
        std::string path = (slash == std::string_view::npos) ? "/" : std::string(rest.substr(slash));
        size_t colon = host_port.find(':');
        if (colon == std::string_view::npos) throw std::runtime_error("missing port in ws url");
        std::string_view host = host_port.substr(0, colon);
        if (host != "127.0.0.1" && host != "localhost") {
            throw std::runtime_error("refusing non-loopback ws host");
        }
        std::string_view port_sv = host_port.substr(colon + 1);
        uint32_t port_u = 0;
        auto [p_ptr, p_ec] = std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), port_u);
        if (p_ec != std::errc{} || p_ptr != port_sv.data() + port_sv.size() || port_u == 0 || port_u > 65535) {
            throw std::runtime_error("invalid port in ws url");
        }
        uint16_t port = static_cast<uint16_t>(port_u);

        SocketGuard sg(connect_local(port));
        BOOL nodelay = TRUE;
        setsockopt(sg.get(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
        DWORD hs_tv = 10000;
        setsockopt(sg.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&hs_tv), sizeof(hs_tv));

        uint8_t key_raw[16]{};
        if (BCryptGenRandom(nullptr, key_raw, sizeof(key_raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            throw std::runtime_error("BCryptGenRandom failed for Sec-WebSocket-Key");
        }
        std::string key = win::base64_encode(key_raw, sizeof(key_raw));
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + std::string(host_port) +
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                          "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!send_all(sg.get(), reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
            throw std::runtime_error("ws handshake send failed");
        }

        std::string hdr;
        char chunk[2048];
        size_t hdr_end = std::string::npos;
        size_t scan_pos = 0;
        while (hdr_end == std::string::npos) {
            int n = recv(sg.get(), chunk, sizeof(chunk), 0);
            if (n <= 0) {
                throw std::runtime_error("ws handshake EOF");
            }
            hdr.append(chunk, static_cast<size_t>(n));
            hdr_end = hdr.find("\r\n\r\n", scan_pos);
            if (hdr_end == std::string::npos) {
                if (hdr.size() > 16384) {
                    throw std::runtime_error("ws handshake header too large");
                }
                scan_pos = (hdr.size() >= 3) ? (hdr.size() - 3) : 0;
            }
        }
        if (hdr_end > 16384) {
            throw std::runtime_error("ws handshake header too large");
        }

        // Validate HTTP/1.1 101 status line, Upgrade/Connection headers, and Sec-WebSocket-Accept
        std::string_view hdr_sv(hdr.data(), hdr_end);
        size_t crlf0 = hdr_sv.find("\r\n");
        std::string_view status_line = (crlf0 != std::string_view::npos) ? hdr_sv.substr(0, crlf0) : hdr_sv;
        if (!status_line.starts_with("HTTP/1.1 101") ||
            (status_line.size() > 12 && status_line[12] != ' ')) {
            std::string snippet(status_line.substr(0, (std::min)(status_line.size(), size_t(64))));
            throw std::runtime_error("ws upgrade failed: " + snippet);
        }

        std::string hdr_lower = to_lower_ascii(std::string(hdr_sv));
        bool has_upgrade = false;
        bool has_connection = false;
        std::string accept_val;
        size_t hpos = (crlf0 == std::string_view::npos) ? hdr_lower.size() : crlf0 + 2;
        while (hpos < hdr_lower.size()) {
            size_t next_eol = hdr_lower.find("\r\n", hpos);
            std::string_view line_l = (next_eol == std::string_view::npos)
                ? std::string_view(hdr_lower).substr(hpos)
                : std::string_view(hdr_lower).substr(hpos, next_eol - hpos);
            std::string_view line_orig = (next_eol == std::string_view::npos)
                ? hdr_sv.substr(hpos)
                : hdr_sv.substr(hpos, next_eol - hpos);
            auto trim_sv = [](std::string_view v) {
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
                return v;
            };
            if (line_l.starts_with("upgrade:") && trim_sv(line_l.substr(8)) == "websocket") {
                has_upgrade = true;
            } else if (line_l.starts_with("connection:") &&
                       line_l.substr(11).find("upgrade") != std::string_view::npos) {
                has_connection = true;
            } else if (line_l.starts_with("sec-websocket-accept:")) {
                accept_val = std::string(trim_sv(line_orig.substr(21)));
            }
            if (next_eol == std::string_view::npos) break;
            hpos = next_eol + 2;
        }
        std::string accept_input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        auto accept_sha1 = win::sha1_raw(
            reinterpret_cast<const uint8_t*>(accept_input.data()), accept_input.size());
        std::string expected_accept = win::base64_encode(accept_sha1.data(), accept_sha1.size());
        if (!has_upgrade || !has_connection || accept_val != expected_accept) {
            throw std::runtime_error("ws handshake validation failed");
        }

        DWORD no_tv = 0;
        setsockopt(sg.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&no_tv), sizeof(no_tv));

        WsClient client;
        client.s = sg.release();
        const size_t body_off = hdr_end + 4;
        if (body_off < hdr.size()) {
            client.rx_buf.assign(
                reinterpret_cast<const uint8_t*>(hdr.data() + body_off),
                reinterpret_cast<const uint8_t*>(hdr.data() + hdr.size()));
        }
        return client;
    }

    [[nodiscard]] bool send_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
        // RFC 6455 §5.3: masking key MUST be chosen unpredictably per frame.
        uint8_t mask[4]{};
        if (BCryptGenRandom(nullptr, mask, sizeof(mask), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            return false;
        }
        std::vector<uint8_t> frame;
        frame.reserve(14 + len);
        frame.push_back(static_cast<uint8_t>(0x80 | opcode));
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(0x80 | len));
        } else if (len <= 65535) {
            frame.push_back(0x80 | 126);
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            frame.push_back(0x80 | 127);
            uint64_t ulen = len;
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame.push_back(static_cast<uint8_t>((ulen >> shift) & 0xFF));
            }
        }
        frame.insert(frame.end(), mask, mask + 4);
        const size_t payload_off = frame.size();
        frame.resize(payload_off + len);
        uint8_t* dst = frame.data() + payload_off;
        uint8_t mask8[8] = {mask[0], mask[1], mask[2], mask[3], mask[0], mask[1], mask[2], mask[3]};
        uint64_t mask64 = 0;
        std::memcpy(&mask64, mask8, sizeof(mask64));
        size_t i = 0;
        for (; i + 8 <= len; i += 8) {
            uint64_t chunk = 0;
            std::memcpy(&chunk, payload + i, sizeof(chunk));
            chunk ^= mask64;
            std::memcpy(dst + i, &chunk, sizeof(chunk));
        }
        for (; i < len; ++i) {
            dst[i] = static_cast<uint8_t>(payload[i] ^ mask[i & 3]);
        }
        return send_all(s, frame.data(), frame.size());
    }

    [[nodiscard]] bool send_text(const std::string& text) {
        return send_frame(0x1, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    [[nodiscard]] bool read_message(std::string& out, bool& closed) {
        out.clear();
        closed = false;
        uint8_t first_op = 0;
        constexpr uint64_t MAX_MESSAGE = 256ULL * 1024 * 1024; // 256 MiB
        while (true) {
            uint8_t hdr[2];
            if (!read_exact_buffered(hdr, 2)) return false;
            bool fin = (hdr[0] & 0x80) != 0;
            uint8_t opcode = hdr[0] & 0x0F;
            bool masked = (hdr[1] & 0x80) != 0;
            uint64_t len = hdr[1] & 0x7F;
            if (len == 126) {
                uint8_t b[2];
                if (!read_exact_buffered(b, 2)) return false;
                len = (static_cast<uint64_t>(b[0]) << 8) | b[1];
            } else if (len == 127) {
                uint8_t b[8];
                if (!read_exact_buffered(b, 8)) return false;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[i];
            }
            // Enforce cumulative message limit across all continuation frames.
            if (len > MAX_MESSAGE || len > MAX_MESSAGE - out.size()) return false;
            uint8_t mask[4]{};
            if (masked && !read_exact_buffered(mask, 4)) return false;
            // Receive directly into the output buffer instead of allocating a
            // temporary vector for every frame.
            const size_t base = out.size();
            out.resize(base + static_cast<size_t>(len));
            if (len > 0 &&
                !read_exact_buffered(reinterpret_cast<uint8_t*>(out.data() + base), static_cast<size_t>(len))) {
                out.resize(base);
                return false;
            }
            if (masked && len > 0) {
                uint8_t* payload = reinterpret_cast<uint8_t*>(out.data() + base);
                for (size_t i = 0; i < static_cast<size_t>(len); ++i) payload[i] ^= mask[i & 3];
            }
            if (opcode == 0x8) {
                out.resize(base);
                closed = true;
                return false;
            }
            if (opcode == 0x9) {
                if (!send_frame(0xA, reinterpret_cast<const uint8_t*>(out.data() + base), static_cast<size_t>(len))) {
                    out.resize(base);
                    return false;
                }
                out.resize(base);
                continue;
            }
            if (opcode == 0xA) {
                out.resize(base);
                continue;
            }
            if (opcode != 0) first_op = opcode;
            if (fin) break;
        }
        if (first_op != 0x1) out.clear();
        return true;
    }
};

struct JobGuard {
    HANDLE h = nullptr;
    JobGuard() {
        h = CreateJobObjectW(nullptr, nullptr);
        if (h) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(h, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        }
    }
    ~JobGuard() {
        if (h) nt::NtClose(h);
    }
    JobGuard(const JobGuard&) = delete;
    JobGuard& operator=(const JobGuard&) = delete;
    void assign(HANDLE proc, bool verbose = false) const {
        if (h && proc) {
            if (!AssignProcessToJobObject(h, proc) && verbose) {
                std::cerr << "[cdp] WARNING: AssignProcessToJobObject failed (err="
                          << GetLastError() << "); process may outlive parent\n";
            }
        }
    }
};

[[nodiscard]] inline std::wstring quote_win_arg_w(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out;
    out.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            out.push_back(ch);
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

struct ProcThreadAttrGuard {
    std::vector<uint8_t> buf;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    bool initialized = false;

    bool init(DWORD count) {
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &attr_size);
        if (attr_size == 0) return false;
        buf.resize(attr_size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
        initialized = (InitializeProcThreadAttributeList(list, count, 0, &attr_size) != 0);
        return initialized;
    }
    ~ProcThreadAttrGuard() {
        if (initialized && list) {
            DeleteProcThreadAttributeList(list);
        }
    }
    ProcThreadAttrGuard() = default;
    ProcThreadAttrGuard(const ProcThreadAttrGuard&) = delete;
    ProcThreadAttrGuard& operator=(const ProcThreadAttrGuard&) = delete;
};
inline std::string format_hex_u32(DWORD v) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    for (int i = 0; i < 8; ++i) {
        buf[2 + i] = kHex[(v >> ((7 - i) * 4)) & 0xF];
    }
    return std::string(buf, 10);
}

inline void lockdown_after_browser_launch(bool verbose) {
    auto vlog = [&](std::string_view msg) {
        if (verbose) std::cerr << "[mitigation] " << msg << "\n";
    };

    PROCESS_MITIGATION_CHILD_PROCESS_POLICY cpp{};
    cpp.NoChildProcessCreation = 1;
    if (!SetProcessMitigationPolicy(ProcessChildProcessPolicy, &cpp, sizeof(cpp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            vlog("post-launch ProcessChildProcessPolicy failed (err=" + std::to_string(err) + ")");
        }
    }

    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY scdp{};
    scdp.DisallowWin32kSystemCalls = 1;
    if (!SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &scdp, sizeof(scdp))) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_SUPPORTED && err != ERROR_INVALID_PARAMETER) {
            vlog("post-launch ProcessSystemCallDisablePolicy failed (err=" + std::to_string(err) + ")");
        }
    }
}

inline void launch_cdp_browser(
    const std::string& start_url,
    const std::string& version,
    const std::string& config_key,
    const std::string& browser_key,
    const std::wstring& profile_dir,
    const std::string& route_pattern = "https://sarathi.parivahan.gov.in/*",
    bool verbose = false
) {
    auto vlog = [&](std::string_view msg) {
        if (verbose) std::cerr << "[cdp] " << msg << "\n";
    };

    WsaGuard wsa;
    JobGuard job;
    std::wstring chrome = find_chrome_exe();
    create_directories_w(profile_dir);

    std::wstring active_port_path = seb::join_path_w(profile_dir, L"DevToolsActivePort");
    DeleteFileW(active_port_path.c_str());

    std::string clean_ver;
    for (char c : version) {
        if ((c >= '0' && c <= '9') || c == '.') clean_ver.push_back(c);
    }
    if (clean_ver.empty()) clean_ver = "3.10.0";
    std::string seb_short = clean_ver;
    int dots = 0;
    for (size_t i = 0; i < clean_ver.size(); ++i) {
        if (clean_ver[i] == '.' && ++dots == 3) {
            seb_short = clean_ver.substr(0, i);
            break;
        }
    }
    std::string user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) SEB/" +
        seb_short + " (x64)";

    std::wstring cmdline = quote_win_arg_w(chrome);
    auto append_arg = [&](std::wstring_view arg) {
        cmdline.push_back(L' ');
        cmdline.append(quote_win_arg_w(arg));
    };
    append_arg(L"--user-data-dir=" + profile_dir);
    append_arg(L"--remote-debugging-port=0");
    append_arg(L"--user-agent=" + win::utf8_to_wide(user_agent));
    for (const wchar_t* flag : {
             L"--disable-blink-features=AutomationControlled",
             L"--disable-infobars",
             L"--disable-background-networking",
             L"--disable-client-side-phishing-detection",
             L"--disable-default-apps",
             L"--disable-hang-monitor",
             L"--disable-popup-blocking",
             L"--disable-prompt-on-repost",
             L"--disable-sync",
             L"--metrics-recording-only",
             L"--password-store=basic",
             L"--use-mock-keychain",
             L"--no-first-run",
             L"--no-default-browser-check",
             L"about:blank"}) {
        append_arg(flag);
    }

    // Apply safe child-process mitigations to Chrome/Edge via STARTUPINFOEXW
    // (heap termination, high-entropy ASLR, forced image relocation). Do not
    // enable ProhibitDynamicCode or MicrosoftSignedOnly on Chrome since V8
    // requires JIT and Chrome loads Google-signed binaries.
    STARTUPINFOEXW sie{};
    sie.StartupInfo.cb = sizeof(sie);
    DWORD64 child_mitigations =
        static_cast<DWORD64>(PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON) |
        static_cast<DWORD64>(PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON) |
        static_cast<DWORD64>(PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON) |
        static_cast<DWORD64>(PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON) |
        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
    ProcThreadAttrGuard attr_guard;
    const bool has_attrs = attr_guard.init(1) &&
        UpdateProcThreadAttribute(
            attr_guard.list, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
            &child_mitigations, sizeof(child_mitigations), nullptr, nullptr);
    if (has_attrs) sie.lpAttributeList = attr_guard.list;

PROCESS_INFORMATION pi{};
    DWORD create_flags = CREATE_SUSPENDED | (has_attrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL launched = CreateProcessW(
        chrome.c_str(), cmdline.data(), nullptr, nullptr, FALSE, create_flags,
        nullptr, nullptr, &sie.StartupInfo, &pi);
    if (!launched) {
        const DWORD err = GetLastError();
        throw std::runtime_error(
            "failed to launch browser (CreateProcessW err=" + std::to_string(err) +
            " / " + format_hex_u32(err) + ")");
    }
    nt::NtHandle proc_h(pi.hProcess);
    nt::NtHandle thread_h(pi.hThread);
    job.assign(proc_h.get(), verbose);
    if (ResumeThread(thread_h.get()) == static_cast<DWORD>(-1)) {
        const DWORD err = GetLastError();
        TerminateProcess(proc_h.get(), 1);
        throw std::runtime_error(
            "failed to resume browser main thread (ResumeThread err=" + std::to_string(err) + ")");
    }
    vlog("launched browser pid=" + std::to_string(pi.dwProcessId) + " (bound to KILL_ON_JOB_CLOSE job)");

    // Safe to lock down seb_keys.exe now: Chrome is already running and will not
    // inherit policies applied after CreateProcessW.
    lockdown_after_browser_launch(verbose);

    const ULONGLONG start_tick = GetTickCount64();
    uint16_t port = 0;
    std::string ws_url;
    while (GetTickCount64() - start_tick < 15000) {
        if (WaitForSingleObject(proc_h.get(), 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(proc_h.get(), &exit_code);
            std::string hint;
            if (exit_code == 0) {
                hint = " (another Chrome instance may already hold this --user-data-dir)";
            } else if (exit_code == 0xC0000022UL) {
                hint = " (STATUS_ACCESS_DENIED - check Win32k/mitigation inheritance)";
            } else if (exit_code == 0xC0000428UL) {
                hint = " (STATUS_INVALID_IMAGE_HASH - check signature policy)";
            }
            throw std::runtime_error(
                "Chrome exited before writing DevToolsActivePort (exit_code=" +
                std::to_string(exit_code) + " / " + format_hex_u32(exit_code) + ")" + hint);
        }

        try {
            if (seb::is_file_w(active_port_path.c_str())) {
                auto bytes = nt::nt_read_file_bytes_w(active_port_path.c_str(), 65536);
                std::string_view sv(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                size_t nl1 = sv.find('\n');
                if (nl1 != std::string_view::npos) {
                    std::string_view p_line = sv.substr(0, nl1);
                    if (!p_line.empty() && p_line.back() == '\r') p_line.remove_suffix(1);
                    uint32_t parsed_port = 0;
                    auto [pp, ec] = std::from_chars(p_line.data(), p_line.data() + p_line.size(), parsed_port);
                    if (ec == std::errc{} && pp == p_line.data() + p_line.size() &&
                        parsed_port > 0 && parsed_port <= 65535) {
                        port = static_cast<uint16_t>(parsed_port);
                        std::string_view rest = sv.substr(nl1 + 1);
                        size_t nl2 = rest.find_first_of("\r\n");
                        std::string_view ws_path = (nl2 == std::string_view::npos) ? rest : rest.substr(0, nl2);
                        if (!ws_path.empty() && ws_path.front() == '/') {
                            ws_url = "ws://127.0.0.1:" + std::to_string(port) + std::string(ws_path);
                            break;
                        }
                    }
                }
            }
        } catch (...) {}
        nt::nt_sleep_ms(20);
    }

    if (ws_url.empty()) {
        TerminateProcess(proc_h.get(), 1);
        throw std::runtime_error("timed out waiting for Chrome CDP DevToolsActivePort (browser still alive after 15s)");
    }

    vlog("chrome=" + win::wide_to_utf8(chrome) + " port=" + std::to_string(port));
    WsClient ws = WsClient::connect_ws(ws_url);
    vlog("connected ws=" + ws_url);
    std::string defuser_tag = build_defuser_tag();
    uint64_t cmd_id = 1;

    auto send_cdp = [&](const std::string& method, json::JsonVal params, const std::string& session_id = "") -> uint64_t {
        uint64_t id = cmd_id++;
        json::JsonObj obj;
        obj.emplace_back("id", json::JsonVal(id));
        obj.emplace_back("method", json::JsonVal(method));
        obj.emplace_back("params", std::move(params));
        if (!session_id.empty()) obj.emplace_back("sessionId", session_id);
        if (!ws.send_text(json::stringify(json::JsonVal(std::move(obj))))) {
            throw std::runtime_error("CDP send_text failed for " + method);
        }
        return id;
    };

    auto enable_page_hooks = [&](const std::string& session_id) {
        json::JsonArr patterns;
        {
            json::JsonObj p1;
            p1.emplace_back("urlPattern", route_pattern);
            p1.emplace_back("requestStage", "Request");
            patterns.emplace_back(std::move(p1));

            json::JsonObj p2;
            p2.emplace_back("urlPattern", route_pattern);
            p2.emplace_back("resourceType", "Document");
            p2.emplace_back("requestStage", "Response");
            patterns.emplace_back(std::move(p2));
        }
        json::JsonObj fetch_params;
        fetch_params.emplace_back("patterns", std::move(patterns));
        send_cdp("Fetch.enable", json::JsonVal(std::move(fetch_params)), session_id);
        send_cdp("Page.enable", json::JsonVal(json::JsonObj{}), session_id);
        {
            json::JsonObj sec_params;
            sec_params.emplace_back("ignore", true);
            send_cdp("Security.setIgnoreCertificateErrors", json::JsonVal(std::move(sec_params)), session_id);
        }
        {
            json::JsonObj sp;
            sp.emplace_back("source", std::string(STEALTH_JS));
            send_cdp("Page.addScriptToEvaluateOnNewDocument", json::JsonVal(std::move(sp)), session_id);
        }
    };

    {
        json::JsonObj sec_params;
        sec_params.emplace_back("ignore", true);
        send_cdp("Security.setIgnoreCertificateErrors", json::JsonVal(std::move(sec_params)));
    }
    bool navigated = false;
    {
        json::JsonObj aa;
        aa.emplace_back("autoAttach", true);
        aa.emplace_back("waitForDebuggerOnStart", false);
        aa.emplace_back("flatten", true);
        send_cdp("Target.setAutoAttach", json::JsonVal(std::move(aa)));
    }
    vlog("enabled browser-level Target.setAutoAttach + Security.setIgnoreCertificateErrors");

    std::unordered_map<std::string, std::pair<std::string, std::string>> header_cache;
    header_cache.reserve(512);
    struct PendingBody {
        std::string request_id;
        std::string session_id;
        int64_t status;
        std::vector<std::pair<std::string, std::string>> headers;
    };
    std::unordered_map<uint64_t, PendingBody> pending_bodies;

    std::string text;
    bool closed = false;
    while (ws.read_message(text, closed)) {
        if (text.empty()) continue;
        json::JsonVal msg;
        try { msg = json::parse(text); } catch (...) { continue; }

        if (const auto* id_val = msg.get("id")) {
            uint64_t id = 0;
            if (id_val->get_u64(id)) {
                auto it = pending_bodies.find(id);
                if (it != pending_bodies.end()) {
                    PendingBody pb = std::move(it->second);
                    pending_bodies.erase(it);

                    if (msg.get("error")) {
                        json::JsonObj cp;
                        cp.emplace_back("requestId", pb.request_id);
                        send_cdp("Fetch.continueResponse", json::JsonVal(std::move(cp)), pb.session_id);
                        continue;
                    }

                    const auto* res = msg.get("result");
                    std::string body_str;
                    bool b64 = false;
                    if (res) {
                        if (const auto* b = res->get("body"); b && b->as_str()) body_str = *b->as_str();
                        if (const auto* f = res->get("base64Encoded"); f && f->as_bool()) b64 = *f->as_bool();
                    }
                    std::string html;
                    std::string body_b64;
                    try {
                        if (b64) {
                            auto decoded = win::base64_decode(body_str);
                            html.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
                        } else {
                            html = std::move(body_str);
                        }
                        std::string modified = inject_defuser_html(html, defuser_tag);
                        if (verbose) {
                            vlog("injected DEFUSER_JS (" + std::to_string(html.size()) + " -> " +
                                 std::to_string(modified.size()) + " bytes)");
                        }
                        body_b64 = win::base64_encode(
                            reinterpret_cast<const uint8_t*>(modified.data()), modified.size());
                    } catch (...) {
                        json::JsonObj cp;
                        cp.emplace_back("requestId", pb.request_id);
                        send_cdp("Fetch.continueResponse", json::JsonVal(std::move(cp)), pb.session_id);
                        continue;
                    }

                    json::JsonArr resp_headers;
                    for (const auto& [k, v] : pb.headers) {
                        std::string kl = to_lower_ascii(k);
                        if (kl == "content-encoding" || kl == "content-length" ||
                            kl == "transfer-encoding" ||
                            kl == "content-security-policy" || kl == "content-security-policy-report-only") {
                            continue;
                        }
                        json::JsonObj h;
                        h.emplace_back("name", k);
                        h.emplace_back("value", v);
                        resp_headers.emplace_back(std::move(h));
                    }

                    json::JsonObj fp;
                    fp.emplace_back("requestId", pb.request_id);
                    fp.emplace_back("responseCode", pb.status);
                    fp.emplace_back("responseHeaders", std::move(resp_headers));
                    fp.emplace_back("body", std::move(body_b64));
                    send_cdp("Fetch.fulfillRequest", json::JsonVal(std::move(fp)), pb.session_id);
                }
            }
            continue;
        }

        std::string session_id;
        if (const auto* sid_v = msg.get("sessionId"); sid_v && sid_v->as_str()) {
            session_id = *sid_v->as_str();
        }

        const auto* method = msg.get("method");
        if (!method || !method->as_str()) continue;
        if (*method->as_str() == "Target.attachedToTarget") {
            const auto* params = msg.get("params");
            if (params) {
                const auto* child_sid = params->get("sessionId");
                const auto* ti = params->get("targetInfo");
                const auto* typ = ti ? ti->get("type") : nullptr;
                if (child_sid && child_sid->as_str() && typ && typ->as_str() && *typ->as_str() == "page") {
                    if (verbose) vlog("attached popup/tab sessionId=" + *child_sid->as_str());
                    enable_page_hooks(*child_sid->as_str());
                    if (!navigated) {
                        navigated = true;
                        json::JsonObj nav_params;
                        nav_params.emplace_back("url", start_url);
                        send_cdp("Page.navigate", json::JsonVal(std::move(nav_params)), *child_sid->as_str());
                        vlog("navigating to " + start_url);
                    }
                }
            }
            continue;
        }
        if (*method->as_str() != "Fetch.requestPaused") continue;
        const auto* params = msg.get("params");
        if (!params) continue;
        const auto* req_id_v = params->get("requestId");
        if (!req_id_v || !req_id_v->as_str()) continue;
        std::string request_id = *req_id_v->as_str();

        if (const auto* status_v = params->get("responseStatusCode")) {
            int64_t status = 200;
            status_v->get_i64(status);
            if (status >= 300 && status < 400) {
                json::JsonObj cp;
                cp.emplace_back("requestId", request_id);
                send_cdp("Fetch.continueResponse", json::JsonVal(std::move(cp)), session_id);
                continue;
            }
            std::vector<std::pair<std::string, std::string>> headers;
            bool is_html = false;
            if (const auto* arr = params->get("responseHeaders"); arr && arr->as_arr()) {
                for (const auto& h : *arr->as_arr()) {
                    std::string name = (h.get("name") && h.get("name")->as_str()) ? *h.get("name")->as_str() : "";
                    std::string val = (h.get("value") && h.get("value")->as_str()) ? *h.get("value")->as_str() : "";
                    if (plist::iequals(name, "content-type") && to_lower_ascii(val).find("text/html") != std::string::npos) {
                        is_html = true;
                    }
                    headers.emplace_back(std::move(name), std::move(val));
                }
            }
            if (is_html && pending_bodies.size() < 256) {
                json::JsonObj gp;
                gp.emplace_back("requestId", request_id);
                uint64_t bid = send_cdp("Fetch.getResponseBody", json::JsonVal(std::move(gp)), session_id);
                pending_bodies.emplace(bid, PendingBody{request_id, session_id, status, std::move(headers)});
            } else {
                json::JsonObj cp;
                cp.emplace_back("requestId", request_id);
                send_cdp("Fetch.continueResponse", json::JsonVal(std::move(cp)), session_id);
            }
            continue;
        }

        const auto* req = params->get("request");
        if (!req) continue;
        std::string req_method = (req->get("method") && req->get("method")->as_str()) ? *req->get("method")->as_str() : "GET";
        std::string req_url = (req->get("url") && req->get("url")->as_str()) ? *req->get("url")->as_str() : "";

        if (plist::iequals(req_method, "OPTIONS")) {
            if (verbose) vlog("OPTIONS 204 " + req_url);
            std::string origin = "https://sarathi.parivahan.gov.in";
            if (const auto* hdrs = req->get("headers"); hdrs && hdrs->as_obj()) {
                for (const auto& [k, v] : *hdrs->as_obj()) {
                    if (plist::iequals(k, "origin") && v.as_str()) {
                        std::string clean_orig;
                        clean_orig.reserve(v.as_str()->size());
                        for (char ch : *v.as_str()) {
                            if (ch != '\r' && ch != '\n') clean_orig.push_back(ch);
                        }
                        if (!clean_orig.empty()) origin = std::move(clean_orig);
                    }
                }
            }
            json::JsonArr rh;
            auto add_h = [&](const char* n, const std::string& val) {
                json::JsonObj o;
                o.emplace_back("name", n);
                o.emplace_back("value", val);
                rh.emplace_back(std::move(o));
            };
            add_h("Access-Control-Allow-Origin", origin);
            add_h("Access-Control-Allow-Credentials", "true");
            add_h("Access-Control-Allow-Headers", "Content-Type, Authorization, X-SafeExamBrowser-ConfigKeyHash, X-SafeExamBrowser-RequestHash");
            add_h("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");

            json::JsonObj fp;
            fp.emplace_back("requestId", request_id);
            fp.emplace_back("responseCode", int64_t(204));
            fp.emplace_back("responseHeaders", std::move(rh));
            send_cdp("Fetch.fulfillRequest", json::JsonVal(std::move(fp)), session_id);
            continue;
        }

        size_t hash_pos = req_url.find('#');
        std::string clean_url = (hash_pos == std::string::npos) ? req_url : req_url.substr(0, hash_pos);
        std::pair<std::string, std::string> pair;
        auto it = header_cache.find(clean_url);
        if (it != header_cache.end()) {
            pair = it->second;
        } else {
            pair = {request_hash(clean_url, config_key), request_hash(clean_url, browser_key)};
            if (header_cache.size() < 512) header_cache.emplace(clean_url, pair);
        }

        json::JsonArr new_headers;
        if (const auto* hdrs = req->get("headers"); hdrs && hdrs->as_obj()) {
            for (const auto& [k, v] : *hdrs->as_obj()) {
                if (plist::iequals(k, "X-SafeExamBrowser-ConfigKeyHash") ||
                    plist::iequals(k, "X-SafeExamBrowser-RequestHash")) {
                    continue;
                }
                if (v.as_str()) {
                    json::JsonObj o;
                    o.emplace_back("name", k);
                    o.emplace_back("value", *v.as_str());
                    new_headers.emplace_back(std::move(o));
                }
            }
        }
        {
            json::JsonObj o1, o2;
            o1.emplace_back("name", "X-SafeExamBrowser-ConfigKeyHash");
            o1.emplace_back("value", pair.first);
            o2.emplace_back("name", "X-SafeExamBrowser-RequestHash");
            o2.emplace_back("value", pair.second);
            new_headers.emplace_back(std::move(o1));
            new_headers.emplace_back(std::move(o2));
        }

        json::JsonObj cp;
        cp.emplace_back("requestId", request_id);
        cp.emplace_back("headers", std::move(new_headers));
        if (verbose) vlog(req_method + " " + clean_url);
        send_cdp("Fetch.continueRequest", json::JsonVal(std::move(cp)), session_id);
    }

    WaitForSingleObject(proc_h.get(), INFINITE);
}

} // namespace seb::cdp
