# Sarathi-LL-Bypass
A modular framework for dynamically extracting, patching, and repacking Smartlock.exe. Features automated bytecode hook injection, Selenium CDP payload delivery, and mathematically sound archive reconstruction.

This project demonstrates techniques for:

- Extracting PyInstaller executables
- Modifying Python bytecode (`.pyc`)
- Injecting runtime hooks
- Repacking executables while preserving archive integrity
- Dynamic module discovery
- Automated deployment and rollback

> **Disclaimer**
>
> This repository is intended solely for educational purposes, reverse engineering research, and understanding Python bytecode manipulation. Ensure you have authorization before modifying or analyzing software that you do not own.

---

## Features

- Automatic Smartlock installation detection
- Dynamic discovery of the application's main Python module
- Recursive Python bytecode patching
- Runtime hook injection
- Automatic PyInstaller archive reconstruction
- Executable backup and restoration
- Version-aware `.pyc` handling
- Dynamic payload loading
- Automatic cleanup after deployment

---

## Repository Structure

```
.
├── master.py                  # Main patching utility
├── smartlock_supplement.py    # Runtime hook source
└── README.md
```

---

## How It Works

```
Original Smartlock.exe
        │
        ▼
Extract PyInstaller archive
        │
        ▼
Locate target Python module
        │
        ▼
Patch Python bytecode
        │
        ▼
Inject runtime hook
        │
        ▼
Rebuild executable
        │
        ▼
Deploy patched executable
```

---

## Components

### master.py

Responsible for the complete patching workflow.

Main responsibilities include:

- locating the Smartlock installation
- downloading required helper scripts
- extracting the executable
- identifying the target Python module
- injecting runtime code
- rebuilding the executable
- deployment
- rollback

---

### smartlock_supplement.py

Provides the runtime hook that is compiled and injected into the target application.

The hook demonstrates runtime monkey-patching techniques such as:

- module replacement
- function overriding
- browser automation customization
- JavaScript injection
- application initialization hooks

---

## Technical Overview

### Dynamic Module Detection

Instead of assuming a fixed module name, the patcher scans extracted bytecode to locate the application's entry module.

---

### Recursive Bytecode Patching

Every nested Python code object is traversed recursively before rebuilding the modified module.

---

### Runtime Hook Injection

The supplement script is compiled at runtime and inserted at the beginning of the target module's execution.

---

### PyInstaller Archive Repacking

The project rebuilds the executable by:

- parsing the archive cookie
- rebuilding the table of contents
- replacing the modified module
- recalculating offsets
- writing a new executable

---

## Requirements

- Python 3.11
- Windows
- Administrator privileges
- `bytecode`

Install dependencies:

```bash
pip install bytecode
```

---

## Usage

Run:

```bash
python master.py
```

The program will automatically:

1. Locate Smartlock
2. Extract the executable
3. Patch the target module
4. Rebuild the executable
5. Deploy the patched version

---

## Restore Original Version

The original executable is automatically backed up before deployment.

To restore:

```text
Menu → Revert to Original
```

---

## Project Highlights

This project demonstrates several advanced reverse engineering concepts:

- Python bytecode editing
- Runtime monkey patching
- Executable reconstruction
- Dynamic code injection
- PyInstaller internals
- Binary archive parsing
- Automated deployment

---

## Limitations

- Windows only
- Designed for PyInstaller-based applications
- Requires the correct Python bytecode version
- Administrator privileges required for deployment

---
# Disclaimer

This repository is provided **"AS IS"**, without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, non-infringement, or uninterrupted operation.

This project is intended solely for educational, research, software analysis, reverse engineering, interoperability, and security research purposes. Users are solely responsible for ensuring that their use of this software complies with all applicable laws, regulations, contractual obligations, software licenses, and terms of service.

The author does not encourage or endorse unauthorized access, circumvention of security measures, academic dishonesty, or any unlawful activity.

---

## License

This repository is provided for educational and research purposes only.

Users are responsible for ensuring they comply with all applicable laws, software licenses, and terms of service when using or modifying software.
