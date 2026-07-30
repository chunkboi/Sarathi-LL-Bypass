import os
import sys
import struct
import zlib
import marshal
import types
import hashlib
import urllib.request
import subprocess
import shutil
import ctypes
import importlib.util
import glob
import traceback
import tempfile

try:
    from bytecode import Bytecode, Instr
except ImportError:
    print("[-] The 'bytecode' library is required. Please install it: pip install bytecode")
    try:
        input("\nPress Enter to exit...")
    except EOFError:
        pass
    sys.exit(1)

# ==============================================================================
# CONFIGURATION
# ==============================================================================
SUPPLEMENT_URL             = "https://raw.githubusercontent.com/chunkboi/Sarathi-LL-Bypass/refs/heads/main/smartlock_supplement.py"
SUPPLEMENT_SHA256          = ""  

PYINSTXTRACTOR_URL         = "https://raw.githubusercontent.com/extremecoders-re/pyinstxtractor/master/pyinstxtractor.py"
PYINSTXTRACTOR_SHA256      = ""  

# ==============================================================================
# UTILITIES
# ==============================================================================
def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def _download(url, dest, expected_sha256=""):
    urllib.request.urlretrieve(url, dest)
    if expected_sha256:
        actual = _sha256_file(dest)
        if actual != expected_sha256:
            raise RuntimeError(
                f"Checksum mismatch for {url}\n"
                f"  expected {expected_sha256}\n"
                f"  got      {actual}"
            )

# ==============================================================================
# PATH RESOLUTION
# ==============================================================================
def find_smartlock_dir():
    seen = set()
    candidates = []
    for var in ("PROGRAMFILES(X86)", "PROGRAMFILES"):
        v = os.environ.get(var)
        if v and v not in seen:
            seen.add(v)
            candidates.append(v)
            
    for root in candidates:
        path = os.path.join(root, "Smartlock")
        if os.path.exists(os.path.join(path, "Smartlock.exe")):
            return path
    return None

def find_target_pyc(extracted_dir):
    print("[*] Dynamically searching for the main script module...")
    pyc_files = [
        p for p in glob.glob(os.path.join(extracted_dir, "*.pyc"))
        if "pyi_" not in os.path.basename(p) and "bootstrap" not in os.path.basename(p)
    ]
    for pyc in pyc_files:
        try:
            with open(pyc, "rb") as f:
                if b"--kiosk" in f.read():
                    print(f"[+] Found main module target: {os.path.basename(pyc)}")
                    return pyc
        except OSError:
            continue
    return None

# ==============================================================================
# SUPPLEMENT LOADER
# ==============================================================================
def load_supplement():
    local_supp_path = "smartlock_supplement.py"

    if not os.path.exists(local_supp_path):
        if not SUPPLEMENT_URL:
            print("[-] SUPPLEMENT_URL is empty and no local supplement found.")
            sys.exit(1)
        print(f"[*] Downloading supplement from {SUPPLEMENT_URL}...")
        try:
            _download(SUPPLEMENT_URL, local_supp_path, SUPPLEMENT_SHA256)
        except Exception as e:
            print(f"[-] Supplement download/integrity failure: {e}")
            sys.exit(1)
        print("[+] Supplement downloaded and verified.")
    else:
        if SUPPLEMENT_SHA256:
            actual = _sha256_file(local_supp_path)
            if actual != SUPPLEMENT_SHA256:
                print(f"[-] Local supplement hash mismatch.\n  expected {SUPPLEMENT_SHA256}\n  got      {actual}")
                sys.exit(1)
        print("[*] Found local smartlock_supplement.py (verified).")

    try:
        spec = importlib.util.spec_from_file_location("smartlock_supplement", local_supp_path)
        supp_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(supp_module)
        if not hasattr(supp_module, "get_hook_code"):
            raise AttributeError("supplement is missing get_hook_code()")
        print("[+] Supplement loaded.")
        return supp_module
    except Exception as e:
        print(f"[-] Error loading supplement: {e}")
        traceback.print_exc()
        sys.exit(1)

# ==============================================================================
# BYTECODE PATCHING
# ==============================================================================
def patch_bytecode_recursive(code_obj, hook_code):
    new_consts = tuple(
        patch_bytecode_recursive(const, hook_code) if isinstance(const, types.CodeType) else const
        for const in code_obj.co_consts
    )
    
    patched_code_obj = code_obj.replace(co_consts=new_consts)

    if patched_code_obj.co_name == '<module>':
        print("[*] Injecting runtime controller hook...")
        
        hook_code_obj = compile(hook_code, '<runtime_hook>', 'exec')
        hook_bc = list(Bytecode.from_code(hook_code_obj))
        
        if len(hook_bc) >= 2 and hook_bc[-1].name == 'RETURN_VALUE' and hook_bc[-2].name == 'LOAD_CONST':
            del hook_bc[-2:]
            
        original_bc = Bytecode.from_code(patched_code_obj)
        
        rebuilt = []
        for instr in original_bc:
            if isinstance(instr, Instr) and instr.name == 'LOAD_CONST' and instr.arg == '--kiosk':
                print("[!] Replacing '--kiosk' with '--ignore-kiosk'...")
                rebuilt.append(instr.copy(arg='--ignore-kiosk'))
            else:
                rebuilt.append(instr)
                
        original_bc[:] = hook_bc + rebuilt
        
        patched_code_obj = original_bc.to_code()
        
    return patched_code_obj

