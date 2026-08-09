"""
PYC header stripping utility - removes Python bytecode headers for embedding.
"""

import sys
import marshal


def get_pyc_payload(pyc_path):
    """
    Extract raw marshaled code from a .pyc file by stripping the header.
    
    Tries multiple header sizes (16, 12, 8 bytes) to find valid payload.
    
    Args:
        pyc_path: Path to .pyc file
        
    Returns:
        bytes: Raw marshaled code without header
        
    Raises:
        SystemExit: If no valid marshaled code found
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
