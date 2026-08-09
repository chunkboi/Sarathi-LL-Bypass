"""
Utility functions for file operations, downloads, hashing, and Python version detection.
"""

import hashlib
import urllib.request
import struct


# Magic number to Python version mapping (common versions)
# Format: magic_bytes -> (major, minor)
MAGIC_TO_VERSION = {
    b'\x0d\x0d\r\n': (3, 5),
    b'\xee\xee\r\n': (3, 6),
    b'\xf7\x0f\r\n': (3, 7),
    b'\xf9\x0f\r\n': (3, 8),
    b'\xfc\x0f\r\n': (3, 9),
    b'\xfe\x0f\r\n': (3, 10),
    b'\x04\x10\r\n': (3, 11),
    b'\x0a\x10\r\n': (3, 12),
    b'\x0e\x10\r\n': (3, 13),
}


def get_python_version_from_magic(magic_bytes):
    """
    Infer Python version from bytecode magic number.
    
    Args:
        magic_bytes: 4-byte magic number from .pyc file
        
    Returns:
        tuple: (major, minor) version tuple, or None if unknown
    """
    if len(magic_bytes) < 4:
        return None
    
    # Magic number is first 2 bytes (little-endian)
    magic_short = magic_bytes[:2]
    
    # Try exact match first
    for magic, version in MAGIC_TO_VERSION.items():
        if magic[:2] == magic_short:
            return version
    
    # If not found, try to infer from known patterns
    # Magic numbers typically increment sequentially
    try:
        magic_int = struct.unpack('<H', magic_short)[0]
        # Base magic for Python 3.5 is 0x0d0d (3341)
        base_magic = 0x0d0d
        base_version = (3, 5)
        
        if magic_int >= base_magic:
            # Approximate version based on offset
            offset = magic_int - base_magic
            # Rough estimate: ~16-20 magic increments per minor version
            minor_offset = offset // 18
            estimated_minor = base_version[1] + minor_offset
            if estimated_minor <= 20:  # Sanity check
                return (base_version[0], estimated_minor)
    except Exception:
        pass
    
    return None


def format_magic_info(magic_bytes):
    """
    Format magic bytes with version information.
    
    Args:
        magic_bytes: 4-byte magic number
        
    Returns:
        str: Formatted string with hex and version info
    """
    version = get_python_version_from_magic(magic_bytes)
    hex_str = magic_bytes.hex()
    if version:
        return f"{hex_str} (Python {version[0]}.{version[1]})"
    else:
        return f"{hex_str} (unknown version)"


def sha256_file(path):
    """Calculate SHA256 hash of a file."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download_file(url, dest, expected_sha256=""):
    """
    Download a file from URL to destination path.
    
    Args:
        url: Source URL
        dest: Destination file path
        expected_sha256: Optional expected checksum for verification
        
    Raises:
        RuntimeError: If checksum verification fails
    """
    urllib.request.urlretrieve(url, dest)
    if expected_sha256:
        actual = sha256_file(dest)
        if actual != expected_sha256:
            raise RuntimeError(
                f"Checksum mismatch for {url}\n"
                f"  expected {expected_sha256}\n"
                f"  got      {actual}"
            )
