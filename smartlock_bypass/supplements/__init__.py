"""
Hook supplements for SmartLock Bypass.

This subpackage contains modular hook components that can be
injected into the target application.
"""

from .hook_injector import get_hook_code

__all__ = ['get_hook_code']
