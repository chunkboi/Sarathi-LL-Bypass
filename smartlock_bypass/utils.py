"""
Utility functions for file operations, downloads, and hashing.
"""

import hashlib
import urllib.request


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
