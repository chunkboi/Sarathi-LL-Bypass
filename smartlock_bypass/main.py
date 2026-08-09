"""
Main entry point for SmartLock Bypass - provides interactive menu and orchestrates patching.
"""

import sys
import os
import shutil
import subprocess
import traceback

from .config import (
    PYINSTXTRACTOR_URL, 
    PYINSTXTRACTOR_SHA256,
    SMARTLOCK_EXE_NAME
)
from .utils import download_file
from .resolver import find_smartlock_dir, find_target_pyc
from .loader import load_supplement
from .patcher import patch_pyc_file
from .repacker import repack_exe
from .deployer import deploy_patch, revert_patch
from .admin import is_admin, elevate


def main_menu():
    """Interactive menu for patch operations."""
    supp_module = load_supplement()
    hook_code = supp_module.get_hook_code()

    install_dir = find_smartlock_dir()
    if not install_dir:
        print("[-] Could not automatically find Smartlock installation directory.")
        install_dir = input("[?] Please enter the full path to the Smartlock folder: ").strip().strip('"')
        if not os.path.exists(os.path.join(install_dir, SMARTLOCK_EXE_NAME)):
            print(f"[-] {SMARTLOCK_EXE_NAME} not found in that directory. Exiting.")
            return

    while True:
        print(f"\n=== Smartlock Patcher Menu (Target: {install_dir}) ===")
        print("1. Patch and Deploy (Unlocks app, disables blocking, highlights answers)")
        print("2. Revert to Original (Restores backup)")
        print("3. Exit")
        choice = input("Select an option (1/2/3): ").strip()

        if choice == '1':
            _run_patch_workflow(install_dir, hook_code)
        elif choice == '2':
            revert_patch(install_dir)
        elif choice == '3':
            print("Exiting...")
            break
        else:
            print("[-] Invalid choice. Please enter 1, 2, or 3.")


def _run_patch_workflow(install_dir, hook_code):
    """Execute the complete patching workflow."""
    target_exe = os.path.join(install_dir, SMARTLOCK_EXE_NAME)
    if not os.path.exists(target_exe):
        print(f"[-] Error: Could not find {target_exe}.")
        return

    local_exe = SMARTLOCK_EXE_NAME
    print(f"[*] Copying original exe from {install_dir}...")
    shutil.copy2(target_exe, local_exe)

    print(f"[*] Downloading pyinstxtractor.py from GitHub...")
    download_file(PYINSTXTRACTOR_URL, "pyinstxtractor.py", PYINSTXTRACTOR_SHA256)
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


def run():
    """Main entry point with platform checks and error handling."""
    if os.name != "nt":
        print("[-] Windows-only.")
        sys.exit(1)

    if not is_admin():
        print("[-] Administrator privileges required. Requesting elevation...")
        elevate()

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
            # Flush and close crash log handler
            sys.stderr.flush()
            sys.stderr.close()
        except Exception:
            pass
        input("\nPress Enter to exit...")
