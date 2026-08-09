"""
Configuration constants for SmartLock Bypass.

Separating configuration allows easy customization without modifying core logic.
"""

# Supplement module download URL
SUPPLEMENT_URL = "https://raw.githubusercontent.com/chunkboi/Sarathi-LL-Bypass/refs/heads/main/smartlock_supplement.py"
SUPPLEMENT_SHA256 = ""  # Optional checksum verification

# pyinstxtractor download URL
PYINSTXTRACTOR_URL = "https://raw.githubusercontent.com/extremecoders-re/pyinstxtractor/master/pyinstxtractor.py"
PYINSTXTRACTOR_SHA256 = ""  # Optional checksum verification

# Executable and directory names
SMARTLOCK_EXE_NAME = "Smartlock.exe"
SMARTLOCK_DIR_NAME = "Smartlock"
BACKUP_EXE_NAME = "Smartlock_bak.exe"

# Cookie magic bytes for PyInstaller EXE format
COOKIE_MAGIC = b"MEI\014\013\012\013\016"

# Crash log filename
CRASH_LOG_FILENAME = "smartlock_patcher_crash.log"
