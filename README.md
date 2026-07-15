# Sarathi-LL-Bypass
A modular framework for dynamically extracting, patching, and repacking Smartlock.exe. Features automated bytecode hook injection, Selenium CDP payload delivery, and mathematically sound archive reconstruction.

**WARNING:** This documentation and the associated scripts are strictly for **educational purposes and reverse-engineering research**.

## 📌 The Blueprint (What is this?)

This script automates the full lifecycle of reverse-engineering a compiled Python executable (specifically a lockdown browser). It doesn't just block a few functions; it fundamentally alters the DNA of the application at runtime. 

The execution flow is as follows:
1. **Reconnaissance & Extraction:** Locates the target executable and unpacks the PyInstaller archive using `pyinstxtractor`.
2. **Dynamic Resolution:** Scans the extracted `.pyc` (compiled Python bytecode) files to find the main entry point by looking for specific byte signatures (e.g., `--kiosk`).
3. **Bytecode Patching (The Scalpel):** Uses Python's `marshal` and `bytecode` libraries to decompile the `.pyc`, strip restrictions, and inject a massive hook payload directly into the AST (Abstract Syntax Tree) / bytecode.
4. **Binary Repacking:** Recalculates the executable's Table of Contents (TOC) and repacks the injected `.pyc` back into a standalone `.exe` without needing the original source code or PyInstaller.
5. **Runtime Evasion (CDP Injection):** Injects JavaScript into the embedded Chromium browser via Chrome DevTools Protocol (CDP) to spoof webcams, bypass VM checks, unbind anti-cheat event listeners, and highlight correct answers.

## ⚙️ Prerequisites & Setup


1. **Exact Python Version Match:** You MUST run this script with the exact same Python minor version that compiled `Smartlock.exe`.  **USE PYTHON 3.11**
2. **Required Libraries:**
   ```bash
   pip install bytecode
   ```
3. **Elevated Privileges:** This script patches files in `C:\Program Files`. It forces UAC elevation via `ctypes.windll.shell32.ShellExecuteW`. Do not run it in a restricted shell.

---

## 🚀 Execution Instructions

1. Run the script as Administrator:
   ```bash
   python master.py
   ```
2. **The Menu:**
   * **Option 1 (Patch and Deploy):** Automatically clones the target, downloads `pyinstxtractor`, rips the binary apart, injects the JS/Python hooks, repacks it, and deploys it to the installation directory.
   * **Option 2 (Revert):** Restores the `.bak` file created during patching. Always have a contingency plan.
   * **Option 3:** Exit.


