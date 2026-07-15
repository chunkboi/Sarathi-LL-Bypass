import os
import sys
import struct
import zlib
import marshal
import types
import urllib.request
import subprocess
import shutil
import ctypes
import importlib.util
import tempfile
import glob
import traceback

try:
    from bytecode import Bytecode, Instr
except ImportError:
    print("[-] The 'bytecode' library is required. Please install it: pip install bytecode")
    sys.exit(1)

# ==============================================================================
# URLS & CONFIGURATION (Edit these if links change)
# ==============================================================================
# URL to the raw text of your smartlock_supplement.py file
SUPPLEMENT_URL = "https://raw.githubusercontent.com/chunkboi/Sarathi-LL-Bypass/refs/heads/main/smartlock_supplement.py"

# URL to download PyInstaller Extractor
PYINSTXTRACTOR_URL = "https://raw.githubusercontent.com/extremecoders-re/pyinstxtractor/master/pyinstxtractor.py"
# ==============================================================================

# ==========================================
# DYNAMIC PATH & FILE RESOLUTION
# ==========================================
def find_smartlock_dir():
    possible_roots = [
        os.environ.get("PROGRAMFILES"),
        os.environ.get("PROGRAMFILES(X86)"),
        r"C:\Program Files",
        r"C:\Program Files (x86)"
    ]
    for root in possible_roots:
        if root:
            path = os.path.join(root, "Smartlock")
            if os.path.exists(os.path.join(path, "Smartlock.exe")):
                return path
    return None

def find_target_pyc(extracted_dir):
    """Dynamically finds the main .pyc file by searching for the '--kiosk' string."""
    print("[*] Dynamically searching for the main script module...")
    pyc_files = glob.glob(os.path.join(extracted_dir, "*.pyc"))
    for pyc in pyc_files:
        if "pyi_" in pyc or "bootstrap" in pyc:
            continue
        try:
            with open(pyc, 'rb') as f:
                if b'--kiosk' in f.read():
                    print(f"[+] Found main module target: {os.path.basename(pyc)}")
                    return pyc
        except Exception:
            pass
    return None

# ==========================================
# SUPPLEMENT LOADER
# ==========================================
def load_supplement():
    """Downloads and dynamically imports the supplement script."""
    local_supp_path = "smartlock_supplement.py"
    
    if not os.path.exists(local_supp_path):
        print(f"[*] Downloading supplement script from {SUPPLEMENT_URL}...")
        try:
            urllib.request.urlretrieve(SUPPLEMENT_URL, local_supp_path)
            print("[+] Downloaded supplement script.")
        except Exception as e:
            print(f"[-] Failed to download supplement script: {e}")
            print("[-] Please check the SUPPLEMENT_URL or place smartlock_supplement.py manually in this folder.")
            sys.exit(1)
    else:
        print("[*] Found local smartlock_supplement.py. Using it.")

    try:
        spec = importlib.util.spec_from_file_location("smartlock_supplement", local_supp_path)
        supp_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(supp_module)
        print("[+] Supplement script loaded successfully.")
        return supp_module
    except Exception as e:
        print(f"[-] Error loading supplement script: {e}")
        sys.exit(1)

# ==========================================
# BYTECODE PATCHING LOGIC (Optimized)
# ==========================================
def patch_bytecode_recursive(code_obj, hook_code):
    # 1. Recursive Heavy-Lifting First: 
    new_consts = tuple(
        patch_bytecode_recursive(const, hook_code) if isinstance(const, types.CodeType) else const
        for const in code_obj.co_consts
    )
    
    patched_code_obj = code_obj.replace(co_consts=new_consts)

    # 2. Scope-Specific Bytecode Injection
    if patched_code_obj.co_name == '<module>':
        print("[*] Injecting runtime controller hook...")
        
        hook_code_obj = compile(hook_code, '<runtime_hook>', 'exec')
        hook_bc = list(Bytecode.from_code(hook_code_obj))
        
        # O(1) Slice deletion to remove trailing LOAD_CONST None and RETURN_VALUE
        if len(hook_bc) >= 2 and hook_bc[-1].name == 'RETURN_VALUE' and hook_bc[-2].name == 'LOAD_CONST':
            del hook_bc[-2:]
            
        original_bc = Bytecode.from_code(patched_code_obj)
        
        # In-place search and replace.
        for instr in original_bc:
            if isinstance(instr, Instr) and instr.name == 'LOAD_CONST' and instr.arg == '--kiosk':
                print("[!] Replacing '--kiosk' with '--ignore-kiosk'...")
                instr.arg = '--ignore-kiosk'
                
        # O(N) Slice insertion. Cleanly prepends the hook.
        original_bc[:0] = hook_bc
        
        patched_code_obj = original_bc.to_code()
        
    return patched_code_obj

