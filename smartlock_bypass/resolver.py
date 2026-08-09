"""
Path resolution utilities for finding SmartLock installation and target modules.
"""

import os
import glob


def find_smartlock_dir():
    """
    Find the SmartLock installation directory.
    
    Searches Program Files and Program Files (x86) for Smartlock.exe.
    
    Returns:
        str: Path to SmartLock directory, or None if not found
    """
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
    """
    Dynamically search for the main script module in extracted PyInstaller files.
    
    Looks for .pyc files containing '--kiosk' marker.
    
    Args:
        extracted_dir: Directory containing extracted .pyc files
        
    Returns:
        str: Path to target .pyc file, or None if not found
    """
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
