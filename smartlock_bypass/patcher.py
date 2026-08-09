"""
Bytecode patching module - modifies Python bytecode to inject hooks.
"""

import types
import marshal
import importlib.util

from bytecode import Bytecode, Instr


def patch_bytecode_recursive(code_obj, hook_code):
    """
    Recursively patch bytecode objects and inject hook code.
    
    Args:
        code_obj: Python code object to patch
        hook_code: Hook code string to inject
        
    Returns:
        types.CodeType: Patched code object
    """
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
    """
    Patch a .pyc file with hook code.
    
    Args:
        input_path: Path to input .pyc file
        output_path: Path to save patched .pyc file
        hook_code: Hook code string to inject
        
    Raises:
        SystemExit: If Python version mismatch detected
    """
    print(f"\n[*] Patching {input_path}...")
    current_magic = importlib.util.MAGIC_NUMBER

    with open(input_path, "rb") as f:
        header = f.read(16)
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
