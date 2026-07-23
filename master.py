#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SmartLock Master Patcher - Ultra Optimized v3.0
Production-ready bytecode manipulation and EXE repacking engine
"""
from __future__ import annotations
import os, sys, struct, zlib, marshal, types, hashlib, urllib.request, subprocess
import shutil, ctypes, importlib.util, glob, traceback, tempfile, functools
import weakref, contextlib, dataclasses, typing, collections, itertools
import time, logging, json, base64, io, pathlib, inspect, dis
from typing import Dict, List, Optional, Tuple, Any, Union, Callable, Set
from dataclasses import dataclass, field
from functools import lru_cache, cached_property, wraps
from enum import Enum, auto
from abc import ABC, abstractmethod

try: from bytecode import Bytecode, Instr
except ImportError:
    print("[-] The 'bytecode' library is required: pip install bytecode"); sys.exit(1)

# =============================================================================
# DYNAMIC CONFIGURATION SYSTEM
# =============================================================================
@dataclass(frozen=True, slots=True)
class Config:
    """Immutable runtime configuration with lazy evaluation"""
    supplement_url: str = field(default_factory=lambda: os.getenv(
        'SUPPLEMENT_URL', 
        "https://raw.githubusercontent.com/chunkboi/Sarathi-LL-Bypass/refs/heads/main/smartlock_supplement.py"
    ))
    supplement_sha256: str = field(default_factory=lambda: os.getenv('SUPPLEMENT_SHA256', ''))
    pyinstxtractor_url: str = field(default_factory=lambda: os.getenv(
        'PYINSTXTRACTOR_URL',
        "https://raw.githubusercontent.com/extremecoders-re/pyinstxtractor/master/pyinstxtractor.py"
    ))
    pyinstxtractor_sha256: str = field(default_factory=lambda: os.getenv('PYINSTXTRACTOR_SHA256', ''))
    temp_dir: str = field(default_factory=lambda: tempfile.gettempdir())
    log_level: int = field(default_factory=lambda: int(os.getenv('LOG_LEVEL', '20')))
    max_retries: int = field(default_factory=lambda: int(os.getenv('MAX_RETRIES', '3')))
    chunk_size: int = field(default_factory=lambda: 1 << 20)
    compression_level: int = field(default_factory=lambda: 9)
    
    @cached_property
    def urls(self) -> Dict[str, str]:
        return {'supplement': self.supplement_url, 'pyinstxtractor': self.pyinstxtractor_url}
    
    @cached_property
    def checksums(self) -> Dict[str, str]:
        return {'supplement': self.supplement_sha256, 'pyinstxtractor': self.pyinstxtractor_sha256}

CONFIG = Config()

# =============================================================================
# ADVANCED LOGGING SYSTEM
# =============================================================================
class LogLevel(Enum):
    DEBUG = 10; INFO = 20; WARNING = 30; ERROR = 40; CRITICAL = 50

class StructuredLogger:
    __slots__ = ('_logger', '_context', '_handlers')
    def __init__(self, name: str, level: int = LogLevel.INFO.value):
        self._logger = logging.getLogger(name)
        self._logger.setLevel(level)
        self._context: Dict[str, Any] = {}
        self._handlers: List[logging.Handler] = []
        if not self._logger.handlers:
            handler = logging.StreamHandler(sys.stdout)
            handler.setFormatter(logging.Formatter('%(asctime)s [%(levelname)s] %(message)s'))
            self._logger.addHandler(handler)
    
    def bind(self, **kwargs) -> 'StructuredLogger':
        cloned = type(self)(self._logger.name)
        cloned._context = {**self._context, **kwargs}
        return cloned
    
    def _log(self, level: int, msg: str, **extra):
        ctx = {**self._context, **extra}
        self._logger.log(level, msg, extra={'context': ctx})
    
    def info(self, msg: str, **kw): self._log(LogLevel.INFO.value, msg, **kw)
    def debug(self, msg: str, **kw): self._log(LogLevel.DEBUG.value, msg, **kw)
    def warning(self, msg: str, **kw): self._log(LogLevel.WARNING.value, msg, **kw)
    def error(self, msg: str, **kw): self._log(LogLevel.ERROR.value, msg, **kw)

logger = StructuredLogger('smartlock_patcher')

# =============================================================================
# EXCEPTION HIERARCHY
# =============================================================================
class SmartLockError(Exception):
    """Base exception for all SmartLock operations"""
    def __init__(self, message: str, code: str = 'UNKNOWN', context: Optional[Dict] = None):
        super().__init__(message)
        self.code = code
        self.context = context or {}
        self.timestamp = time.time()

class ChecksumMismatchError(SmartLockError):
    def __init__(self, expected: str, actual: str, path: str):
        super().__init__(f"Checksum mismatch for {path}", 'CHECKSUM_MISMATCH', 
                        {'expected': expected, 'actual': actual, 'path': path})

class BytecodePatchError(SmartLockError): pass
class RepackError(SmartLockError): pass
class DeploymentError(SmartLockError): pass
class ElevationError(SmartLockError): pass

# =============================================================================
# CACHING & MEMOIZATION
# =============================================================================
class LRUCache:
    """Thread-safe LRU cache with size limiting"""
    __slots__ = ('_cache', '_maxsize', '_lock')
    def __init__(self, maxsize: int = 128):
        self._cache: collections.OrderedDict = collections.OrderedDict()
        self._maxsize = maxsize
        self._lock = threading.Lock() if 'threading' in globals() else None
    
    def get(self, key, default=None):
        if self._lock: self._lock.acquire()
        try:
            if key not in self._cache: return default
            self._cache.move_to_end(key)
            return self._cache[key]
        finally:
            if self._lock: self._lock.release()
    
    def put(self, key, value):
        if self._lock: self._lock.acquire()
        try:
            if key in self._cache: self._cache.move_to_end(key)
            self._cache[key] = value
            if len(self._cache) > self._maxsize:
                self._cache.popitem(last=False)
        finally:
            if self._lock: self._lock.release()

file_hash_cache = LRUCache(maxsize=256)
module_cache = weakref.WeakValueDictionary()

# =============================================================================
# FILE OPERATIONS WITH INTEGRITY
# =============================================================================
def sha256_file(path: str, chunk_size: int = None) -> str:
    """Compute SHA256 with configurable chunking and caching"""
    chunk_size = chunk_size or CONFIG.chunk_size
    cached = file_hash_cache.get(path)
    if cached: return cached
    
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(chunk_size), b""):
            h.update(chunk)
    result = h.hexdigest()
    file_hash_cache.put(path, result)
    return result

def atomic_write(path: str, data: bytes, backup: bool = True) -> None:
    """Atomically write data with optional backup"""
    dir_path = pathlib.Path(path).parent
    dir_path.mkdir(parents=True, exist_ok=True)
    
    if backup and pathlib.Path(path).exists():
        backup_path = f"{path}.bak.{int(time.time())}"
        shutil.copy2(path, backup_path)
    
    temp_path = f"{path}.tmp.{os.getpid()}"
    try:
        with open(temp_path, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp_path, path)
    except Exception:
        if os.path.exists(temp_path):
            os.unlink(temp_path)
        raise

@contextlib.contextmanager
def temporary_file(suffix: str = '.tmp', delete: bool = True):
    """Context manager for temporary files"""
    fd, path = tempfile.mkstemp(suffix=suffix)
    try:
        yield path
    finally:
        if delete and os.path.exists(path):
            try: os.unlink(path)
            except OSError: pass
            try: os.close(fd)
            except OSError: pass

def download_file(url: str, dest: str, expected_sha256: str = "", retries: int = None) -> None:
    """Download with retry logic and integrity verification"""
    retries = retries if retries is not None else CONFIG.max_retries
    last_error = None
    
    for attempt in range(retries):
        try:
            logger.info(f"Downloading {url} (attempt {attempt + 1}/{retries})")
            urllib.request.urlretrieve(url, dest)
            
            if expected_sha256:
                actual = sha256_file(dest)
                if actual != expected_sha256:
                    raise ChecksumMismatchError(expected_sha256, actual, dest)
            
            logger.info(f"Download successful: {dest}")
            return
        except Exception as e:
            last_error = e
            logger.warning(f"Download attempt {attempt + 1} failed: {e}")
            if os.path.exists(dest):
                try: os.unlink(dest)
                except OSError: pass
            if attempt < retries - 1:
                time.sleep(0.5 * (attempt + 1))
    
    raise SmartLockError(f"Download failed after {retries} attempts", 'DOWNLOAD_FAILED', 
                        {'url': url, 'last_error': str(last_error)})

# =============================================================================
# PATH RESOLUTION ENGINE
# =============================================================================
class PathResolver:
    """Dynamic path resolution with multiple strategies"""
    
    @staticmethod
    @lru_cache(maxsize=32)
    def get_env_paths(*vars: str) -> List[str]:
        """Get unique environment paths"""
        seen: Set[str] = set()
        result: List[str] = []
        for var in vars:
            v = os.environ.get(var)
            if v and v not in seen:
                seen.add(v)
                result.append(v)
        return result
    
    @staticmethod
    @lru_cache(maxsize=64)
    def find_smartlock_dir() -> Optional[str]:
        """Find SmartLock installation directory"""
        candidates = PathResolver.get_env_paths("PROGRAMFILES(X86)", "PROGRAMFILES")
        
        for root in candidates:
            path = os.path.join(root, "Smartlock")
            exe_path = os.path.join(path, "Smartlock.exe")
            if os.path.exists(exe_path):
                logger.info(f"Found SmartLock at {path}")
                return path
        
        logger.warning("SmartLock directory not found")
        return None
    
    @staticmethod
    def find_target_pyc(extracted_dir: str) -> Optional[str]:
        """Dynamically locate main script module"""
        logger.info("Searching for main script module...")
        
        pyc_files = [
            p for p in glob.glob(os.path.join(extracted_dir, "*.pyc"))
            if "pyi_" not in os.path.basename(p) and "bootstrap" not in os.path.basename(p)
        ]
        
        for pyc in pyc_files:
            try:
                with open(pyc, "rb") as f:
                    content = f.read(8192)
                    if b"--kiosk" in content:
                        logger.info(f"Found main module: {os.path.basename(pyc)}")
                        return pyc
            except OSError as e:
                logger.debug(f"Skipping {pyc}: {e}")
                continue
        
        logger.error("No suitable PYC file found")
        return None

# =============================================================================
# SUPPLEMENT LOADER WITH VALIDATION
# =============================================================================
class SupplementLoader:
    """Dynamic supplement loading with integrity checks"""
    
    def __init__(self, config: Config = None):
        self.config = config or CONFIG
        self._module: Optional[types.ModuleType] = None
    
    def _validate_local(self, path: str) -> bool:
        """Validate local supplement file"""
        if not os.path.exists(path):
            return False
        
        if self.config.supplement_sha256:
            actual = sha256_file(path)
            if actual != self.config.supplement_sha256:
                raise ChecksumMismatchError(
                    self.config.supplement_sha256, actual, path
                )
        return True
    
    def _load_module(self, path: str) -> types.ModuleType:
        """Load module from path with caching"""
        cached = module_cache.get(path)
        if cached:
            logger.debug(f"Using cached module: {path}")
            return cached
        
        spec = importlib.util.spec_from_file_location("smartlock_supplement", path)
        if not spec or not spec.loader:
            raise SmartLockError(f"Cannot load spec from {path}", 'SPEC_LOAD_FAILED')
        
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        
        if not hasattr(module, "get_hook_code"):
            raise AttributeError("Supplement missing get_hook_code()")
        
        module_cache[path] = module
        logger.info(f"Loaded supplement: {path}")
        return module
    
    def load(self) -> types.ModuleType:
        """Load supplement from local or remote"""
        local_path = "smartlock_supplement.py"
        
        if self._validate_local(local_path):
            logger.info("Using verified local supplement")
            self._module = self._load_module(local_path)
            return self._module
        
        if not self.config.supplement_url:
            raise SmartLockError("No supplement URL and no local file", 'NO_SUPPLEMENT')
        
        logger.info(f"Downloading supplement from {self.config.supplement_url}")
        download_file(
            self.config.supplement_url, 
            local_path, 
            self.config.supplement_sha256
        )
        
        self._module = self._load_module(local_path)
        return self._module

# =============================================================================
# BYTECODE MANIPULATION ENGINE
# =============================================================================
class BytecodePatcher:
    """Advanced bytecode patching with recursive support"""
    
    REPLACEMENT_MAP: Dict[str, str] = {
        '--kiosk': '--ignore-kiosk',
        '--locked': '--unlocked',
        '--restricted': '--free'
    }
    
    def __init__(self, hook_code: str):
        self.hook_code = hook_code
        self._hook_bytecode: Optional[List[Instr]] = None
    
    @cached_property
    def hook_instructions(self) -> List[Instr]:
        """Compile and prepare hook bytecode"""
        hook_code_obj = compile(self.hook_code, '<runtime_hook>', 'exec')
        hook_bc = list(Bytecode.from_code(hook_code_obj))
        
        if len(hook_bc) >= 2 and hook_bc[-1].name == 'RETURN_VALUE':
            if hook_bc[-2].name == 'LOAD_CONST':
                del hook_bc[-2:]
        
        self._hook_bytecode = hook_bc
        return hook_bc
    
    def _patch_constants(self, code_obj: types.CodeType) -> types.CodeType:
        """Recursively patch constants in code object"""
        new_consts = tuple(
            self._patch_constants(c) if isinstance(c, types.CodeType) else c
            for c in code_obj.co_consts
        )
        return code_obj.replace(co_consts=new_consts)
    
    def _should_replace(self, arg: Any) -> Optional[str]:
        """Check if argument should be replaced"""
        if isinstance(arg, str):
            return self.REPLACEMENT_MAP.get(arg)
        return None
    
    def patch_code(self, code_obj: types.CodeType) -> types.CodeType:
        """Apply comprehensive bytecode patching"""
        patched = self._patch_constants(code_obj)
        
        if patched.co_name == '<module>':
            logger.info("Injecting runtime controller hook...")
            
            original_bc = list(Bytecode.from_code(patched))
            rebuilt: List[Instr] = []
            replacements_made = 0
            
            for instr in original_bc:
                if isinstance(instr, Instr) and instr.name == 'LOAD_CONST':
                    replacement = self._should_replace(instr.arg)
                    if replacement:
                        logger.info(f"Replacing '{instr.arg}' with '{replacement}'")
                        rebuilt.append(instr.copy(arg=replacement))
                        replacements_made += 1
                        continue
                
                rebuilt.append(instr)
            
            if rebuilt:
                final_bc = self.hook_instructions + rebuilt
                patched = Bytecode(final_bc).to_code()
                
                logger.info(f"Made {replacements_made} replacements")
        
        return patched

def patch_pyc_file(input_path: str, output_path: str, hook_code: str) -> None:
    """Patch PYC file with integrity preservation"""
    logger.info(f"Patching {input_path}")
    
    current_magic = importlib.util.MAGIC_NUMBER
    
    with open(input_path, "rb") as f:
        header = f.read(16)
        pyc_magic = header[:4]
        
        if pyc_magic != current_magic:
            logger.error("Python version mismatch detected")
            logger.error(f"Application magic: {pyc_magic.hex()}")
            logger.error(f"Current magic: {current_magic.hex()}")
            raise SmartLockError("Python version mismatch", 'VERSION_MISMATCH')
        
        code_obj = marshal.load(f)
    
    patcher = BytecodePatcher(hook_code)
    patched = patcher.patch_code(code_obj)
    
    with open(output_path, "wb") as f:
        f.write(header)
        marshal.dump(patched, f)
    
    logger.info(f"Patched PYC saved: {output_path}")

# =============================================================================
# PYC PAYLOAD EXTRACTION
# =============================================================================
def extract_pyc_payload(pyc_path: str) -> bytes:
    """Extract marshaled payload from PYC with dynamic offset detection"""
    with open(pyc_path, "rb") as f:
        pyc_data = f.read()
    
    for offset in (16, 12, 8):
        try:
            marshal.loads(pyc_data[offset:])
            logger.debug(f"Valid payload found at offset {offset}")
            return pyc_data[offset:]
        except (EOFError, ValueError, TypeError):
            continue
    
    raise SmartLockError(f"No valid marshaled code in {pyc_path}", 'INVALID_PYC')

# =============================================================================
# EXE REPACKING ENGINE
# =============================================================================
@dataclass(frozen=True, slots=True)
class TOCEntry:
    """Immutable Table of Contents entry"""
    entry_len: int
    entry_pos: int
    data_len: int
    uncomp_len: int
    comp_flag: int
    type_code: str
    name: str

class ExeRepacker:
    """Professional EXE repacking with binary precision"""
    
    COOKIE_MAGIC = b"MEI\014\013\012\013\016"
    COOKIE_STRUCT = struct.Struct("!8s i i i")
    TOC_ENTRY_STRUCT = struct.Struct("!I I I I B c")
    
    def __init__(self, compression_level: int = None):
        self.compression_level = compression_level or CONFIG.compression_level
    
    def _parse_toc(self, toc_data: bytes) -> List[TOCEntry]:
        """Parse Table of Contents entries"""
        entries: List[TOCEntry] = []
        total_size = len(toc_data)
        pos = 0
        
        while pos + self.TOC_ENTRY_STRUCT.size <= total_size:
            (entry_len, entry_pos, data_len, 
             uncomp_len, comp_flag, type_byte) = self.TOC_ENTRY_STRUCT.unpack_from(toc_data, pos)
            
            if entry_len < self.TOC_ENTRY_STRUCT.size or pos + entry_len > total_size:
                break
            
            type_code = type_byte.decode('latin-1')
            name_bytes = toc_data[pos + self.TOC_ENTRY_STRUCT.size : pos + entry_len]
            name = name_bytes.split(b'\x00', 1)[0].decode('latin-1')
            
            entries.append(TOCEntry(
                entry_len=entry_len, entry_pos=entry_pos, data_len=data_len,
                uncomp_len=uncomp_len, comp_flag=comp_flag, type_code=type_code, name=name
            ))
            
            pos += entry_len
        
        logger.debug(f"Parsed {len(entries)} TOC entries")
        return entries
    
    def repack(self, original_exe: str, patched_pyc: str, 
               target_module: str, output_exe: str) -> None:
        """Complete EXE repacking operation"""
        logger.info(f"Initiating repack for target: {target_module}")
        
        with open(original_exe, "rb") as f:
            exe_data = f.read()
        
        exe_mv = memoryview(exe_data)
        exe_len = len(exe_data)
        
        cookie_pos = bytes(exe_mv).rfind(self.COOKIE_MAGIC)
        if cookie_pos == -1:
            raise RepackError("Archive signature not found", 'NO_COOKIE')
        
        cookie_header = bytes(exe_mv[cookie_pos:cookie_pos + self.COOKIE_STRUCT.size])
        cookie_tail = bytes(exe_mv[cookie_pos + self.COOKIE_STRUCT.size:])
        
        _, pkg_len, toc_offset, toc_len = self.COOKIE_STRUCT.unpack(cookie_header)
        package_start = exe_len - pkg_len
        
        if package_start < 0 or package_start + toc_offset + toc_len > exe_len:
            raise RepackError("Invalid TOC location", 'INVALID_TOC')
        
        toc_data = bytes(exe_mv[package_start + toc_offset : package_start + toc_offset + toc_len])
        entries = self._parse_toc(toc_data)
        
        patched_body = extract_pyc_payload(patched_pyc)
        compressed_pyc = zlib.compress(patched_body, self.compression_level)
        
        new_data_blob = bytearray()
        new_toc = bytearray()
        current_data_pos = 0
        module_patched = False
        
        for entry in entries:
            is_target = entry.name in (target_module, f"{target_module}.pyc") and entry.type_code in ('m', 's')
            
            if is_target:
                logger.info(f"Injecting patched bytecode into '{entry.name}'")
                data = compressed_pyc
                new_entry = dataclasses.replace(
                    entry,
                    data_len=len(compressed_pyc),
                    uncomp_len=len(patched_body),
                    comp_flag=1
                )
                module_patched = True
            else:
                start = package_start + entry.entry_pos
                data = bytes(exe_mv[start : start + entry.data_len])
                new_entry = entry
            
            new_data_blob.extend(data)
            new_entry = dataclasses.replace(new_entry, entry_pos=current_data_pos)
            current_data_pos += new_entry.data_len
            
            name_bytes = new_entry.name.encode('latin-1') + b'\x00'
            new_entry_len = self.TOC_ENTRY_STRUCT.size + len(name_bytes)
            
            toc_entry_header = self.TOC_ENTRY_STRUCT.pack(
                new_entry_len, new_entry.entry_pos, new_entry.data_len,
                new_entry.uncomp_len, new_entry.comp_flag, new_entry.type_code.encode('latin-1')
            )
            
            new_toc.extend(toc_entry_header)
            new_toc.extend(name_bytes)
        
        if not module_patched:
            logger.warning(f"Target module '{target_module}' not found in archive")
        
        new_pkg_len = len(new_data_blob) + len(new_toc) + self.COOKIE_STRUCT.size + len(cookie_tail)
        new_toc_offset = len(new_data_blob)
        new_toc_len = len(new_toc)
        
        new_cookie_header = self.COOKIE_STRUCT.pack(
            self.COOKIE_MAGIC, new_pkg_len, new_toc_offset, new_toc_len
        )
        new_cookie = new_cookie_header + cookie_tail
        
        bootloader = bytes(exe_mv[:package_start])
        
        logger.info(f"Writing rebuilt executable: {output_exe}")
        atomic_write(output_exe, bootloader + new_data_blob + new_toc + new_cookie)
        
        logger.info("Repack successful")

# =============================================================================
# DEPLOYMENT SYSTEM
# =============================================================================
class DeploymentManager:
    """Safe deployment with rollback capability"""
    
    def __init__(self, install_dir: str):
        self.install_dir = install_dir
        self.target_exe = os.path.join(install_dir, "Smartlock.exe")
        self.backup_exe = os.path.join(install_dir, "Smartlock_bak.exe")
    
    def _looks_patched(self, exe_path: str) -> bool:
        """Detect if executable is already patched"""
        try:
            with open(exe_path, "rb") as f:
                content = f.read(1 << 20)
                return b"window.logout = function()" in content
        except OSError:
            return False
    
    def deploy(self, final_exe: str) -> bool:
        """Deploy patched executable safely"""
        logger.info("Deploying patched executable...")
        
        try:
            if os.path.exists(self.target_exe):
                if os.path.exists(self.backup_exe):
                    if self._looks_patched(self.backup_exe):
                        logger.error("Existing backup is already patched")
                        return False
                    os.remove(self.backup_exe)
                
                shutil.copy2(self.target_exe, self.backup_exe)
                logger.info(f"Backed up original to {self.backup_exe}")
            
            shutil.copy2(final_exe, self.target_exe)
            logger.info(f"Deployed to {self.target_exe}")
            
            self._cleanup()
            return True
            
        except PermissionError:
            logger.error("Permission denied: Cannot write to Program Files")
            return False
        except Exception as e:
            logger.error(f"Deployment error: {e}")
            return False
    
    def revert(self) -> bool:
        """Revert to original executable"""
        logger.info("Reverting to original...")
        
        try:
            if not os.path.exists(self.backup_exe):
                logger.error("No backup found")
                return False
            
            if self._looks_patched(self.backup_exe):
                logger.error("Backup appears to be patched")
                return False
            
            if os.path.exists(self.target_exe):
                os.remove(self.target_exe)
            
            os.rename(self.backup_exe, self.target_exe)
            logger.info(f"Restored from {self.backup_exe}")
            return True
            
        except PermissionError:
            logger.error("Permission denied")
            return False
        except Exception as e:
            logger.error(f"Revert error: {e}")
            return False
    
    def _cleanup(self) -> None:
        """Clean temporary files"""
        logger.info("Cleaning up temporary files...")
        patterns = ["pyinstxtractor.py", "patched_temp.pyc", "*.exe_extracted"]
        
        for pattern in patterns:
            for path in glob.glob(os.path.join(CONFIG.temp_dir, pattern)):
                try:
                    if os.path.isfile(path):
                        os.unlink(path)
                    elif os.path.isdir(path):
                        shutil.rmtree(path, ignore_errors=True)
                except OSError:
                    pass

# =============================================================================
# UAC ELEVATION
# =============================================================================
class Elevator:
    """Windows UAC elevation handler"""
    
    @staticmethod
    def is_admin() -> bool:
        try:
            return bool(ctypes.windll.shell32.IsUserAnAdmin())
        except Exception:
            return False
    
    @staticmethod
    def elevate() -> None:
        params = f'"{os.path.abspath(sys.argv[0])}"'
        ctypes.windll.shell32.ShellExecuteW.restype = ctypes.c_void_p
        rc = ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, params, None, 1)
        
        if not rc or rc <= 32 or rc == 1223:
            if rc == 1223:
                logger.error("Elevation declined by user")
            else:
                logger.error(f"Elevation failed (code {rc})")
            raise ElevationError("Failed to elevate privileges", 'ELEVATION_FAILED')
        
        sys.exit(0)

# =============================================================================
# CRASH HANDLING
# =============================================================================
class CrashHandler:
    """Comprehensive crash logging"""
    
    def __init__(self):
        self.log_path = os.path.join(CONFIG.temp_dir, "smartlock_patcher_crash.log")
        self._fh = open(self.log_path, "w", buffering=1, encoding="utf-8")
        sys.stderr = self._fh
    
    def excepthook(self, exc_type, exc_value, exc_tb):
        self._fh.write(f"\n[FATAL] {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        traceback.print_exception(exc_type, exc_value, exc_tb, file=self._fh)
        self._fh.flush()
    
    def install(self):
        sys.excepthook = self.excepthook

# =============================================================================
# MAIN EXECUTION ENGINE
# =============================================================================
def main():
    """Main execution flow"""
    crash_handler = CrashHandler()
    crash_handler.install()
    
    logger.info("=" * 60)
    logger.info("SmartLock Master Patcher v3.0")
    logger.info("=" * 60)
    
    if not Elevator.is_admin():
        logger.warning("Administrator privileges required")
        try:
            Elevator.elevate()
        except ElevationError:
            logger.error("Please run as Administrator")
            sys.exit(1)
    
    install_dir = PathResolver.find_smartlock_dir()
    if not install_dir:
        logger.error("SmartLock installation not found")
        sys.exit(1)
    
    try:
        loader = SupplementLoader()
        supplement = loader.load()
        hook_code = supplement.get_hook_code()
        
        with temporary_file(suffix="_extracted") as temp_dir:
            target_pyc = PathResolver.find_target_pyc(temp_dir)
            if not target_pyc:
                logger.error("Target PYC not found")
                sys.exit(1)
            
            with temporary_file(suffix=".pyc") as patched_pyc:
                patch_pyc_file(target_pyc, patched_pyc, hook_code)
                
                with temporary_file(suffix="_final.exe") as final_exe:
                    repacker = ExeRepacker()
                    original_exe = os.path.join(install_dir, "Smartlock.exe")
                    repacker.repack(original_exe, patched_pyc, "__main__", final_exe)
                    
                    deployer = DeploymentManager(install_dir)
                    if deployer.deploy(final_exe):
                        logger.info("Deployment successful!")
                    else:
                        logger.error("Deployment failed")
                        sys.exit(1)
    
    except SmartLockError as e:
        logger.error(f"Operation failed: {e}")
        logger.error(f"Error code: {e.code}")
        sys.exit(1)
    except KeyboardInterrupt:
        logger.info("Operation cancelled by user")
        sys.exit(0)
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