def patch_pyc_file(input_path, output_path, hook_code):
    print(f"\n[*] Patching {input_path}...")
    current_magic = importlib.util.MAGIC_NUMBER

    with open(input_path, "rb") as f:
        header   = f.read(16)
        pyc_magic = header[:4]
        if pyc_magic != current_magic:
            print("\n[-] FATAL: Python version mismatch.")
            print(f"[-] Application magic: {pyc_magic.hex()}")
            print(f"[-] Your magic:        {current_magic.hex()}")
            sys.exit(1)
        code_obj = marshal.load(f)

    patched = patch_bytecode_recursive(code_obj, hook_code)

    with open(output_path, "wb") as f:
        f.write(header)
        marshal.dump(patched, f)
    print(f"[+] Saved {output_path}")

# ==============================================================================
# PYC HEADER STRIPPING
# ==============================================================================
def get_dynamic_pyc_payload(pyc_path):
    with open(pyc_path, "rb") as f:
        pyc_data = f.read()

    for offset in (16, 12, 8):
        try:
            marshal.loads(pyc_data[offset:])
            return pyc_data[offset:]
        except (EOFError, ValueError, TypeError):
            continue
            
    print(f"[-] ERROR: '{pyc_path}' does not contain a valid marshaled code object.")
    sys.exit(1)

# ==============================================================================
# EXE REPACK
# ==============================================================================
COOKIE_MAGIC      = b"MEI\014\013\012\013\016"
COOKIE_STRUCT     = struct.Struct("!8s i i i")
TOC_ENTRY_STRUCT  = struct.Struct("!I I I I B c")

def repack_exe(original_exe_path, patched_pyc_path, target_module_name, output_exe_path):
    print(f"\n[*] Initiating dynamic repack sequence for target: {target_module_name}")
    
    with open(original_exe_path, "rb") as f:
        exe_mv = memoryview(f.read())
    exe_len = len(exe_mv)

    cookie_pos = bytes(exe_mv).rfind(COOKIE_MAGIC)
    if cookie_pos == -1:
        print("[-] ERROR: Archive signature (Magic Cookie) not found. Is this a PyInstaller EXE?")
        sys.exit(1)

    cookie_header = bytes(exe_mv[cookie_pos:cookie_pos + COOKIE_STRUCT.size])
    cookie_tail   = bytes(exe_mv[cookie_pos + COOKIE_STRUCT.size:])
    
    _, pkg_len, toc_offset, toc_len = COOKIE_STRUCT.unpack(cookie_header)
    package_start = exe_len - pkg_len

    if package_start < 0 or package_start + toc_offset + toc_len > exe_len:
        print("[-] Cookie claims a TOC outside the file. Corrupt EXE?")
        sys.exit(1)

    toc_data = bytes(exe_mv[package_start + toc_offset : package_start + toc_offset + toc_len])
    total_toc_size = len(toc_data)
    
    entries = []
    pos = 0
    
    while pos + TOC_ENTRY_STRUCT.size <= total_toc_size:
        entry_len, entry_pos, data_len, uncomp_len, comp_flag, type_byte = TOC_ENTRY_STRUCT.unpack_from(toc_data, pos)
        
        if entry_len < TOC_ENTRY_STRUCT.size or pos + entry_len > total_toc_size:
            break
            
        type_code = type_byte.decode('latin-1')
        name_bytes = toc_data[pos + TOC_ENTRY_STRUCT.size : pos + entry_len]
        name = name_bytes.split(b'\x00', 1)[0].decode('latin-1')
        
        entries.append({
            'entry_len': entry_len, 'entry_pos': entry_pos, 'data_len': data_len, 
            'uncomp_len': uncomp_len, 'comp_flag': comp_flag, 'type_code': type_code, 
            'name': name
        })
        pos += entry_len

    patched_body = get_dynamic_pyc_payload(patched_pyc_path)
    compressed_pyc = zlib.compress(patched_body, 9)

    new_data_blob = bytearray()
    new_toc = bytearray()
    current_data_pos = 0
    module_patched = False

    for e in entries:
        start = package_start + e["entry_pos"]
        end = start + e["data_len"]
        if start < package_start or end > exe_len:
            print(f"[-] TOC entry '{e['name']}' has out-of-range slice. Aborting.")
            sys.exit(1)

    for entry in entries:
        is_target = entry['name'] in (target_module_name, target_module_name + '.pyc') and entry['type_code'] in ('m', 's')
        
        if is_target:
            print(f"[+] Injecting patched bytecode into '{target_module_name}'...")
            entry['data_len'] = len(compressed_pyc)
            entry['uncomp_len'] = len(patched_body)
            entry['comp_flag'] = 1
            data = compressed_pyc
            module_patched = True
        else:
            start = package_start + entry['entry_pos']
            data = bytes(exe_mv[start : start + entry['data_len']])
        
        new_data_blob.extend(data)
        entry['entry_pos'] = current_data_pos
        current_data_pos += entry['data_len']
        
        name_bytes = entry['name'].encode('latin-1') + b'\x00'
        new_entry_len = TOC_ENTRY_STRUCT.size + len(name_bytes)
        
        toc_entry_header = TOC_ENTRY_STRUCT.pack(
            new_entry_len, 
            entry['entry_pos'], 
            entry['data_len'], 
            entry['uncomp_len'], 
            entry['comp_flag'], 
            entry['type_code'].encode('latin-1')
        )
        new_toc.extend(toc_entry_header)
        new_toc.extend(name_bytes)

    if not module_patched:
        print(f"[-] WARNING: Target module '{target_module_name}' was NOT found in the EXE archive.")

    new_pkg_len = len(new_data_blob) + len(new_toc) + COOKIE_STRUCT.size + len(cookie_tail)
    new_toc_offset = len(new_data_blob)
    new_toc_len = len(new_toc)

    new_cookie_header = COOKIE_STRUCT.pack(COOKIE_MAGIC, new_pkg_len, new_toc_offset, new_toc_len)
    new_cookie = new_cookie_header + cookie_tail

    bootloader = bytes(exe_mv[:package_start])
    
    print(f"[*] Writing rebuilt executable to: {output_exe_path}")
    with open(output_exe_path, "wb") as f:
        f.write(bootloader)
        f.write(new_data_blob)
        f.write(new_toc)
        f.write(new_cookie)
        
    print("[+] Repack successful. Executable is ready.")

