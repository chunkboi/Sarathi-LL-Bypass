"""
PyInstaller EXE repacking module - modifies compiled executables.

Handles parsing and rebuilding PyInstaller single-file executables,
including TOC (Table of Contents) manipulation and bytecode injection.
"""

import sys
import struct
import zlib

from .config import COOKIE_MAGIC
from .stripper import get_pyc_payload


# Binary structure definitions
COOKIE_STRUCT = struct.Struct("!8s i i i")
TOC_ENTRY_STRUCT = struct.Struct("!I I I I B c")


def repack_exe(original_exe_path, patched_pyc_path, target_module_name, output_exe_path):
    """
    Repack a PyInstaller executable with patched bytecode.
    
    Args:
        original_exe_path: Path to original Smartlock.exe
        patched_pyc_path: Path to patched .pyc file
        target_module_name: Name of target module to replace
        output_exe_path: Path for output patched executable
        
    Raises:
        SystemExit: If EXE format is invalid or repacking fails
    """
    print(f"\n[*] Initiating dynamic repack sequence for target: {target_module_name}")
    
    with open(original_exe_path, "rb") as f:
        exe_mv = memoryview(f.read())
    exe_len = len(exe_mv)

    cookie_pos = bytes(exe_mv).rfind(COOKIE_MAGIC)
    if cookie_pos == -1:
        print("[-] ERROR: Archive signature (Magic Cookie) not found. Is this a PyInstaller EXE?")
        sys.exit(1)

    cookie_header = bytes(exe_mv[cookie_pos:cookie_pos + COOKIE_STRUCT.size])
    cookie_tail = bytes(exe_mv[cookie_pos + COOKIE_STRUCT.size:])
    
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

    patched_body = get_pyc_payload(patched_pyc_path)
    compressed_pyc = zlib.compress(patched_body, 9)

    new_data_blob = bytearray()
    new_toc = bytearray()
    current_data_pos = 0
    module_patched = False

    # Validate all TOC entries first
    for e in entries:
        start = package_start + e["entry_pos"]
        end = start + e["data_len"]
        if start < package_start or end > exe_len:
            print(f"[-] TOC entry '{e['name']}' has out-of-range slice. Aborting.")
            sys.exit(1)

    # Process entries and build new archive
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
