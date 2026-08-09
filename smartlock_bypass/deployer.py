"""
Deployment module - handles installation, backup, and restoration of patched executables.
"""

import os
import shutil
import traceback

from .config import SMARTLOCK_EXE_NAME, BACKUP_EXE_NAME


def _looks_patched(exe_path):
    """Check if an executable appears to already be patched."""
    try:
        with open(exe_path, "rb") as f:
            return b"window.logout = function()" in f.read()
    except OSError:
        return False


def deploy_patch(final_exe_path, install_dir):
    """
    Deploy patched executable to SmartLock installation directory.
    
    Creates backup of original before deployment.
    
    Args:
        final_exe_path: Path to patched executable
        install_dir: SmartLock installation directory
        
    Raises:
        PermissionError: If admin rights needed for Program Files
    """
    print("\n[*] Deploying patched executable...")
    target_exe = os.path.join(install_dir, SMARTLOCK_EXE_NAME)
    backup_exe = os.path.join(install_dir, BACKUP_EXE_NAME)
    
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
        temp_files = ["pyinstxtractor.py", "patched_temp.pyc", "Smartlock_final.exe", "Smartlock.exe"]
        for p in temp_files:
            if os.path.exists(p):
                try:
                    os.remove(p)
                except OSError:
                    pass
        if os.path.exists("Smartlock.exe_extracted"):
            shutil.rmtree("Smartlock.exe_extracted", ignore_errors=True)
        print("[+] Cleanup complete.")
        
    except PermissionError:
        print("\n[-] PERMISSION DENIED: Cannot write to Program Files.")
    except Exception as e:
        print(f"[-] Error during deployment: {e}")
        traceback.print_exc()


def revert_patch(install_dir):
    """
    Revert to original executable from backup.
    
    Args:
        install_dir: SmartLock installation directory
        
    Raises:
        PermissionError: If admin rights needed for Program Files
    """
    print("\n[*] Reverting to original executable...")
    target_exe = os.path.join(install_dir, SMARTLOCK_EXE_NAME)
    backup_exe = os.path.join(install_dir, BACKUP_EXE_NAME)
    
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