def patch_pyc_file(input_path, output_path, hook_code):
    print(f"\n[*] Patching {input_path}...")
    current_magic = importlib.util.MAGIC_NUMBER
    
    with open(input_path, 'rb') as f:
        header = f.read(16)
        pyc_magic = header[:4]
        
        # FAIL FAST: Do not marshal.load if the magic is wrong! 
        if pyc_magic != current_magic:
            print(f"\n[-] FATAL ERROR: Python Version Mismatch!")
            print(f"[-] Application uses magic: {pyc_magic.hex()}")
            print(f"[-] Your Python uses magic: {current_magic.hex()}")
            sys.exit(1)
            
        code_obj = marshal.load(f)
        
    patched_code = patch_bytecode_recursive(code_obj, hook_code)
    
    with open(output_path, 'wb') as f:
        f.write(header)
        marshal.dump(patched_code, f)
        
    print(f"[+] Saved {output_path}")

# ==========================================
# EXE ARCHIVE LOGIC (Dynamic & Struct-based)
# ==========================================
def get_dynamic_pyc_payload(pyc_path):
    """
    Dynamically strips the .pyc header regardless of the Python version.
    Python 2.7: 8 bytes
    Python 3.3-3.6: 12 bytes
    Python 3.7+: 16 bytes
    """
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

def repack_exe(original_exe_path, patched_pyc_path, target_module_name, output_exe_path):
    print(f"\n[*] Initiating dynamic repack sequence for target: {target_module_name}")
    
    with open(original_exe_path, "rb") as f:
        exe_data = f.read()

    # ==========================================
    # AXIOMS (Pre-compiled Structs for Optimization)
    # ==========================================
    COOKIE_MAGIC = b'MEI\014\013\012\013\016'
    COOKIE_STRUCT = struct.Struct('!8s i i i')
    TOC_ENTRY_STRUCT = struct.Struct('!I I I I B c')
    
    # ==========================================
    # 1. Locate and Parse the Cookie
    # ==========================================
    cookie_pos = exe_data.rfind(COOKIE_MAGIC)
    if cookie_pos == -1:
        print("[-] ERROR: Archive signature (Magic Cookie) not found. Is this a PyInstaller EXE?")
        sys.exit(1)

    cookie_header = exe_data[cookie_pos : cookie_pos + COOKIE_STRUCT.size]
    cookie_tail = exe_data[cookie_pos + COOKIE_STRUCT.size :] 
    
    _, pkg_len, toc_offset, toc_len = COOKIE_STRUCT.unpack(cookie_header)
    package_start = len(exe_data) - pkg_len

    # ==========================================
    # 2. Parse the Table of Contents (TOC)
    # ==========================================
    toc_data_exe = exe_data[package_start + toc_offset : package_start + toc_offset + toc_len]
    total_toc_size = len(toc_data_exe)
    
    entries = []
    pos = 0
    
    while pos < total_toc_size:
        if pos + TOC_ENTRY_STRUCT.size > total_toc_size:
            break
            
        entry_len, entry_pos, data_len, uncomp_len, comp_flag, type_code_byte = TOC_ENTRY_STRUCT.unpack_from(toc_data_exe, pos)
        
        if entry_len < TOC_ENTRY_STRUCT.size or (pos + entry_len) > total_toc_size:
            break
            
        type_code = type_code_byte.decode('latin-1')
        name_bytes = toc_data_exe[pos + TOC_ENTRY_STRUCT.size : pos + entry_len]
        name = name_bytes.split(b'\x00', 1)[0].decode('latin-1')
        
        entries.append({
            'entry_len': entry_len, 'entry_pos': entry_pos, 'data_len': data_len, 
            'uncomp_len': uncomp_len, 'comp_flag': comp_flag, 'type_code': type_code, 
            'name': name
        })
        pos += entry_len

    # ==========================================
    # 3. Dynamically Process the Patched Payload
    # ==========================================
    patched_body = get_dynamic_pyc_payload(patched_pyc_path)
    compressed_pyc = zlib.compress(patched_body)

    # ==========================================
    # 4. Rebuild the Data Blob and TOC
    # ==========================================
    new_data_blob_exe = bytearray()
    new_toc_exe = bytearray()
    current_data_pos = 0
    module_patched = False

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
            data_start = package_start + entry['entry_pos']
            data = exe_data[data_start : data_start + entry['data_len']]
        
        new_data_blob_exe.extend(data)
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
        new_toc_exe.extend(toc_entry_header)
        new_toc_exe.extend(name_bytes)

    if not module_patched:
        print(f"[-] WARNING: Target module '{target_module_name}' was NOT found in the EXE archive.")

    # ==========================================
    # 5. Dynamically Recalculate Limits and Rebuild
    # ==========================================
    new_pkg_len = len(new_data_blob_exe) + len(new_toc_exe) + COOKIE_STRUCT.size + len(cookie_tail)
    new_toc_offset = len(new_data_blob_exe)
    new_toc_len = len(new_toc_exe)

    new_cookie_header = COOKIE_STRUCT.pack(
        COOKIE_MAGIC, 
        new_pkg_len, 
        new_toc_offset, 
        new_toc_len
    )
    new_cookie = new_cookie_header + cookie_tail

    bootloader = exe_data[:package_start]
    
    print(f"[*] Writing rebuilt executable to: {output_exe_path}")
    with open(output_exe_path, "wb") as f:
        f.write(bootloader)
        f.write(new_data_blob_exe)
        f.write(new_toc_exe)
        f.write(new_cookie)
        
    print("[+] Repack successful. Executable is ready.")

