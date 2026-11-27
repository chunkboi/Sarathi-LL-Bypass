#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "json.hpp"
#include "plist.hpp"
#include "seb.hpp"
#include "win_crypto.hpp"

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
 
   try { if (navigator.webdriver) Object.defineProperty(Navigator.prototype, 'webdriver', { get: () => false, configurable: true }); } catch {}


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

inline std::string inject_defuser_html(const std::string& html, const std::string& defuser_tag) {
    size_t pos = 0;
    while ((pos = find_ci_ascii(html, "<head", pos)) != std::string::npos) {
        char next = (pos + 5 < html.size()) ? html[pos + 5] : '>';
        if (next == '>' || std::isspace(static_cast<unsigned char>(next))) {
            size_t gt = html.find('>', pos);
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

inline std::wstring find_chrome_exe() {
    const wchar_t* fixed_candidates[] = {
        LR"(C:\Program Files\Google\Chrome\Application\chrome.exe)",
        LR"(C:\Program Files (x86)\Google\Chrome\Application\chrome.exe)",
    };
    for (const wchar_t* c : fixed_candidates) {
        if (seb::is_file_w(c)) return c;
    }
    wchar_t local[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LocalAppData", local, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring p1 = seb::join_path_w(local, LR"(Google\Chrome\Application\chrome.exe)");
        if (seb::is_file_w(p1.c_str())) return p1;
        std::wstring p2 = seb::join_path_w(local, LR"(Microsoft\Edge\Application\msedge.exe)");
        if (seb::is_file_w(p2.c_str())) return p2;
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
    CreateDirectoryW(dir.c_str(), nullptr);
}

struct WsaGuard {
    WsaGuard() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WsaGuard() { WSACleanup(); }
};

inline uint16_t free_local_port() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        throw std::runtime_error("bind failed");
    }
    int len = sizeof(addr);
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    uint16_t port = ntohs(addr.sin_port);
    closesocket(s);
    return port;
}

inline SOCKET connect_local(uint16_t port, DWORD timeout_ms = 0) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (timeout_ms > 0) {
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        int rc = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc != 0) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                closesocket(s);
                throw std::runtime_error("connect failed");
            }
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(s, &wfds);
            FD_SET(s, &efds);
            timeval tv{};
            tv.tv_sec = static_cast<long>(timeout_ms / 1000);
            tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
            int sel = select(0, nullptr, &wfds, &efds, &tv);
            if (sel <= 0 || FD_ISSET(s, &efds) || !FD_ISSET(s, &wfds)) {
                closesocket(s);
                throw std::runtime_error("connect timeout/refused");
            }
            // select() write-ready doesn't guarantee success on Windows —
            // a RST can briefly appear writable. Verify via SO_ERROR.
            int sock_err = 0;
            int soe_len = sizeof(sock_err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sock_err), &soe_len) != 0 || sock_err != 0) {
                closesocket(s);
                throw std::runtime_error("connect failed (SO_ERROR)");
            }
        }
        u_long blk = 0;
        ioctlsocket(s, FIONBIO, &blk);
        return s;
    }
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        throw std::runtime_error("connect failed");
    }
    return s;
}

