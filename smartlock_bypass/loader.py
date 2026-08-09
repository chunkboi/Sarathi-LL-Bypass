"""
Supplement loader module - loads and validates the hook supplement.
"""

import sys
import os
import importlib.util
import traceback

from .config import SUPPLEMENT_URL, SUPPLEMENT_SHA256
from .utils import sha256_file, download_file


def load_supplement():
    """
    Load the supplement module containing hook code.
    
    Downloads supplement if not present locally, verifies checksums,
    and imports the module dynamically.
    
    Returns:
        module: Loaded supplement module with get_hook_code() function
        
    Raises:
        SystemExit: If supplement cannot be loaded
    """
    local_supp_path = "smartlock_supplement.py"

    if not os.path.exists(local_supp_path):
        if not SUPPLEMENT_URL:
            print("[-] SUPPLEMENT_URL is empty and no local supplement found.")
            sys.exit(1)
        print(f"[*] Downloading supplement from {SUPPLEMENT_URL}...")
        try:
            download_file(SUPPLEMENT_URL, local_supp_path, SUPPLEMENT_SHA256)
        except Exception as e:
            print(f"[-] Supplement download/integrity failure: {e}")
            sys.exit(1)
        print("[+] Supplement downloaded and verified.")
    else:
        if SUPPLEMENT_SHA256:
            actual = sha256_file(local_supp_path)
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
