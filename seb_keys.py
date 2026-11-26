#!/usr/bin/env python3
r"""Standalone one-shot .seb -> CK, BEK, ConfigKeyHash, RequestHash.
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import datetime as dt
import gzip
import hashlib
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from functools import cmp_to_key
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit
from xml.etree.ElementTree import ParseError

DEFAULT_CACHE = Path(__file__).resolve().parent / ".seb_tree_cache.json"
DEFAULT_PROFILE_DIR = Path(__file__).resolve().parent / ".seb_chrome_profile"
URL = "https://sarathi.parivahan.gov.in/onlinelltestbnd/exam/sebCheck"

# ==============================================================================
# CDP INJECTED JAVASCRIPT (Stealth Defusers + Fake SEB + __cmp Capturer)
# ==============================================================================
DEFUSER_JS = r"""
(() => {
  'use strict';

  const stealth = (fn, name) => {
    const nativeStr = `function ${name}() { [native code] }`;
    Object.defineProperty(fn, 'toString', { value: () => nativeStr, configurable: true, writable: true });
    return fn;
  };

  // 1. FAKE SEB ENVIRONMENT
  window.SafeExamBrowser = {
    version: "3.10.0",
    security: { isScreenCaptureAllowed: () => false, isVirtualMachine: () => false },
    browser: { isRunning: () => true }
  };

  // 2. EXPOSE ANGULAR COMPONENT (__cmp)
  (function captureComponent() {
    const check = (thisArg) => {
      if (window.__cmp || !thisArg || typeof thisArg !== 'object') return;
      if (thisArg.forceLogout || thisArg.onFinalSubmit || thisArg.pollFrame || thisArg.proctorService || thisArg.checkRealInternet) {
        window.__cmp = thisArg;
        console.log('%c[defuser] 🎯 __cmp CAPTURED!', 'color:#0f0;font-weight:bold');
      }
    };
    const origCall = Function.prototype.call;
    Function.prototype.call = stealth(function (thisArg, ...args) {
      check(thisArg);
      return Reflect.apply(origCall, this, [thisArg, ...args]);
    }, 'call');
    const origApply = Function.prototype.apply;
    Function.prototype.apply = stealth(function (thisArg, args) {
      check(thisArg);
      return Reflect.apply(origApply, this, [thisArg, args]);
    }, 'apply');
  })();

  // 3. PATCH ONNX & CAMERA
  (function patchProctoringServices() {
    const interval = setInterval(() => {
      if (window.__cmp && window.__cmp.proctorService && window.__cmp.anti) {
        const proctorProto = Object.getPrototypeOf(window.__cmp.proctorService);
        if (!proctorProto.runProctorSpoofCheck.__patched) {
          proctorProto.runProctorSpoofCheck = stealth(async function () {
            return { finalDecision: true, bufferReady: true, spoofDetected: false, probReal: 1.0 };
          }, 'runProctorSpoofCheck');
          proctorProto.runProctorSpoofCheck.__patched = true;
          console.log('%c[defuser] 🧠 ONNX Proctor bypassed.', 'color:#0f0');
        }

        const antiProto = Object.getPrototypeOf(window.__cmp.anti);
        if (!antiProto.runCameraSecurityCheck.__patched) {
          antiProto.runCameraSecurityCheck = stealth(async function () {
            return { status: "ok", hasVirtualCamera: false, hasMultipleCameras: false, suspiciousDevices: [], replayLikely: false, browserTampered: false };
          }, 'runCameraSecurityCheck');
          antiProto.runCameraSecurityCheck.__patched = true;
          console.log('%c[defuser] 📷 Camera Security bypassed.', 'color:#0f0');
        }
      }
    }, 500);
  })();

  // 4. CRYPTO INTERCEPTOR
  (function interceptCrypto() {
    if (!window.crypto || !window.crypto.subtle) return;
    try {
      const origImportKey = crypto.subtle.importKey.bind(crypto.subtle);
      crypto.subtle.importKey = stealth(async function (format, keyData, algorithm, extractable, keyUsages) {
        try {
          if (format === 'raw' && algorithm && algorithm.name === 'PBKDF2') {
            const passphrase = new TextDecoder().decode(keyData);
            if (!window.__SEB_PASSPHRASE) {
              window.__SEB_PASSPHRASE = passphrase;
              console.log('%c[defuser] 🔑 CAPTURED PASSPHRASE:', 'color:#ff0;font-weight:bold;font-size:14px', passphrase);
            }
          }
        } catch (e) {}
        return origImportKey(format, keyData, algorithm, extractable, keyUsages);
      }, 'importKey');

      const origDeriveKey = crypto.subtle.deriveKey.bind(crypto.subtle);
      crypto.subtle.deriveKey = stealth(async function (algorithm, baseKey, derivedKeyType, extractable, keyUsages) {
        try {
          if (algorithm && algorithm.name === 'PBKDF2' && algorithm.salt) {
            const salt = new TextDecoder().decode(algorithm.salt);
            if (!window.__SEB_SALT) {
              window.__SEB_SALT = salt;
              console.log('%c[defuser] 🧂 CAPTURED SALT:', 'color:#ff0;font-weight:bold;font-size:14px', salt);
            }
          }
        } catch (e) {}
        return origDeriveKey(algorithm, baseKey, derivedKeyType, extractable, keyUsages);
      }, 'deriveKey');

      const origEncrypt = crypto.subtle.encrypt.bind(crypto.subtle);
      crypto.subtle.encrypt = stealth(async function (algorithm, key, data) {
        try {
          const text = new TextDecoder().decode(data);
          if (text.includes('{')) console.log('%c[defuser] 🔒 ENCRYPTING PAYLOAD:', 'color:#f80', JSON.parse(text));
        } catch (e) {}
        return origEncrypt(algorithm, key, data);
      }, 'encrypt');

      const origDecrypt = crypto.subtle.decrypt.bind(crypto.subtle);
      crypto.subtle.decrypt = stealth(async function (algorithm, key, data) {
        const result = await origDecrypt(algorithm, key, data);
        try {
          const text = new TextDecoder().decode(result);
          if (text.includes('{')) console.log('%c[defuser] 🔓 DECRYPTED PAYLOAD:', 'color:#0f0', JSON.parse(text));
        } catch (e) {}
        return result;
      }, 'decrypt');
    } catch (e) {}
  })();

  // 5. DEFUSE TIMERS & CONTEXT MENU
  (function neutralizeTraps() {
    const hasDebugger = (src) => /\bdebugger\b/.test(src);
    function isAntiDebugFn(fn) {
      if (typeof fn !== 'function') return false;
      let src;
      try { src = Function.prototype.toString.call(fn); } catch { return false; }
      return hasDebugger(src);
    }
    function wrapTimer(orig, name) {
      return stealth(function (fn, ...rest) {
        if (isAntiDebugFn(fn)) return 0;
        return orig.call(this, fn, ...rest);
      }, name);
    }
    window.setInterval = wrapTimer(window.setInterval, 'setInterval');
    window.setTimeout  = wrapTimer(window.setTimeout,  'setTimeout');
    window.requestAnimationFrame = wrapTimer(window.requestAnimationFrame, 'requestAnimationFrame');
    window.addEventListener('contextmenu', (e) => e.stopImmediatePropagation(), { capture: true });
    
    const origDP = Object.defineProperty;
    Object.defineProperty = stealth(function (obj, key, desc) {
      if (key === 'id' && desc && typeof desc.get === 'function' && obj instanceof Image) return obj;
      return origDP.call(this, obj, key, desc);
    }, 'defineProperty');
  })();

  // 6. DEFUSE KEYS & HISTORY & STORAGE
  window.addEventListener('keydown', (e) => {
    if ((e.altKey && e.key === 'ArrowLeft') || (e.key === 'Backspace' && !['INPUT', 'TEXTAREA'].includes(e.target?.tagName))) {
      e.stopImmediatePropagation();
    }
  }, { capture: true });

  const DUMMY_UID = /^s\d+_\d+_[a-z0-9]+$/i;
  const origPush = History.prototype.pushState;
  History.prototype.pushState = stealth(function (state, title, url) {
    if (state && typeof state === 'object' && DUMMY_UID.test(state.uid || '')) return;
    return origPush.call(this, state, title, url);
  }, 'pushState');

  const origAssign = Location.prototype.assign;
  Location.prototype.assign = stealth(function (u) {
    if (/SebSetupInstructions|\/logout/i.test(String(u))) return;
    return origAssign.call(this, u);
  }, 'assign');

  const origGet = Storage.prototype.getItem;
  Storage.prototype.getItem = stealth(function (k) {
    if (k === 'seb_error_reason') return null;
    return origGet.call(this, k);
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

  try { Object.defineProperty(navigator, 'webdriver', { get: () => false, configurable: true }); } catch {}

  console.log('[defuser] All stealth bypasses active. Waiting for __cmp...');
})();
"""
# ==============================================================================

# ---------------------------------------------------------------------------
# SEB Tree Hashing & Key Extraction Logic
# ---------------------------------------------------------------------------

def _root_from_exe_path(exe_path: Path) -> Path | None:
    if not exe_path.is_file():
        return None
    if exe_path.parent.name.lower() == "application":
        return exe_path.parent.parent
    return exe_path.parent

def detect_seb_root() -> Path:
    if os.name == "nt":
        import winreg
        for subkey in (r"seb\shell\open\command", r"sebs\shell\open\command", r"SafeExamBrowser\shell\open\command"):
            try:
                with winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, subkey) as key:
                    cmd = winreg.QueryValue(key, "")
                parts = shlex.split(cmd, posix=False)
                if parts:
                    candidate = _root_from_exe_path(Path(parts[0].strip('"')))
                    if candidate and candidate.is_dir():
                        return candidate
            except OSError:
                pass

        uninstall_roots = [
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"),
            (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
        ]
        for hive, path in uninstall_roots:
            try:
                with winreg.OpenKey(hive, path) as parent:
                    count = winreg.QueryInfoKey(parent)[0]
                    for i in range(count):
                        name = winreg.EnumKey(parent, i)
                        with winreg.OpenKey(parent, name) as sub:
                            try:
                                display = str(winreg.QueryValueEx(sub, "DisplayName")[0])
                            except OSError:
                                continue
                            if "safe exam browser" not in display.lower():
                                continue
                            for val_name in ("InstallLocation", "DisplayIcon"):
                                try:
                                    raw = str(winreg.QueryValueEx(sub, val_name)[0]).split(",")[0].strip('" ')
                                    p = Path(raw)
                                    root = _root_from_exe_path(p) if p.suffix.lower() == ".exe" else p
                                    if root and root.is_dir():
                                        return root
                                except OSError:
                                    pass
            except OSError:
                pass

    for env_var in ("ProgramFiles", "ProgramFiles(x86)", "LocalAppData"):
        base = os.environ.get(env_var)
        if base:
            candidate = Path(base) / "SafeExamBrowser"
            if candidate.is_dir():
                return candidate

    raise FileNotFoundError("Could not locate SEB installation; pass --root explicitly.")

def _find_seb_exe(root: Path) -> Path:
    direct = root / "Application" / "SafeExamBrowser.exe"
    if direct.is_file(): return direct
    for match in root.rglob("SafeExamBrowser.exe"):
        if match.is_file(): return match
    raise FileNotFoundError(f"SafeExamBrowser.exe not found under {root}")

def _find_seb_dll(root: Path) -> Path:
    direct = root / "Application" / "seb_x64.dll"
    if direct.is_file(): return direct
    for match in root.rglob("seb_x64.dll"):
        if match.is_file(): return match
    raise FileNotFoundError(f"seb_x64.dll not found under {root}")

def extract_salt_const(root: Path) -> str:
    import pefile  # type: ignore
    dll_path = _find_seb_dll(root)
    pe = pefile.PE(str(dll_path), fast_load=True)
    try:
        text_sec = next((s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text"), None)
        if text_sec is None: raise ValueError(f".text section not found in {dll_path}")
        text_rva = text_sec.VirtualAddress
        text_bytes = text_sec.get_data()

        sig = re.compile(rb"\x66\x0f\x6f\x05(.{4})\x45\x33\xc0\x66\x0f\x6f\x0d(.{4})", re.DOTALL)
        for m in sig.finditer(text_bytes):
            fn_off = m.start()
            window = text_bytes[fn_off:fn_off + 0x90]
            movdqas = []
            for mm in re.finditer(rb"\x66\x0f\x6f([\x05\x0d\x15\x1d])(.{4})", window, re.DOTALL):
                disp = struct.unpack_from("<i", mm.group(2), 0)[0]
                target_rva = text_rva + fn_off + mm.end() + disp
                movdqas.append(pe.get_data(target_rva, 16))
            dwords = [
                struct.unpack_from("<I", window, dm.start() + 3)[0].to_bytes(4, "little")
                for dm in re.finditer(rb"\xc7\x45.(....)", window, re.DOTALL)
            ]
            if len(movdqas) >= 4 and len(dwords) >= 4:
                buf_a = movdqas[0] + movdqas[1] + dwords[0] + dwords[1]
                buf_b = movdqas[2] + movdqas[3] + dwords[2] + dwords[3]
                candidate = bytes(x ^ y for x, y in zip(buf_a, buf_b)).decode("ascii", "ignore")
                if re.fullmatch(r"[0-9a-f]{40}", candidate):
                    return candidate
    finally:
        pe.close()
    raise ValueError(f"Could not locate integrity salt in {dll_path}")

def _detect_seb_version(root: Path) -> str:
    exe = _find_seb_exe(root)
    _, ver = _file_metadata(exe)
    if ver:
        return ver
    raise ValueError(f"Could not read version from {exe}")

# ---------------------------------------------------------------------------
# SEB Unpack & Key Generation
# ---------------------------------------------------------------------------

_RNCRYPTOR = None

def _get_rncryptor():
    global _RNCRYPTOR
    if _RNCRYPTOR is None:
        import rncryptor  # type: ignore

        class _RawRNCryptor(rncryptor.RNCryptor):
            def _pbkdf2(self, password, salt, iterations=10000, key_length=32):
                return hashlib.pbkdf2_hmac("sha1", password, salt, iterations, key_length)

            def post_decrypt_data(self, data: bytes) -> bytes:
                pad = data[-1]
                if not 1 <= pad <= 16 or data[-pad:] != bytes([pad]) * pad:
                    raise ValueError("invalid padding")
                return data[:-pad]

        _RNCRYPTOR = _RawRNCryptor()
    return _RNCRYPTOR

def unpack_container(data: bytes, password: str = "") -> tuple[bytes, str]:
    current = data
    detected_mode = "xml"
    for _ in range(8):
        if not current: break
        stripped = current.lstrip()
        if stripped.startswith((b"<?xml", b"<!DOCTYPE", b"<plist")):
            return stripped, detected_mode
        if current.startswith(b"\x1f\x8b"):
            current = gzip.decompress(current)
            continue
        header, body = current[:4], current[4:]
        if header in (b"pwcc", b"pswd"):
            detected_mode = header.decode("ascii")
            effective = hashlib.sha256(password.encode("utf-8")).hexdigest().upper() if header == b"pwcc" and password else password
            current = _get_rncryptor().decrypt(body, effective)
            continue
        if header == b"plnd":
            detected_mode = "plnd"
            current = body
            continue
        break
    raise ValueError("input is not a valid SEB configuration")

def _compare_invariant(left: str, right: str) -> int:
    if os.name == "nt":
        try:
            from ctypes import wintypes
            compare = ctypes.windll.kernel32.CompareStringEx
            compare.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.LPCWSTR, ctypes.c_int, wintypes.LPCWSTR, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, wintypes.LPARAM]
            compare.restype = ctypes.c_int
            result = compare("", 0, left, len(left), right, len(right), None, None, 0)
            if result: return result - 2
        except Exception: pass
    a, b = left.casefold(), right.casefold()
    return (a > b) - (a < b)

def _xml_value(element: ET.Element) -> Any:
    tag = element.tag.rsplit("}", 1)[-1]
    if tag == "dict":
        result: dict[str, Any] = {}
        children = list(element)
        for i in range(0, len(children), 2):
            result[children[i].text or ""] = _xml_value(children[i + 1])
        return result
    if tag == "array": return [_xml_value(child) for child in element]
    if tag == "string": return element.text if element.text is not None else None
    if tag == "integer": return int(element.text or "0")
    if tag == "real": return float(element.text or "0")
    if tag in ("true", "false"): return tag == "true"
    if tag == "data": return base64.b64decode("".join((element.text or "").split()))
    if tag == "date":
        value = (element.text or "").strip()
        if value.endswith("Z"): value = value[:-1] + "+00:00"
        parsed = dt.datetime.fromisoformat(value)
        return parsed.astimezone() if parsed.tzinfo is not None else parsed
    raise ValueError(f"Unsupported plist element: {tag}")

def load_settings_xml_bytes(data: bytes) -> dict[str, Any]:
    root = ET.fromstring(data)
    plist = root if root.tag.rsplit("}", 1)[-1] == "plist" else root.find("plist")
    value = _xml_value(plist[0])
    if not isinstance(value, dict): raise ValueError("Expected root dictionary")
    return value

def _dotnet_double(value: float) -> str:
    if math.isnan(value): return "NaN"
    if math.isinf(value): return "Infinity" if value > 0 else "-Infinity"
    text = repr(value)
    if text.endswith(".0"): text = text[:-2]
    if "e" in text:
        mantissa, exponent = text.split("e")
        exp = int(exponent)
        text = f"{mantissa}E{'-' if exp < 0 else '+'}{abs(exp):02d}"
    return text

def _serializable(value: Any) -> str:
    if isinstance(value, dict):
        keys = sorted(value, key=cmp_to_key(_compare_invariant))
        result = "{"
        for i, key in enumerate(keys):
            item = value[key]
            if key.casefold() == "originatorversion" or (isinstance(item, dict) and not item): continue
            result += '"' + key + '":' + _serializable(item) + ("," if i < len(keys) - 1 else "")
        return result + "}"
    if isinstance(value, list):
        return "[" + ",".join(_serializable(item) for item in value) + "]"
    if isinstance(value, (bytes, bytearray)): return '"' + base64.b64encode(value).decode("ascii") + '"'
    if isinstance(value, dt.datetime):
        text = value.isoformat(timespec="microseconds")
        return '"' + text + ('Z' if value.utcoffset() == dt.timedelta(0) else '') + '"'
    if value is None: return '""'
    if isinstance(value, bool): return "true" if value else "false"
    if isinstance(value, int): return str(value)
    if isinstance(value, float): return _dotnet_double(value)
    if isinstance(value, str): return '"' + value + '"'
    raise TypeError(f"Unsupported type: {type(value)}")

def exam_configuration_key(raw_data: dict[str, Any], purpose: str = "as-is") -> str:
    effective = dict(raw_data)
    if purpose != "as-is": effective["sebConfigPurpose"] = 0 if purpose == "starting-exam" else 1
    return hashlib.sha256(_serializable(effective).encode("utf-8")).hexdigest()

def _detect_purpose(mode: str, settings: dict[str, Any]) -> str:
    if mode == "pwcc": return "configuring-client"
    return "as-is" if "sebConfigPurpose" in settings else "starting-exam"

def _salt_hex(settings: dict[str, Any]) -> str:
    value = settings.get("examKeySalt")
    if isinstance(value, (bytes, bytearray)): return bytes(value).hex()
    if isinstance(value, str) and value: return value
    raise ValueError("examKeySalt missing")

def _wide_truncated(text: str) -> bytes:
    encoded = text.encode("utf-16-le", errors="surrogatepass")
    return encoded[:len(encoded) // 2]

def _init_version_dll():
    if os.name != "nt":
        return None
    from ctypes import wintypes

    vdll = ctypes.windll.version
    vdll.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    vdll.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    vdll.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    vdll.GetFileVersionInfoW.restype = wintypes.BOOL
    vdll.VerQueryValueW.argtypes = [
        ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)
    ]
    vdll.VerQueryValueW.restype = wintypes.BOOL

    class VS_FIXEDFILEINFO(ctypes.Structure):
        _fields_ = [
            ("dwSignature", wintypes.DWORD),
            ("dwStrucVersion", wintypes.DWORD),
            ("dwFileVersionMS", wintypes.DWORD),
            ("dwFileVersionLS", wintypes.DWORD),
            ("dwProductVersionMS", wintypes.DWORD),
            ("dwProductVersionLS", wintypes.DWORD),
            ("dwFileFlagsMask", wintypes.DWORD),
            ("dwFileFlags", wintypes.DWORD),
            ("dwFileOS", wintypes.DWORD),
            ("dwFileType", wintypes.DWORD),
            ("dwFileSubtype", wintypes.DWORD),
            ("dwFileDateMS", wintypes.DWORD),
            ("dwFileDateLS", wintypes.DWORD),
        ]

    return vdll, wintypes, VS_FIXEDFILEINFO


_VERSION_DLL = _init_version_dll()


def _file_metadata(path: Path) -> tuple[str, str]:
    if _VERSION_DLL is not None:
        try:
            with path.open("rb") as fh:
                if fh.read(2) != b"MZ":
                    return "", ""
            vdll, wintypes, VS_FIXEDFILEINFO = _VERSION_DLL
            dummy = wintypes.DWORD()
            size = vdll.GetFileVersionInfoSizeW(str(path), ctypes.byref(dummy))
            if not size:
                return "", ""
            buf = ctypes.create_string_buffer(size)
            if not vdll.GetFileVersionInfoW(str(path), 0, size, buf):
                return "", ""
            ptr = ctypes.c_void_p()
            length = wintypes.UINT()
            ver_str = ""
            if vdll.VerQueryValueW(buf, "\\", ctypes.byref(ptr), ctypes.byref(length)) and ptr.value:
                info = VS_FIXEDFILEINFO.from_address(ptr.value)
                ver_str = (
                    f"{info.dwFileVersionMS >> 16}.{info.dwFileVersionMS & 0xffff}."
                    f"{info.dwFileVersionLS >> 16}.{info.dwFileVersionLS & 0xffff}"
                )
            orig = ""
            if (
                vdll.VerQueryValueW(buf, "\\VarFileInfo\\Translation", ctypes.byref(ptr), ctypes.byref(length))
                and ptr.value and length.value >= 4
            ):
                lang, cp = ctypes.cast(ptr.value, ctypes.POINTER(wintypes.WORD * 2)).contents
                sub = f"\\StringFileInfo\\{lang:04x}{cp:04x}\\OriginalFilename"
                if vdll.VerQueryValueW(buf, sub, ctypes.byref(ptr), ctypes.byref(length)) and ptr.value and length.value:
                    orig = ctypes.wstring_at(ptr.value).rstrip("\0")
            return orig, ver_str
        except Exception:
            return "", ""
    return "", ""

def _init_crypt32():
    if os.name != "nt":
        return None
    from ctypes import wintypes

    crypt32 = ctypes.windll.crypt32

    class CERT_CONTEXT(ctypes.Structure):
        _fields_ = [
            ("dwCertEncodingType", wintypes.DWORD),
            ("pbCertEncoded", ctypes.POINTER(ctypes.c_ubyte)),
            ("cbCertEncoded", wintypes.DWORD),
            ("pCertInfo", ctypes.c_void_p),
            ("hCertStore", wintypes.HANDLE),
        ]

    crypt32.CertEnumCertificatesInStore.restype = ctypes.POINTER(CERT_CONTEXT)
    crypt32.CertGetCertificateContextProperty.argtypes = [
        ctypes.POINTER(CERT_CONTEXT), wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)
    ]
    crypt32.CertFreeCertificateContext.argtypes = [ctypes.POINTER(CERT_CONTEXT)]
    crypt32.CertCloseStore.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    return crypt32, wintypes


_CRYPT32 = _init_crypt32()


def _signer_thumbprint(path: Path) -> str:
    """Return embedded Authenticode signer DER SHA-1 via native crypt32.dll."""
    if _CRYPT32 is None:
        return ""
    crypt32, wintypes = _CRYPT32
    h_store = wintypes.HANDLE()
    if not crypt32.CryptQueryObject(
        1, str(path), 1 << 10, 2, 0, None, None, None, ctypes.byref(h_store), None, None
    ) or not h_store:
        return ""
    try:
        ctx = crypt32.CertEnumCertificatesInStore(h_store, None)
        if not ctx:
            return ""
        try:
            buf = (ctypes.c_ubyte * 20)()
            sz = wintypes.DWORD(20)
            if crypt32.CertGetCertificateContextProperty(ctx, 3, buf, ctypes.byref(sz)):
                return bytes(buf[:sz.value]).hex()
            return ""
        finally:
            crypt32.CertFreeCertificateContext(ctx)
    finally:
        crypt32.CertCloseStore(h_store, 0)


def _hash_and_meta(item: tuple[Path, int]) -> tuple[str, str, str, str, str, int]:
    path, size = item
    with path.open("rb") as fh:
        content_hash = hashlib.file_digest(fh, "sha256").hexdigest()
    original, version = _file_metadata(path)
    cert = _signer_thumbprint(path) if path.suffix.lower() in (".dll", ".exe") else ""
    return str(path), cert, content_hash, original, version, size


def tree_fingerprint_bytes(root: Path) -> bytes:
    events: list[tuple[str, str | tuple[Path, int]]] = []
    file_items: list[tuple[Path, int]] = []

    def scan(directory: Path) -> None:
        events.append(("dir", directory.name))
        subdirs, files = [], []
        with os.scandir(directory) as entries:
            for entry in entries:
                try:
                    if entry.is_dir(follow_symlinks=False):
                        subdirs.append(Path(entry.path))
                    elif entry.is_file(follow_symlinks=False):
                        files.append((Path(entry.path), entry.stat(follow_symlinks=False).st_size))
                except OSError:
                    continue
        for child in subdirs:
            scan(child)
        for item in files:
            events.append(("file", item))
            file_items.append(item)

    scan(root)
    with ThreadPoolExecutor(max_workers=min(8, (os.cpu_count() or 4) + 2)) as pool:
        meta_results = list(pool.map(_hash_and_meta, file_items))

    file_info = {k: (cert, ch, orig, ver, sz) for k, cert, ch, orig, ver, sz in meta_results}
    parts = bytearray()
    for kind, payload in events:
        if kind == "dir":
            parts.extend(_wide_truncated(payload))  # type: ignore[arg-type]
        else:
            path, _ = payload  # type: ignore[misc]
            cert, content_hash, original, version, size = file_info[str(path)]
            parts.extend(_wide_truncated(cert + content_hash + path.name + original + str(size) + version))
    return bytes(parts)

def get_install_state(root: Path, use_cache: bool = True) -> tuple[str, str, bytes]:
    exe = _find_seb_exe(root)
    st = exe.stat()
    root_key, mtime_ns, size = str(root.resolve()), st.st_mtime_ns, st.st_size
    if use_cache and DEFAULT_CACHE.is_file():
        try:
            cached = json.loads(DEFAULT_CACHE.read_text(encoding="utf-8"))
            if (
                cached.get("root") == root_key
                and cached.get("mtime_ns") == mtime_ns
                and cached.get("size") == size
                and isinstance(cached.get("seb_version"), str)
                and isinstance(cached.get("integrity_salt"), str)
                and isinstance(cached.get("prefix_b64"), str)
            ):
                return cached["seb_version"], cached["integrity_salt"], base64.b64decode(cached["prefix_b64"])
        except Exception:
            pass
    version = _detect_seb_version(root)
    salt_const = extract_salt_const(root)
    tree = tree_fingerprint_bytes(root)
    if use_cache:
        payload = {
            "root": root_key,
            "mtime_ns": mtime_ns,
            "size": size,
            "seb_version": version,
            "integrity_salt": salt_const,
            "prefix_b64": base64.b64encode(tree).decode("ascii"),
        }
        DEFAULT_CACHE.write_text(json.dumps(payload), encoding="utf-8")
    return version, salt_const, tree

# ---------------------------------------------------------------------------
# CDP / Patchright Integration
# ---------------------------------------------------------------------------

def request_hash(url: str, key_hex: str) -> str:
    url = url.split("#", 1)[0]
    return hashlib.sha256((url + key_hex).encode("utf-8")).hexdigest()

def launch_patchright(start_url: str, version: str, config_key: str, browser_key: str, profile_dir: Path) -> None:
    try:
        from patchright.sync_api import sync_playwright
    except ImportError as exc:
        raise SystemExit("patchright required: pip install patchright") from exc

    profile_dir.mkdir(parents=True, exist_ok=True)

    seb_short = ".".join(version.split(".")[:3])
    user_agent = f"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) SEB/{seb_short} (x64)"
    
    with sync_playwright() as p:
        context = p.chromium.launch_persistent_context(
            user_data_dir=str(profile_dir),
            headless=False,
            no_viewport=True,
            user_agent=user_agent,
            ignore_https_errors=True
        )
        
        # Ensure DEFUSER_JS does NOT patch window.fetch anymore. (Remove the bypassInternetCheck block).
        context.add_init_script(DEFUSER_JS)

        header_cache: dict[str, tuple[str, str]] = {}
        _req_hash = request_hash
        _ck = config_key
        _bek = browser_key

        def route_handler(route):
            req_url = route.request.url
            if "favicon.ico" in req_url or "google.com" in req_url:
                try:
                    response = route.fetch()
                    route.fulfill(response=response)
                except Exception:
                    route.fulfill(status=200, body=b"", headers={"Content-Type": "image/x-icon"})
                return

            # =========================================================
            # Handle CORS Preflight for Sarathi
            # =========================================================
            if route.request.method == "OPTIONS":
                origin = route.request.headers.get("origin", "https://sarathi.parivahan.gov.in")
                route.fulfill(status=204, headers={
                    "Access-Control-Allow-Origin": origin,
                    "Access-Control-Allow-Credentials": "true",
                    "Access-Control-Allow-Headers": "Content-Type, Authorization, X-SafeExamBrowser-ConfigKeyHash, X-SafeExamBrowser-RequestHash",
                    "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS"
                })
                return

            # =========================================================
            # Inject SEB headers for Sarathi
            # =========================================================
            if "sarathi.parivahan.gov.in" in req_url:
                clean_url = req_url.split("#")[0]
                pair = header_cache.get(clean_url)
                if pair is None:
                    pair = (_req_hash(clean_url, _ck), _req_hash(clean_url, _bek))
                    if len(header_cache) < 512:
                        header_cache[clean_url] = pair
                        
                headers = dict(route.request.headers)
                headers["X-SafeExamBrowser-ConfigKeyHash"], headers["X-SafeExamBrowser-RequestHash"] = pair
                
                try:
                    route.continue_(headers=headers)
                except Exception:
                    pass
            else:
                try:
                    route.continue_()
                except Exception:
                    pass

        context.route("**/*", route_handler)
        
        try:
            page = context.pages[0] if context.pages else context.new_page()
            page.goto(start_url, wait_until="domcontentloaded")
            
            while context.pages:
                context.pages[0].wait_for_event("close", timeout=0)
        finally:
            context.close()
def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("seb", type=Path)
    parser.add_argument("--root", type=Path, default=None)
    parser.add_argument("--password", default="")
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--launch", action="store_true")
    args = parser.parse_args()

    if not args.seb.is_file(): parser.error(f"file not found: {args.seb}")

    try:
        root = args.root if args.root is not None else detect_seb_root()
        seb_stat = args.seb.stat()
        pwd_hash = hashlib.sha256(args.password.encode("utf-8")).hexdigest()
        exe = _find_seb_exe(root)
        exe_st = exe.stat()
        root_key, mtime_ns, exe_size = str(root.resolve()), exe_st.st_mtime_ns, exe_st.st_size

        cached_hit = False
        if not args.no_cache and DEFAULT_CACHE.is_file():
            try:
                cached = json.loads(DEFAULT_CACHE.read_text(encoding="utf-8"))
                last_seb = cached.get("last_seb") or {}
                if (
                    cached.get("root") == root_key
                    and cached.get("mtime_ns") == mtime_ns
                    and cached.get("size") == exe_size
                    and last_seb.get("path") == str(args.seb.resolve())
                    and last_seb.get("mtime_ns") == seb_stat.st_mtime_ns
                    and last_seb.get("size") == seb_stat.st_size
                    and last_seb.get("pwd_hash") == pwd_hash
                ):
                    version = cached["seb_version"]
                    config_key = last_seb["config_key"]
                    browser_key = last_seb["browser_key"]
                    start_url = last_seb["start_url"]
                    cached_hit = True
            except Exception:
                pass

        if not cached_hit:
            with ThreadPoolExecutor(max_workers=4) as pool:
                ver_fut = pool.submit(_detect_seb_version, root)
                salt_fut = pool.submit(extract_salt_const, root)
                tree_fut = pool.submit(tree_fingerprint_bytes, root)
                raw = args.seb.read_bytes()
                xml, mode = unpack_container(raw, args.password)
                settings = load_settings_xml_bytes(xml)
                purpose = _detect_purpose(mode, settings)
                salt = _salt_hex(settings)
                config_key = exam_configuration_key(settings, purpose)
                version = ver_fut.result()
                salt_const = salt_fut.result()
                tree = tree_fut.result()

            browser_key = hashlib.sha256(
                tree + config_key.encode("ascii") + salt.encode("ascii") + salt_const.encode("ascii")
            ).hexdigest()
            start_url = settings.get("startURL") or URL
            if not args.no_cache:
                try:
                    payload = {
                        "root": root_key,
                        "mtime_ns": mtime_ns,
                        "size": exe_size,
                        "seb_version": version,
                        "integrity_salt": salt_const,
                        "prefix_b64": base64.b64encode(tree).decode("ascii"),
                        "last_seb": {
                            "path": str(args.seb.resolve()),
                            "mtime_ns": seb_stat.st_mtime_ns,
                            "size": seb_stat.st_size,
                            "pwd_hash": pwd_hash,
                            "config_key": config_key,
                            "browser_key": browser_key,
                            "start_url": start_url,
                        },
                    }
                    DEFAULT_CACHE.write_text(json.dumps(payload), encoding="utf-8")
                except Exception:
                    pass
    except Exception as exc:
        parser.error(str(exc))

    print(f"configKey={config_key}")
    print(f"browserExamKey={browser_key}")
    
    if args.launch:
        launch_patchright(start_url, version, config_key, browser_key, DEFAULT_PROFILE_DIR)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