# ==========================================
# DEPLOYMENT & CLEANUP LOGIC
# ==========================================
def deploy_patch(final_exe_path, install_dir):
    print("\n[*] Deploying patched executable...")
    target_exe = os.path.join(install_dir, "Smartlock.exe")
    backup_exe = os.path.join(install_dir, "Smartlock_bak.exe")
    
    try:
        if os.path.exists(target_exe):
            if os.path.exists(backup_exe):
                os.remove(backup_exe)
            os.rename(target_exe, backup_exe)
            print(f"[+] Backed up original to {backup_exe}")
            
        shutil.copy(final_exe_path, target_exe)
        print(f"[+] Deployed patched exe to {target_exe}")
        
        print("[*] Cleaning up temporary files...")
        if os.path.exists("pyinstxtractor.py"):
            os.remove("pyinstxtractor.py")
        if os.path.exists("Smartlock.exe_extracted"):
            shutil.rmtree("Smartlock.exe_extracted")
        if os.path.exists("patched_temp.pyc"):
            os.remove("patched_temp.pyc")
        if os.path.exists(final_exe_path):
            os.remove(final_exe_path)
        if os.path.exists("Smartlock.exe"):
            os.remove("Smartlock.exe")
        print("[+] Cleanup complete.")
        
    except PermissionError:
        print("\n[-] PERMISSION DENIED: Cannot write to Program Files.")
    except Exception as e:
        print(f"[-] Error during deployment: {e}")

def revert_patch(install_dir):
    print("\n[*] Reverting to original executable...")
    target_exe = os.path.join(install_dir, "Smartlock.exe")
    backup_exe = os.path.join(install_dir, "Smartlock_bak.exe")
    
    try:
        if not os.path.exists(backup_exe):
            print("[-] No backup found. Cannot revert.")
            return
            
        if os.path.exists(target_exe):
            os.remove(target_exe)
            print("[+] Removed patched executable.")
            
        os.rename(backup_exe, target_exe)
        print(f"[+] Restored original from {backup_exe}")
        print("[+] Revert complete.")
        
    except PermissionError:
        print("\n[-] PERMISSION DENIED: Cannot write to Program Files.")
    except Exception as e:
        print(f"[-] Error during revert: {e}")

# ==========================================
# MAIN MENU & UAC ELEVATION
# ==========================================
def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False

def main_menu():
    supp_module = load_supplement()
    hook_code = supp_module.get_hook_code()

    install_dir = find_smartlock_dir()
    if not install_dir:
        print("[-] Could not automatically find Smartlock installation directory.")
        install_dir = input("[?] Please enter the full path to the Smartlock folder (e.g. C:\\Program Files (x86)\\Smartlock): ").strip()
        if not os.path.exists(os.path.join(install_dir, "Smartlock.exe")):
            print("[-] Smartlock.exe not found in that directory. Exiting.")
            return

    while True:
        print(f"\n=== Smartlock Patcher Menu (Target: {install_dir}) ===")
        print("1. Patch and Deploy (Unlocks app, disables blocking, highlights answers)")
        print("2. Revert to Original (Restores backup)")
        print("3. Exit")
        choice = input("Select an option (1/2/3): ")
        
        if choice == '1':
            target_exe = os.path.join(install_dir, "Smartlock.exe")
            if not os.path.exists(target_exe):
                print(f"[-] Error: Could not find {target_exe}.")
                continue

            local_exe = "Smartlock.exe"
            print(f"[*] Copying original exe from {install_dir}...")
            shutil.copy(target_exe, local_exe)

            print(f"[*] Downloading pyinstxtractor.py from GitHub...")
            urllib.request.urlretrieve(PYINSTXTRACTOR_URL, "pyinstxtractor.py")
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
    if not is_admin():
        print("[-] Administrator privileges required. Requesting elevation...")
        ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, f'"{os.path.abspath(sys.argv[0])}"', None, 1)
        sys.exit(0)
        
    try:
        main_menu()
    except Exception as e:
        print("\n[FATAL ERROR]")
        traceback.print_exc()
        
    input("\nPress Enter to exit...")