# ==============================================================================
# INTEGRITY / DEPLOYMENT
# ==============================================================================
def _looks_patched(exe_path):
    try:
        with open(exe_path, "rb") as f:
            return b"window.logout = function()" in f.read()
    except OSError:
        return False

def deploy_patch(final_exe_path, install_dir):
    print("\n[*] Deploying patched executable...")
    target_exe = os.path.join(install_dir, "Smartlock.exe")
    backup_exe = os.path.join(install_dir, "Smartlock_bak.exe")
    
    try:
        if os.path.exists(target_exe):
            if os.path.exists(backup_exe):
                if _looks_patched(backup_exe):
                    print("[-] Existing backup is itself patched. Aborting to avoid losing the original.")
                    return
                os.remove(backup_exe)
            shutil.copy2(target_exe, backup_exe)
            print(f"[+] Backed up original to {backup_exe}")
            
        shutil.copy2(final_exe_path, target_exe)
        print(f"[+] Deployed patched exe to {target_exe}")
        
        print("[*] Cleaning up temporary files...")
        for p in ("pyinstxtractor.py", "patched_temp.pyc", "Smartlock_final.exe", "Smartlock.exe"):
            if os.path.exists(p):
                try: os.remove(p)
                except OSError: pass
        if os.path.exists("Smartlock.exe_extracted"):
            shutil.rmtree("Smartlock.exe_extracted", ignore_errors=True)
        print("[+] Cleanup complete.")
        
    except PermissionError:
        print("\n[-] PERMISSION DENIED: Cannot write to Program Files.")
    except Exception as e:
        print(f"[-] Error during deployment: {e}")
        traceback.print_exc()

def revert_patch(install_dir):
    print("\n[*] Reverting to original executable...")
    target_exe = os.path.join(install_dir, "Smartlock.exe")
    backup_exe = os.path.join(install_dir, "Smartlock_bak.exe")
    
    try:
        if not os.path.exists(backup_exe):
            print("[-] No backup found. Cannot revert.")
            return
            
        if _looks_patched(backup_exe):
            print("[-] Backup file appears to already be patched. Refusing to restore.")
            return
            
        if os.path.exists(target_exe):
            os.remove(target_exe)
            
        os.rename(backup_exe, target_exe)
        print(f"[+] Restored original from {backup_exe}")
        
    except PermissionError:
        print("\n[-] PERMISSION DENIED: Cannot write to Program Files.")
    except Exception as e:
        print(f"[-] Error during revert: {e}")
        traceback.print_exc()

