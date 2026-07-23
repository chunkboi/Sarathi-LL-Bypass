#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SmartLock Supplement - Ultra Optimized v3.0
Runtime hook injection engine with comprehensive bypass capabilities
"""
from __future__ import annotations
import sys, types, os, tempfile, traceback, json, time, functools, weakref
from typing import Dict, List, Optional, Any, Callable, Tuple
from dataclasses import dataclass, field
from abc import ABC, abstractmethod

# =============================================================================
# DYNAMIC CONFIGURATION
# =============================================================================
@dataclass(frozen=True, slots=True)
class HookConfig:
    """Runtime configuration for hook behavior"""
    temp_dir: str = field(default_factory=lambda: tempfile.gettempdir())
    crash_log_name: str = 'smartlock_crash.log'
    debug_mode: bool = field(default_factory=lambda: os.getenv('HOOK_DEBUG', '0') == '1')
    photo_b64_env: str = 'USER_PHOTO_B64'
    
    @property
    def crash_log_path(self) -> str:
        return os.path.join(self.temp_dir, self.crash_log_name)

HOOK_CONFIG = HookConfig()

# =============================================================================
# LOGGING UTILITIES
# =============================================================================
class HookLogger:
    """Lightweight logger for hook operations"""
    __slots__ = ('_prefix',)
    def __init__(self, prefix: str = '[Bypass]'):
        self._prefix = prefix
    
    def _write(self, level: str, msg: str):
        print(f"{self._prefix} {level}: {msg}", file=sys.stderr)
    
    def info(self, msg: str): self._write('INFO', msg)
    def warn(self, msg: str): self._write('WARN', msg)
    def error(self, msg: str): self._write('ERROR', msg)
    def debug(self, msg: str):
        if HOOK_CONFIG.debug_mode:
            self._write('DEBUG', msg)

logger = HookLogger()

# =============================================================================
# CRASH HANDLING SYSTEM
# =============================================================================
class CrashHandler:
    """Runtime crash logging with automatic stderr redirection"""
    
    _instance: Optional['CrashHandler'] = None
    _original_stderr = None
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance
    
    def install(self) -> None:
        """Install crash handler with stderr capture"""
        try:
            self._original_stderr = sys.stderr
            crash_fh = open(HOOK_CONFIG.crash_log_path, 'w', buffering=1, encoding='utf-8')
            sys.stderr = crash_fh
            
            def excepthook(exc_type, exc_value, exc_tb):
                try:
                    traceback.print_exception(exc_type, exc_value, exc_tb, file=sys.stderr)
                    sys.stderr.flush()
                except Exception:
                    pass
                if sys.__excepthook__:
                    sys.__excepthook__(exc_type, exc_value, exc_tb)
            
            sys.excepthook = excepthook
            logger.info("Crash handler installed")
        except Exception as e:
            logger.error(f"Failed to install crash handler: {e}")

# =============================================================================
# MODULE SPOOFING ENGINE
# =============================================================================
class ModuleSpoof:
    """Dynamic module spoofing with lazy instantiation"""
    
    _spoofed_modules: Dict[str, types.ModuleType] = {}
    _original_modules: Dict[str, types.ModuleType] = {}
    
    @classmethod
    def spoof(cls, name: str, attrs: Dict[str, Any]) -> types.ModuleType:
        """Create or replace a module with spoofed attributes"""
        if name in sys.modules:
            cls._original_modules[name] = sys.modules[name]
        
        module = types.ModuleType(name)
        for attr_name, attr_value in attrs.items():
            setattr(module, attr_name, attr_value)
        
        sys.modules[name] = module
        cls._spoofed_modules[name] = module
        logger.debug(f"Spoofed module: {name}")
        return module
    
    @classmethod
    def patch(cls, name: str, attrs: Dict[str, Any]) -> None:
        """Patch existing module with new attributes"""
        if name not in sys.modules:
            sys.modules[name] = types.ModuleType(name)
        
        module = sys.modules[name]
        for attr_name, attr_value in attrs.items():
            setattr(module, attr_name, attr_value)
        
        logger.debug(f"Patched module: {name}")
    
    @classmethod
    def restore(cls, name: str) -> bool:
        """Restore original module if it existed"""
        if name in cls._original_modules:
            sys.modules[name] = cls._original_modules[name]
            del cls._spoofed_modules[name]
            return True
        return False
    
    @classmethod
    def cleanup(cls) -> None:
        """Remove all spoofed modules"""
        for name in list(cls._spoofed_modules.keys()):
            cls.restore(name)

# =============================================================================
# FUNCTION MONKEY PATCHING
# =============================================================================
class MonkeyPatcher:
    """Safe monkey patching with rollback support"""
    
    _patches: List[Tuple[Any, str, Any]] = []
    
    @classmethod
    def patch(cls, obj: Any, attr: str, replacement: Any) -> None:
        """Replace attribute with backup for rollback"""
        if hasattr(obj, attr):
            original = getattr(obj, attr)
            cls._patches.append((obj, attr, original))
        
        setattr(obj, attr, replacement)
        logger.debug(f"Patched {type(obj).__name__}.{attr}")
    
    @classmethod
    def patch_method(cls, cls_obj: type, method_name: str, replacement: Callable) -> None:
        """Patch class method"""
        cls.patch(cls_obj, method_name, replacement)
    
    @classmethod
    def rollback(cls) -> None:
        """Restore all patched attributes"""
        for obj, attr, original in reversed(cls._patches):
            setattr(obj, attr, original)
        cls._patches.clear()
        logger.info("All patches rolled back")

# =============================================================================
# JAVASCRIPT PAYLOAD GENERATOR
# =============================================================================
class JSPayloadGenerator:
    """Dynamic JavaScript payload generation with composition"""
    
    def __init__(self):
        self._fragments: List[str] = []
        self._variables: Dict[str, str] = {}
    
    def add_variable(self, name: str, value: str) -> 'JSPayloadGenerator':
        """Add JavaScript variable"""
        self._variables[name] = value
        return self
    
    def add_fragment(self, code: str) -> 'JSPayloadGenerator':
        """Add code fragment"""
        self._fragments.append(code.strip())
        return self
    
    def add_protection_bypass(self) -> 'JSPayloadGenerator':
        """Add window protection bypasses"""
        return self.add_fragment("""
            window.close = function() { console.log('[Bypass] window.close blocked!'); };
            const _origAlert = window.alert;
            window.alert = function(msg) {
                if (typeof msg === 'string' && (msg.includes('DEVTOOLS detected') || msg.includes('Fraudulent activity detected'))) {
                    console.log('[Bypass] Blocked detection alert:', msg);
                    return;
                }
                return _origAlert.apply(this, arguments);
            };
            window.logout = function() { console.log('[Bypass] logout() blocked'); };
        """)
    
    def add_visibility_spoof(self) -> 'JSPayloadGenerator':
        """Add visibility state spoofing"""
        return self.add_fragment("""
            Object.defineProperty(document, 'hidden', { value: false, writable: false, configurable: true });
            Object.defineProperty(document, 'visibilityState', { value: 'visible', writable: false, configurable: true });
            document.addEventListener('visibilitychange', function(e) { e.stopImmediatePropagation(); }, true);
        """)
    
    def add_navigation_spoof(self) -> 'JSPayloadGenerator':
        """Add navigation type spoofing"""
        return self.add_fragment("""
            if (window.performance && performance.getEntriesByType) {
                const origGetEntries = performance.getEntriesByType.bind(performance);
                performance.getEntriesByType = function(type) {
                    const entries = origGetEntries(type);
                    if (type === 'navigation' && entries.length > 0) {
                        return [new Proxy(entries[0], {
                            get(target, prop, receiver) {
                                if (prop === 'type') return 'navigate';
                                const v = Reflect.get(target, prop, receiver);
                                return typeof v === 'function' ? v.bind(target) : v;
                            }
                        })];
                    }
                    return entries;
                };
            }
        """)
    
    def add_websocket_fake(self) -> 'JSPayloadGenerator':
        """Add fake WebSocket implementation"""
        return self.add_fragment("""
            window.WebSocket = class {
                constructor(url) {
                    this.url = url; this.readyState = 1; this._listeners = {};
                    setTimeout(() => {
                        if (this.onopen) this.onopen(new Event('open'));
                        if (this._listeners['open']) this._listeners['open'].forEach(l => l(new Event('open')));
                    }, 10);
                }
                send(data) {
                    try {
                        const parsed = JSON.parse(data);
                        if (parsed.type === 'Authentication') {
                            setTimeout(() => {
                                if (typeof window.SHA256 === 'function' && typeof serverToken !== 'undefined') {
                                    const hashToSend = window.SHA256(serverToken);
                                    const fakeSuccess = { "type": "Authentication", "token_hash": hashToSend, "userid": parsed.userid || "12345" };
                                    const msgEvent = new MessageEvent('message', { data: JSON.stringify(fakeSuccess) });
                                    if (this.onmessage) this.onmessage(msgEvent);
                                    if (this._listeners['message']) this._listeners['message'].forEach(l => l(msgEvent));
                                } else {
                                    console.error('[Bypass] serverToken or SHA256 missing.');
                                }
                            }, 50);
                        }
                    } catch (e) {}
                }
                close() {}
                addEventListener(type, listener) { if (!this._listeners[type]) this._listeners[type] = []; this._listeners[type].push(listener); }
                removeEventListener() {}
            };
        """)
    
    def add_camera_bypass(self) -> 'JSPayloadGenerator':
        """Add camera/AI proctoring bypass"""
        return self.add_fragment("""
            window.videoload = function() {};
            window.loadModels = function() { isModelsLoaded = true; return Promise.resolve(); };
        """)
    
    def add_photo_stub(self, photo_b64: Optional[str] = None) -> 'JSPayloadGenerator':
        """Add photo validation stub"""
        photo_value = photo_b64 or os.getenv(HOOK_CONFIG.photo_b64_env, "PLACEHOLDER_BASE64")
        return self.add_variable('USER_PHOTO_B64', f'"{photo_value}"').add_fragment("""
            const FIXED_PHOTO = "data:image/jpeg;base64," + USER_PHOTO_B64;
            const fakeAware = {
                start: async function(base64Image) {
                    return { isMatch: true, message: 'Validation in process...', nextExp: '', photo: FIXED_PHOTO };
                },
                match: async function() {
                    return { isMatch: true, message: 'Validation in process...', nextExp: '', photo: FIXED_PHOTO };
                },
                reStart: async function() { return; },
                stop: function() {}, clearCanvas: function() {},
                checkPhotoResolution: async function() { return true; }
            };
            Object.defineProperty(window, 'aware', { value: fakeAware, writable: false, configurable: false });
        """)
    
    def add_input_unlock(self) -> 'JSPayloadGenerator':
        """Add input restriction removal"""
        return self.add_fragment("""
            document.oncontextmenu = null;
            document.ondragstart = null;
            document.onselectstart = null;
            document.onkeydown = null;
            document.onkeypress = null;
            document.onkeyup = null;
            document.addEventListener('contextmenu', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('selectstart', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('copy', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('cut', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('paste', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('keydown', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('keyup', function(e) { e.stopImmediatePropagation(); }, true);
            document.addEventListener('keypress', function(e) { e.stopImmediatePropagation(); }, true);
        """)
    
    def add_jquery_unlock(self) -> 'JSPayloadGenerator':
        """Add jQuery event unbinding"""
        return self.add_fragment("""
            function unbindJQ() {
                if (typeof jQuery !== 'undefined') {
                    jQuery(document).off('contextmenu');
                    jQuery(document).off('cut copy paste');
                    jQuery('.form-text').off('cut copy paste contextmenu');
                    console.log('[Bypass] jQuery protections unbound.');
                } else {
                    setTimeout(unbindJQ, 100);
                }
            }
            unbindJQ();
        """)
    
    def add_exam_highlighter(self) -> 'JSPayloadGenerator':
        """Add exam answer highlighter"""
        return self.add_fragment("""
            function setupExamInterceptor() {
                var form = document.StallExam || document.getElementById('examform');
                if (!form || form.dataset.bypassed === 'true') return;
                form.dataset.bypassed = 'true';
                
                var originalSubmit = form.submit;
                form.submit = function() { console.log('[Bypass] Auto-submit blocked.'); };
                
                if (typeof show === 'function') show(0);
                
                var correctIndex = -1;
                for (var i = 1; i <= 4; i++) {
                    var lab = document.getElementById('lab' + i);
                    if (lab) {
                        var bg = (lab.style.background || "").toLowerCase();
                        if (bg.includes('green') || bg.includes('#8ac007') || bg.includes('rgb(138, 192, 7)')) {
                            correctIndex = i; break;
                        }
                    }
                }
                
                for (var i = 1; i <= 4; i++) {
                    var radio = document.getElementById('radio' + i + '' + i);
                    if (radio) { radio.disabled = false; radio.style.visibility = 'visible'; }
                }
                if (form.confirm) form.confirm.disabled = false;
                
                if (correctIndex !== -1) {
                    var correctLab = document.getElementById('lab' + correctIndex);
                    var correctRadio = document.getElementById('radio' + correctIndex + '' + correctIndex);
                    
                    if (correctLab) {
                        correctLab.style.border = '5px solid #00ff00';
                        correctLab.style.borderRadius = '10px';
                        correctLab.style.boxShadow = '0 0 15px #00ff00';
                        correctLab.style.fontWeight = 'bold';
                        correctLab.style.color = '#00cc00';
                    }
                    
                    var banner = document.createElement('div');
                    banner.textContent = '✓ CORRECT ANSWER: OPTION ' + correctIndex;
                    banner.style.cssText = 'position:fixed; top:20px; right:20px; background:#00cc00; color:white; padding:15px; font-size:20px; font-weight:bold; z-index:999999; border-radius:10px; box-shadow:0 0 10px black;';
                    document.body.appendChild(banner);
                    
                    if (correctRadio) {
                        correctRadio.addEventListener('click', function() {
                            var selectedInput = document.getElementById("selected_answer");
                            if (selectedInput) selectedInput.value = correctRadio.value;
                            form.submit = originalSubmit;
                            if (banner) banner.remove();
                        });
                    }
                    
                    if (form.confirm) {
                        form.confirm.addEventListener('click', function() {
                            form.submit = originalSubmit;
                            if (banner) banner.remove();
                        });
                    }
                } else {
                    form.submit = originalSubmit;
                }
            }
            setInterval(setupExamInterceptor, 500);
        """)
    
    def build(self) -> str:
        """Build complete JavaScript payload"""
        parts = []
        
        for name, value in self._variables.items():
            parts.append(f"var {name} = {value};")
        
        parts.extend(self._fragments)
        
        return '\n'.join(parts)

# =============================================================================
# SELENIUM PATCHING
# =============================================================================
class SeleniumPatcher:
    """Selenium WebDriver patching engine"""
    
    @staticmethod
    def patch_options_kiosk() -> None:
        """Patch Chrome options to block kiosk mode"""
        try:
            from selenium.webdriver.chrome.options import Options
            orig_add_arg = Options.add_argument
            
            @functools.wraps(orig_add_arg)
            def safe_add_arg(self, arg):
                if 'kiosk' in str(arg).lower():
                    logger.debug(f"Blocked kiosk argument: {arg}")
                    return
                return orig_add_arg(self, arg)
            
            Options.add_argument = safe_add_arg
            logger.info("Kiosk mode blocking enabled")
        except ImportError:
            logger.warn("Selenium not available, skipping options patch")
    
    @staticmethod
    def patch_chrome_driver() -> None:
        """Patch Chrome driver with CDP injection"""
        try:
            from selenium.webdriver import Chrome
            
            generator = JSPayloadGenerator()
            js_payload = (generator
                .add_protection_bypass()
                .add_visibility_spoof()
                .add_navigation_spoof()
                .add_websocket_fake()
                .add_camera_bypass()
                .add_photo_stub()
                .add_input_unlock()
                .add_jquery_unlock()
                .add_exam_highlighter()
                .build())
            
            orig_get = Chrome.get
            
            @functools.wraps(orig_get)
            def safe_get(self, url):
                try:
                    self.execute_cdp_cmd('Page.addScriptToEvaluateOnNewDocument', {
                        'source': js_payload
                    })
                    logger.debug("JavaScript payload injected")
                except Exception as e:
                    logger.error(f"CDP injection failed: {e}")
                
                orig_get(self, url)
            
            Chrome.fullscreen_window = lambda self: None
            Chrome.get = safe_get
            logger.info("Chrome driver patched with CDP injection")
        except ImportError:
            logger.warn("Selenium Chrome not available, skipping driver patch")

# =============================================================================
# CONTROLLER PATCHING
# =============================================================================
class ControllerPatcher:
    """Python controller module patching"""
    
    FAKE_HARDWARE_ATTRS = {
        'is_vm': lambda: False,
        'is_multiple_display': lambda: False,
        'is_wifi_available': lambda: False,
        'is_multiple_interface': lambda: False,
        'lock_keyboard_keys': lambda: None,
        'initiate_lock': lambda: None,
        'hide_taskbar': lambda: None,
        'start_firewall': lambda: None,
        'disconnect_display': lambda: None,
        'touchpad_gesture': lambda *a: None,
        'disable_services': lambda: None,
        'get_usb_list': lambda: [],
        'get_mac': lambda: '00:00:00:00:00:00',
        'get_system_ip': lambda: '127.0.0.1',
        'get_system_ipv6': lambda: '::1',
    }
    
    @classmethod
    def patch_controller(cls) -> None:
        """Patch controller module"""
        try:
            import controller
            
            for attr_name, attr_value in cls.FAKE_HARDWARE_ATTRS.items():
                if hasattr(controller, attr_name):
                    MonkeyPatcher.patch(controller, attr_name, attr_value)
            
            cls._patch_open_browser(controller)
            logger.info("Controller module patched")
        except ImportError:
            logger.warn("Controller module not available")
    
    @staticmethod
    def _patch_open_browser(controller) -> None:
        """Patch browser opening with error handling"""
        orig_open = getattr(controller, 'open_browser', None)
        if not orig_open:
            return
        
        def patched_open_browser(driver):
            try:
                controller.driver_g = driver
                controller.lock_keyboard_keys()
                url = controller.read_config_value('ExamUrl')
                driver.get(url)
            except Exception as url_ex:
                if hasattr(controller, 'messagebox'):
                    controller.messagebox.showerror('Error', 'Cannot connect to the exam url')
                driver.quit()
                if hasattr(controller, 'root_g'):
                    controller.root_g.destroy()
                if hasattr(controller, 'generate_error_log'):
                    controller.generate_error_log(url_ex, '5')
        
        MonkeyPatcher.patch(controller, 'open_browser', patched_open_browser)

# =============================================================================
# REGISTRY PATCHING
# =============================================================================
class RegistryPatcher:
    """Windows registry edit prevention"""
    
    @staticmethod
    def patch_registry() -> None:
        """Patch registry editing functions"""
        try:
            import registry_edit
            
            if hasattr(registry_edit, 'Regedit'):
                MonkeyPatcher.patch(registry_edit.Regedit, 'reg_edit_start', lambda self: True)
                MonkeyPatcher.patch(registry_edit.Regedit, 'reg_edit_end', lambda self: True)
            
            logger.info("Registry editing patched")
        except ImportError:
            logger.warn("Registry module not available")

# =============================================================================
# MAIN HOOK ORCHESTRATOR
# =============================================================================
class HookOrchestrator:
    """Main orchestrator for all bypass hooks"""
    
    def __init__(self):
        self.crash_handler = CrashHandler()
        self.patches_applied = False
    
    def install_all(self) -> None:
        """Install all bypass hooks"""
        logger.info("Installing bypass hooks...")
        
        self.crash_handler.install()
        
        ModuleSpoof.patch('win32gui', {
            'GetForegroundWindow': lambda: 0,
            'ShowWindow': lambda *a, **b: None
        })
        
        ModuleSpoof.patch('win32con', {
            'SW_HIDE': 0
        })
        
        SeleniumPatcher.patch_options_kiosk()
        SeleniumPatcher.patch_chrome_driver()
        
        ControllerPatcher.patch_controller()
        
        RegistryPatcher.patch_registry()
        
        self.patches_applied = True
        logger.info("All bypass hooks installed successfully")
    
    def uninstall_all(self) -> None:
        """Uninstall all hooks and restore original state"""
        if not self.patches_applied:
            return
        
        logger.info("Uninstalling bypass hooks...")
        MonkeyPatcher.rollback()
        ModuleSpoof.cleanup()
        self.patches_applied = False
        logger.info("All hooks uninstalled")

# =============================================================================
# PUBLIC API
# =============================================================================
def get_hook_code() -> str:
    """
    Generate and return the complete hook code string.
    This function is called by the master patcher to inject hooks.
    """
    orchestrator = HookOrchestrator()
    
    hook_lines = [
        'import sys, types, os, tempfile, traceback, json, time, functools',
        '',
        '# Runtime configuration',
        f'TEMP_DIR = r"{tempfile.gettempdir()}"',
        f'CRASH_LOG = os.path.join(TEMP_DIR, "smartlock_crash.log")',
        '',
        '# Install crash handler',
        'try:',
        '    sys.stderr = open(CRASH_LOG, "w", buffering=1, encoding="utf-8")',
        'except: pass',
        '',
        'def _excepthook(t, v, tb):',
        '    try:',
        '        traceback.print_exception(t, v, tb, file=sys.stderr)',
        '        sys.stderr.flush()',
        '    except: pass',
        '    if sys.__excepthook__: sys.__excepthook__(t, v, tb)',
        'sys.excepthook = _excepthook',
        '',
        '# Spoof Windows APIs',
        'if "win32gui" not in sys.modules: sys.modules["win32gui"] = types.ModuleType("win32gui")',
        'sys.modules["win32gui"].GetForegroundWindow = lambda: 0',
        'sys.modules["win32gui"].ShowWindow = lambda *a, **b: None',
        '',
        'if "win32con" not in sys.modules: sys.modules["win32con"] = types.ModuleType("win32con")',
        'sys.modules["win32con"].SW_HIDE = 0',
        '',
    ]
    
    generator = JSPayloadGenerator()
    js_payload = (generator
        .add_protection_bypass()
        .add_visibility_spoof()
        .add_navigation_spoof()
        .add_websocket_fake()
        .add_camera_bypass()
        .add_photo_stub()
        .add_input_unlock()
        .add_jquery_unlock()
        .add_exam_highlighter()
        .build())
    
    hook_lines.append('# Selenium kiosk bypass')
    hook_lines.append('try:')
    hook_lines.append('    from selenium.webdriver.chrome.options import Options as _Opt')
    hook_lines.append('    _orig_add = _Opt.add_argument')
    hook_lines.append('    def _safe_add(self, arg):')
    hook_lines.append('        if "kiosk" in str(arg).lower(): return')
    hook_lines.append('        return _orig_add(self, arg)')
    hook_lines.append('    _Opt.add_argument = _safe_add')
    hook_lines.append('    from selenium.webdriver import Chrome as _Chrome')
    hook_lines.append('    _Chrome.fullscreen_window = lambda s: None')
    hook_lines.append(f'    _JS_PAYLOAD = """{js_payload}"""')
    hook_lines.append('    def _safe_get(self, url):')
    hook_lines.append('        try: self.execute_cdp_cmd("Page.addScriptToEvaluateOnNewDocument", {"source": _JS_PAYLOAD})')
    hook_lines.append('        except: pass')
    hook_lines.append('        return _orig_get(self, url)')
    hook_lines.append('    _orig_get = _Chrome.get')
    hook_lines.append('    _Chrome.get = _safe_get')
    hook_lines.append('except: pass')
    hook_lines.append('')
    hook_lines.append('# Controller bypass')
    hook_lines.append('try:')
    hook_lines.append('    import controller as _c')
    for attr, val in ControllerPatcher.FAKE_HARDWARE_ATTRS.items():
        hook_lines.append(f'    _c.{attr} = lambda: {val()}')
    hook_lines.append('    def _patched_open(driver):')
    hook_lines.append('        try:')
    hook_lines.append('            _c.driver_g = driver')
    hook_lines.append('            _c.lock_keyboard_keys()')
    hook_lines.append("            url = _c.read_config_value('ExamUrl')")
    hook_lines.append('            driver.get(url)')
    hook_lines.append('        except Exception as e:')
    hook_lines.append("            if hasattr(_c, 'messagebox'): _c.messagebox.showerror('Error', 'Cannot connect')")
    hook_lines.append('            driver.quit()')
    hook_lines.append('            if hasattr(_c, "root_g"): _c.root_g.destroy()')
    hook_lines.append('            if hasattr(_c, "generate_error_log"): _c.generate_error_log(e, "5")')
    hook_lines.append('    _c.open_browser = _patched_open')
    hook_lines.append('except: pass')
    hook_lines.append('')
    hook_lines.append('# Registry bypass')
    hook_lines.append('try:')
    hook_lines.append('    import registry_edit')
    hook_lines.append('    registry_edit.Regedit.reg_edit_start = lambda s: True')
    hook_lines.append('    registry_edit.Regedit.reg_edit_end = lambda s: True')
    hook_lines.append('except: pass')
    
    return '\n'.join(hook_lines)

if __name__ == "__main__":
    print(get_hook_code())
