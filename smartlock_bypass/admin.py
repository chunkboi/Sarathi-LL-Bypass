"""
Windows UAC elevation and crash logging utilities.

Provides admin privilege detection/elevation and global exception handling
for robust error reporting during patch operations.
"""

import sys
import os
import ctypes
import tempfile
import traceback

from .config import CRASH_LOG_FILENAME


def is_admin():
    """
    Check if running with administrator privileges (Windows only).
    
    Returns:
        bool: True if admin, False otherwise
    """
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def elevate():
    """
    Request UAC elevation and restart script as administrator.
    
    Exits current process after launching elevated copy.
    """
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


# Global crash logging setup
CRASH_LOG = os.path.join(tempfile.gettempdir(), CRASH_LOG_FILENAME)
_crash_fh = open(CRASH_LOG, "w", buffering=1, encoding="utf-8")
sys.stderr = _crash_fh


def _excepthook(exc_type, exc_value, exc_tb):
    """Global exception handler that writes to crash log."""
    _crash_fh.write("\n[FATAL]\n")
    traceback.print_exception(exc_type, exc_value, exc_tb, file=_crash_fh)
    _crash_fh.flush()
    try:
        traceback.print_exception(exc_type, exc_value, exc_tb)
    except Exception:
        pass


sys.excepthook = _excepthook
