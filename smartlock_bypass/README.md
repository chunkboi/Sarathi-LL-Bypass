# SmartLock Bypass - Modular Refactored Version

This is a refactored version of the SmartLock Bypass tool, split into smaller, maintainable modules.

## Original Files
- `master.py` (523 lines) - Original monolithic script
- `smartlock_supplement.py` (350 lines) - Hook code supplement

## New Modular Structure

### Main Package (`smartlock_bypass/`)
```
smartlock_bypass/
├── __init__.py           # Package metadata (12 lines)
├── config.py             # Configuration constants (24 lines)
├── utils.py              # Utility functions (38 lines)
├── resolver.py           # Path resolution (58 lines)
├── loader.py             # Supplement loader (59 lines)
├── patcher.py            # Bytecode patching (86 lines)
├── stripper.py           # PYC header stripping (35 lines)
├── repacker.py           # EXE repacking (146 lines)
├── deployer.py           # Deployment logic (103 lines)
├── admin.py              # UAC elevation & crash logging (66 lines)
├── main.py               # Entry point & menu (121 lines)
└── supplements/
    ├── __init__.py       # Subpackage init (10 lines)
    └── hook_injector.py  # Hook code (350 lines, copied from smartlock_supplement.py)
```

**Total: ~748 lines in core modules + 360 lines in supplements = 1,108 lines**

Compared to original: 873 lines → Better separation of concerns with minimal overhead.

## Benefits of Refactoring

1. **Separation of Concerns**: Each module has a single responsibility
2. **Easier Testing**: Individual components can be tested independently
3. **Better Documentation**: Docstrings explain each module's purpose
4. **Maintainability**: Changes to one component don't affect others
5. **Reusability**: Modules like `utils.py` and `config.py` can be reused

## Usage

```python
from smartlock_bypass.main import run

if __name__ == "__main__":
    run()
```

Or run as a module:
```bash
python -m smartlock_bypass.main
```

## Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| `config` | Constants and configuration values |
| `utils` | File hashing and download utilities |
| `resolver` | Finding SmartLock installation and target modules |
| `loader` | Loading and validating the supplement module |
| `patcher` | Bytecode manipulation and hook injection |
| `stripper` | Removing PYC headers for embedding |
| `repacker` | PyInstaller EXE parsing and rebuilding |
| `deployer` | Installation, backup, and restoration |
| `admin` | Windows UAC elevation and crash logging |
| `main` | Interactive menu and workflow orchestration |

## Original Files Preserved

The original `master.py` and `smartlock_supplement.py` files remain in the workspace root for reference.