# ==============================================================================
# UAC
# ==============================================================================
def is_admin():
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False

def _elevate():
    params = f'"{os.path.abspath(sys.argv[0])}"'
    ctypes.windll.shell32.ShellExecuteW.restype = ctypes.c_void_p
    rc = ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, params, None, 1)
    
    if not rc or rc <= 32 or rc == 1223:
        if rc == 1223:
            print("[-] Elevation declined by user.")
        else:
            print(f"[-] Elevation failed (code {rc}). Please run as Administrator.")
        sys.exit(1)
    sys.exit(0)

# ==============================================================================
# CRASH LOGGING
# ==============================================================================
CRASH_LOG = os.path.join(tempfile.gettempdir(), "smartlock_patcher_crash.log")
_crash_fh = open(CRASH_LOG, "w", buffering=1, encoding="utf-8")
sys.stderr = _crash_fh

def _excepthook(exc_type, exc_value, exc_tb):
    _crash_fh.write("\n[FATAL]\n")
    traceback.print_exception(exc_type, exc_value, exc_tb, file=_crash_fh)
    _crash_fh.flush()
    try:
        traceback.print_exception(exc_type, exc_value, exc_tb)
    except Exception:
        pass
sys.excepthook = _excepthook

# ==============================================================================
# MAIN
# ==============================================================================
def main_menu():
    supp_module = load_supplement()
    hook_code = supp_module.get_hook_code()

    install_dir = find_smartlock_dir()
    if not install_dir:
        print("[-] Could not automatically find Smartlock installation directory.")
        install_dir = input("[?] Please enter the full path to the Smartlock folder: ").strip().strip('"')
        if not os.path.exists(os.path.join(install_dir, "Smartlock.exe")):
            print("[-] Smartlock.exe not found in that directory. Exiting.")
            return

    while True:
        print(f"\n=== Smartlock Patcher Menu (Target: {install_dir}) ===")
        print("1. Patch and Deploy (Unlocks app, disables blocking, highlights answers)")
        print("2. Revert to Original (Restores backup)")
        print("3. Exit")
        choice = input("Select an option (1/2/3): ").strip()
        
        if choice == '1':
            target_exe = os.path.join(install_dir, "Smartlock.exe")
            if not os.path.exists(target_exe):
                print(f"[-] Error: Could not find {target_exe}.")
                continue

            local_exe = "Smartlock.exe"
            print(f"[*] Copying original exe from {install_dir}...")
            shutil.copy2(target_exe, local_exe)

            print(f"[*] Downloading pyinstxtractor.py from GitHub...")
            _download(PYINSTXTRACTOR_URL, "pyinstxtractor.py", PYINSTXTRACTOR_SHA256)
            print("[+] Downloaded pyinstxtractor.py\n")

            print("[*] Extracting Smartlock.exe...")
            subprocess.run([sys.executable, "pyinstxtractor.py", local_exe], check=True)
            print("[+] Extraction complete.\n")

            extracted_dir = "Smartlock.exe_extracted"
            target_pyc_path = find_target_pyc(extracted_dir)
            
            if not target_pyc_path:
                print("[-] Could not dynamically find the main script. Falling back to 'core.pyc'")
                target_pyc_path = os.path.join(extracted_dir, "core.pyc")
                if not os.path.exists(target_pyc_path):
                    print("[-] Fallback failed. Exiting.")
                    sys.exit(1)

            target_module_name = os.path.splitext(os.path.basename(target_pyc_path))[0]
            patched_pyc_path = "patched_temp.pyc"
            
            patch_pyc_file(target_pyc_path, patched_pyc_path, hook_code)

            final_exe_path = "Smartlock_final.exe"
            repack_exe(local_exe, patched_pyc_path, target_module_name, final_exe_path)

            deploy_patch(final_exe_path, install_dir)
            
        elif choice == '2':
            revert_patch(install_dir)
            
        elif choice == '3':
            print("Exiting...")
            break
        else:
            print("[-] Invalid choice. Please enter 1, 2, or 3.")

if __name__ == "__main__":
    if os.name != "nt":
        print("[-] Windows-only.")
        sys.exit(1)
        
    if not is_admin():
        print("[-] Administrator privileges required. Requesting elevation...")
        _elevate()
        
    try:
        main_menu()
    except SystemExit as e:
        if e.code != 0 and e.code is not None:
            print(f"\n[-] Script exited with error code: {e.code}")
    except Exception:
        print("\n[-] An unexpected error occurred:")
        traceback.print_exc()
    finally:
        try:
            _crash_fh.flush()
            _crash_fh.close()
        except Exception:
            pass
        input("\nPress Enter to exit...")