inline bool send_all(SOCKET s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int chunk = static_cast<int>((std::min<size_t>)(len - sent, 0x40000000ULL));
        int n = send(s, reinterpret_cast<const char*>(data + sent), chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

inline bool recv_exact(SOCKET s, uint8_t* dst, size_t len) {
    size_t got = 0;
    while (got < len) {
        int chunk = static_cast<int>((std::min<size_t>)(len - got, 0x40000000ULL));
        int n = recv(s, reinterpret_cast<char*>(dst + got), chunk, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

inline std::string http_get_local(uint16_t port, const std::string& path) {
    SOCKET s = connect_local(port, 50);
    DWORD tv = 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nConnection: close\r\n\r\n";
    if (!send_all(s, reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
        closesocket(s);
        throw std::runtime_error("HTTP send failed");
    }
    std::string raw;
    char buf[4096];
    size_t sep = std::string::npos;
    size_t content_len = std::string::npos;
    while (true) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > 64ULL * 1024 * 1024) {
            closesocket(s);
            throw std::runtime_error("HTTP response too large");
        }
        if (sep == std::string::npos) {
            sep = raw.find("\r\n\r\n");
            if (sep != std::string::npos) {
                std::string hdr_lower = to_lower_ascii(raw.substr(0, sep));
                size_t cl_pos = hdr_lower.find("content-length:");
                if (cl_pos != std::string::npos) {
                    cl_pos += 15;
                    while (cl_pos < hdr_lower.size() && (hdr_lower[cl_pos] == ' ' || hdr_lower[cl_pos] == '\t')) cl_pos++;
                    uint64_t parsed_cl = std::strtoull(hdr_lower.c_str() + cl_pos, nullptr, 10);
                    if (parsed_cl <= 64ULL * 1024 * 1024) {
                        content_len = static_cast<size_t>(parsed_cl);
                    }
                }
            }
        }
        if (sep != std::string::npos && content_len != std::string::npos &&
            raw.size() - (sep + 4) >= content_len) {
            break;
        }
    }
    closesocket(s);
    if (sep == std::string::npos) throw std::runtime_error("invalid HTTP response");
    if (content_len != std::string::npos && raw.size() - (sep + 4) >= content_len) {
        return raw.substr(sep + 4, content_len);
    }
    return raw.substr(sep + 4);
}

struct WsClient {
    SOCKET s = INVALID_SOCKET;

    ~WsClient() {
        if (s != INVALID_SOCKET) closesocket(s);
    }

    static WsClient connect_ws(const std::string& ws_url) {
        const std::string prefix = "ws://";
        if (ws_url.compare(0, prefix.size(), prefix) != 0) {
            throw std::runtime_error("only ws:// supported for local CDP");
        }
        std::string rest = ws_url.substr(prefix.size());
        size_t slash = rest.find('/');
        std::string host_port = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);
        size_t colon = host_port.find(':');
        if (colon == std::string::npos) throw std::runtime_error("missing port in ws url");
        std::string host = host_port.substr(0, colon);
        if (host != "127.0.0.1" && host != "localhost") {
            throw std::runtime_error("refusing non-loopback ws host");
        }
        int port_i = std::stoi(host_port.substr(colon + 1));
        if (port_i <= 0 || port_i > 65535) throw std::runtime_error("invalid port in ws url");
        uint16_t port = static_cast<uint16_t>(port_i);

        SOCKET s = connect_local(port);
        BOOL nodelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

        uint8_t key_raw[16]{};
        BCryptGenRandom(nullptr, key_raw, sizeof(key_raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        std::string key = win::base64_encode(key_raw, sizeof(key_raw));
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host_port +
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                          "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!send_all(s, reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
            closesocket(s);
            throw std::runtime_error("ws handshake send failed");
        }

        std::string hdr;
        char c = 0;
        while (hdr.size() < 4 || hdr.compare(hdr.size() - 4, 4, "\r\n\r\n") != 0) {
            if (recv(s, &c, 1, 0) <= 0) {
                closesocket(s);
                throw std::runtime_error("ws handshake EOF");
            }
            hdr.push_back(c);
            if (hdr.size() > 16384) {
                closesocket(s);
                throw std::runtime_error("ws handshake header too large");
            }
        }
        // Validate the HTTP status line specifically — a naive find("101") would
        // match in a header value or response body. The status line is always
        // the first CRLF-terminated token of the upgrade response.
        {
            size_t crlf0 = hdr.find("\r\n");
            std::string_view status_line = (crlf0 != std::string::npos)
                ? std::string_view(hdr).substr(0, crlf0)
                : std::string_view(hdr);
            if (status_line.find("101") == std::string_view::npos) {
                closesocket(s);
                std::string snippet(status_line.substr(0, std::min(status_line.size(), size_t(64))));
                throw std::runtime_error("ws upgrade failed: " + snippet);
            }
        }

        WsClient client;
        client.s = s;
        return client;
    }

    bool send_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
        // RFC 6455 §5.3: masking key MUST be chosen unpredictably per frame.
        uint8_t mask[4]{};
        BCryptGenRandom(nullptr, mask, sizeof(mask), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
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

    bool send_text(const std::string& text) {
        return send_frame(0x1, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    bool read_message(std::string& out, bool& closed) {
        out.clear();
        closed = false;
        uint8_t first_op = 0;
        while (true) {
            uint8_t hdr[2];
            if (!recv_exact(s, hdr, 2)) return false;
            bool fin = (hdr[0] & 0x80) != 0;
            uint8_t opcode = hdr[0] & 0x0F;
            bool masked = (hdr[1] & 0x80) != 0;
            uint64_t len = hdr[1] & 0x7F;
            if (len == 126) {
                uint8_t b[2];
                if (!recv_exact(s, b, 2)) return false;
                len = (static_cast<uint64_t>(b[0]) << 8) | b[1];
            } else if (len == 127) {
                uint8_t b[8];
                if (!recv_exact(s, b, 8)) return false;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[i];
            }
            // Guard against absurdly large frame lengths advertised by the
            // server (e.g. a buggy or malicious CDP endpoint). Chrome CDP
            // messages are always well under 256 MiB in practice.
            constexpr uint64_t MAX_FRAME = 256u * 1024 * 1024; // 256 MiB
            if (len > MAX_FRAME) return false;
            uint8_t mask[4]{};
            if (masked && !recv_exact(s, mask, 4)) return false;
            // Receive directly into the output buffer instead of allocating a
            // temporary vector for every frame.
            const size_t base = out.size();
            out.resize(base + static_cast<size_t>(len));
            if (len > 0 &&
                !recv_exact(s, reinterpret_cast<uint8_t*>(out.data() + base), static_cast<size_t>(len))) {
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
                send_frame(0xA, reinterpret_cast<const uint8_t*>(out.data() + base), static_cast<size_t>(len));
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
    uint16_t port = free_local_port();
    create_directories_w(profile_dir);

    vlog("chrome=" + win::wide_to_utf8(chrome) + " port=" + std::to_string(port));

    std::string seb_short = version;
    int dots = 0;
    for (size_t i = 0; i < version.size(); ++i) {
        if (version[i] == '.' && ++dots == 3) {
            seb_short = version.substr(0, i);
            break;
        }
    }
    std::string user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) SEB/" +
        seb_short + " (x64)";

    std::wstring cmdline = L"\"" + chrome + L"\"" +
                           L" --user-data-dir=\"" + profile_dir + L"\"" +
                           L" --remote-debugging-port=" + std::to_wstring(port) +
                           L" --user-agent=\"" + win::utf8_to_wide(user_agent) + L"\"" +
                           L" --disable-blink-features=AutomationControlled" +
                           L" --disable-infobars" +
                           L" --disable-background-networking" +
                           L" --disable-client-side-phishing-detection" +
                           L" --disable-default-apps" +
                           L" --disable-hang-monitor" +
                           L" --disable-popup-blocking" +
                           L" --disable-prompt-on-repost" +
                           L" --disable-sync" +
                           L" --metrics-recording-only" +
                           L" --password-store=basic" +
                           L" --use-mock-keychain" +
                           L" --no-first-run --no-default-browser-check about:blank";

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
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<uint8_t> attr_buf(attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    bool has_attrs = attr_size > 0 &&
        InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size) &&
        UpdateProcThreadAttribute(
            attr_list, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
            &child_mitigations, sizeof(child_mitigations), nullptr, nullptr);
    if (has_attrs) sie.lpAttributeList = attr_list;

    PROCESS_INFORMATION pi{};
    DWORD create_flags = CREATE_SUSPENDED | (has_attrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL launched = CreateProcessW(
        nullptr, cmdline.data(), nullptr, nullptr, FALSE, create_flags,
        nullptr, nullptr, &sie.StartupInfo, &pi);
    if (has_attrs) DeleteProcThreadAttributeList(attr_list);
    if (!launched) {
        throw std::runtime_error("failed to launch browser");
    }
    nt::NtHandle proc_h(pi.hProcess);
    nt::NtHandle thread_h(pi.hThread);
    job.assign(proc_h.get(), verbose);
    ResumeThread(thread_h.get());
    vlog("launched browser pid=" + std::to_string(pi.dwProcessId) + " (bound to KILL_ON_JOB_CLOSE job)");

    DWORD start_tick = GetTickCount();
    std::string ws_url;
    while (GetTickCount() - start_tick < 15000) {
        try {
            std::string body = http_get_local(port, "/json/list");
            auto val = json::parse(body);
            if (const auto* arr = val.as_arr()) {
                for (const auto& t : *arr) {
                    const auto* typ = t.get("type");
                    const auto* ws = t.get("webSocketDebuggerUrl");
                    if (typ && typ->as_str() && *typ->as_str() == "page" && ws && ws->as_str()) {
                        ws_url = *ws->as_str();
                        break;
                    }
                }
            }
            if (!ws_url.empty()) break;
        } catch (...) {}
        nt::nt_sleep_ms(20);
    }

    if (ws_url.empty()) {
        TerminateProcess(proc_h.get(), 1);
        throw std::runtime_error("timed out waiting for Chrome CDP");
    }

    WsClient ws = WsClient::connect_ws(ws_url);
    vlog("connected ws=" + ws_url);
    std::string defuser_tag = build_defuser_tag();
    uint64_t cmd_id = 1;

    auto send_cdp = [&](const std::string& method, json::JsonVal params, const std::string& session_id = "") {
        uint64_t id = cmd_id++;
        json::JsonObj obj;
        obj.emplace_back("id", json::JsonVal(id));
        obj.emplace_back("method", json::JsonVal(method));
        obj.emplace_back("params", std::move(params));
        if (!session_id.empty()) obj.emplace_back("sessionId", session_id);
        ws.send_text(json::stringify(json::JsonVal(std::move(obj))));
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

    enable_page_hooks("");
    vlog("enabled Fetch (" + route_pattern + "), Security.setIgnoreCertificateErrors, STEALTH_JS");
    {
        json::JsonObj aa;
        aa.emplace_back("autoAttach", true);
        aa.emplace_back("waitForDebuggerOnStart", false);
        aa.emplace_back("flatten", true);
        send_cdp("Target.setAutoAttach", json::JsonVal(std::move(aa)));
    }
    {
        json::JsonObj nav_params;
        nav_params.emplace_back("url", start_url);
        send_cdp("Page.navigate", json::JsonVal(std::move(nav_params)));
    }
    vlog("navigating to " + start_url);

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

                    json::JsonArr resp_headers;
                    for (const auto& [k, v] : pb.headers) {
                        std::string kl = to_lower_ascii(k);
                        if (kl == "content-encoding" || kl == "content-length" ||
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
                    fp.emplace_back("body", win::base64_encode(reinterpret_cast<const uint8_t*>(modified.data()), modified.size()));
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
            if (is_html) {
                json::JsonObj gp;
                gp.emplace_back("requestId", request_id);
                uint64_t bid = send_cdp("Fetch.getResponseBody", json::JsonVal(std::move(gp)), session_id);
                // Cap the map to prevent unbounded growth if CDP ids are
                // issued faster than responses arrive (e.g. rapid navigation).
                if (pending_bodies.size() < 256) {
                    pending_bodies.emplace(bid, PendingBody{request_id, session_id, status, std::move(headers)});
                } else {
                    // Map full — just continue the response unmodified.
                    json::JsonObj cp2;
                    cp2.emplace_back("requestId", request_id);
                    send_cdp("Fetch.continueResponse", json::JsonVal(std::move(cp2)), session_id);
                }
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
