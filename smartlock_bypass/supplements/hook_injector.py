def get_hook_code():
    """
    This function returns a raw Python string containing the hook code.
    The master.py patcher injects this string directly into the top-level
    of Smartlock's main module bytecode.
    """
    return r"""
import sys, types, os, tempfile, traceback

# ==============================================================================
# 1. RUNTIME CRASH LOGGING
# PyInstaller apps usually hide the console. If the injected hook or the app
# crashes, the error disappears. We redirect stderr to a line-buffered file
# and install a custom excepthook to force-write tracebacks immediately.
# ==============================================================================
try:
    crash_log = os.path.join(tempfile.gettempdir(), 'smartlock_crash.log')
    # buffering=1 means line-buffered. encoding='utf-8' prevents char errors.
    sys.stderr = open(crash_log, 'w', buffering=1, encoding='utf-8')
except Exception:
    pass

def _excepthook(type, value, tb):
    try:
        traceback.print_exception(type, value, tb, file=sys.stderr)
        sys.stderr.flush() # Force flush immediately on crash
    except:
        pass
    sys.__excepthook__(type, value, tb)
sys.excepthook = _excepthook

# ==============================================================================
# 2. WIN32 API SPOOFING
# Smartlock uses win32gui to detect if it is the foreground window and to
# hide other desktop windows. We inject fake modules into sys.modules so
# these calls do nothing, preventing the app from locking the desktop.
# ==============================================================================
if 'win32gui' in sys.modules:
    _wg = sys.modules['win32gui']
else:
    _wg = types.ModuleType('win32gui')
    sys.modules['win32gui'] = _wg
_wg.GetForegroundWindow = lambda: 0 # Always return a fake handle
_wg.ShowWindow = lambda *a, **b: None # Swallow window hide/show commands

if 'win32con' in sys.modules:
    _wc = sys.modules['win32con']
else:
    _wc = types.ModuleType('win32con')
    sys.modules['win32con'] = _wc
_wc.SW_HIDE = 0

# ==============================================================================
# 3. SELENIUM KIOSK MODE BYPASS
# Smartlock attempts to launch Chrome in '--kiosk' mode, locking the screen.
# We monkey-patch the Options object to silently drop '--kiosk' arguments.
# ==============================================================================
from selenium.webdriver.chrome.options import Options as _Opt
_orig_add_arg = _Opt.add_argument
def _safe_add_arg(self, arg):
    if 'kiosk' in str(arg).lower():
        return # Block kiosk mode
    return _orig_add_arg(self, arg)
_Opt.add_argument = _safe_add_arg

from selenium.webdriver import Chrome as _Chrome
# Prevent the app from forcing fullscreen via Selenium commands
_Chrome.fullscreen_window = lambda self: None

# ==============================================================================
# 4. JAVASCRIPT PAYLOAD INJECTION (The Core Browser Bypass)
# We patch Chrome.get(). Before navigating to any URL, we use CDP
# (Chrome DevTools Protocol) to inject a JS script into the page context.
# This runs before the webpage's own scripts, allowing us to override protections.
# ==============================================================================
_orig_get = _Chrome.get
def _safe_get(self, url):
    try:
        self.execute_cdp_cmd('Page.addScriptToEvaluateOnNewDocument', {
            'source': '''
                // --- Block forced closures and anti-cheat alerts ---
                window.close = function() { console.log('[Bypass] window.close blocked!'); };
                const _origAlert = window.alert;
                window.alert = function(msg) {
                    if (typeof msg === 'string' && (msg.includes('DEVTOOLS detected') || msg.includes('Fraudulent activity detected'))) {
                        console.log('[Bypass] Blocked detection alert:', msg);
                        return; // Suppress anti-cheat alert popups
                    }
                    return _origAlert.apply(this, arguments);
                };
                window.logout = function() { console.log('[Bypass] logout() blocked'); };
                
                // --- Blind visibility checks ---
                // The app checks if the tab is hidden (user switching tabs). We force it to visible.
                Object.defineProperty(document, 'hidden', { value: false, writable: false, configurable: true });
                Object.defineProperty(document, 'visibilityState', { value: 'visible', writable: false, configurable: true });
                document.addEventListener('visibilitychange', function(e) { e.stopImmediatePropagation(); }, true);

                // --- Spoof navigation type ---
                // Some sites check performance entries to see if the page was refreshed.
                // We proxy the entries to always return type: 'navigate' so refreshes are ignored.
                if (window.performance && performance.getEntriesByType) {
                    const origGetEntries = performance.getEntriesByType.bind(performance);
                    performance.getEntriesByType = function(type) {
                        const entries = origGetEntries(type);
                        if (type === 'navigation' && entries.length > 0) {
                            return [new Proxy(entries[0], {
                                get(target, prop, receiver) {
                                    if (prop === 'type') return 'navigate';
                                    // CRITICAL FIX: Use Reflect.get to preserve 'this' binding.
                                    const v = Reflect.get(target, prop, receiver);
                                    return typeof v === 'function' ? v.bind(target) : v;
                                }
                            })];
                        }
                        return entries;
                    };
                }

                // --- Fake WebSocket (Authentication Bypass) ---
                // The app connects to a local WS server for auth. We fake the server response.
                window.WebSocket = class {
                    constructor(url) {
                        this.url = url; this.readyState = 1; this._listeners = {};
                        // Simulate connection open
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
                                    // CRITICAL FIX: Removed hardcoded meme hash. Use actual token.
                                    if (typeof window.SHA256 === 'function' && typeof serverToken !== 'undefined') {
                                        const hashToSend = window.SHA256(serverToken);
                                        const fakeSuccess = { "type": "Authentication", "token_hash": hashToSend, "userid": parsed.userid || "12345" };
                                        const msgEvent = new MessageEvent('message', { data: JSON.stringify(fakeSuccess) });
                                        if (this.onmessage) this.onmessage(msgEvent);
                                        if (this._listeners['message']) this._listeners['message'].forEach(l => l(msgEvent));
                                    } else {
                                        console.error('[Bypass] serverToken or SHA256 missing. Hard failing.');
                                    }
                                }, 50);
                            }
                        } catch (e) {}
                    }
                    close() {}
                    addEventListener(type, listener) { if (!this._listeners[type]) this._listeners[type] = []; this._listeners[type].push(listener); }
                    removeEventListener() {}
                };

                // --- Gut webcam and AI proctoring completely ---
                window.videoload = function() {};
                window.loadModels = function() { isModelsLoaded = true; return Promise.resolve(); };
                
                // =======================================================
                // FIXED PHOTO CONFIGURATION
                // We bypass the webcam entirely. We send a fixed base64 image
                // to the server so human auditors see a valid photo.
                // Paste your raw base64 string between the quotes below.
                // Do NOT include the "data:image/jpeg;base64," prefix!
                // =======================================================
                const USER_PHOTO_B64 = "PASTE_YOUR_BASE64_HERE";
                const FIXED_PHOTO = "data:image/jpeg;base64," + USER_PHOTO_B64;
                
                // Replace the window.aware object with a stub that instantly passes
                // all liveness checks and returns our fixed photo.
                const fakeAware = {
                    start: async function(base64Image) { 
                        return { isMatch: true, message: 'Validation is in process, Please Wait ............', nextExp: '', photo: FIXED_PHOTO }; 
                    },
                    match: async function() { 
                        return { isMatch: true, message: 'Validation is in process, Please Wait ............', nextExp: '', photo: FIXED_PHOTO }; 
                    },
                    reStart: async function() { return; },
                    stop: function() {}, clearCanvas: function() {},
                    checkPhotoResolution: async function() { return true; }
                };
                Object.defineProperty(window, 'aware', { value: fakeAware, writable: false, configurable: false });

                // --- DISABLE ALL PROTECTIONS (Right-click, Copy, Paste, Shortcuts) ---
                document.oncontextmenu = null;
                document.ondragstart = null;
                document.onselectstart = null;
                document.onkeydown = null;
                document.onkeypress = null;
                document.onkeyup = null;
                
                // Intercept and neutralize protection events at the capture phase
                document.addEventListener('contextmenu', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('selectstart', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('copy', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('cut', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('paste', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('keydown', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('keyup', function(e) { e.stopImmediatePropagation(); }, true);
                document.addEventListener('keypress', function(e) { e.stopImmediatePropagation(); }, true);
                
                // Wait for jQuery to load, then unbind its restrictions
                function unbindJQ() {
                    if (typeof jQuery !== 'undefined') {
                        jQuery(document).off('contextmenu');
                        jQuery(document).off('cut copy paste');
                        jQuery('.form-text').off('cut copy paste contextmenu');
                        console.log('[Bypass] jQuery copy/paste protections unbound.');
                    } else {
                        setTimeout(unbindJQ, 100);
                    }
                }
                unbindJQ();

                // --- EXAM ANSWER HIGHLIGHTER (No Auto-Submit) ---
                // This interval runs constantly to catch new questions loading via AJAX.
                function setupExamInterceptor() {
                    var form = document.StallExam || document.getElementById('examform');
                    // Only run once per form instance to avoid spamming
                    if (!form || form.dataset.bypassed === 'true') return;
                    form.dataset.bypassed = 'true'; // Mark this question as processed

                    var originalSubmit = form.submit;
                    // 1. Block auto-submit completely so the timer doesn't skip the question
                    form.submit = function() { console.log('[Bypass] Auto-submit blocked. Waiting for user click.'); };

                    // 2. Trigger the website's local evaluation/coloring logic
                    if (typeof show === 'function') show(0);

                    // 3. Find which label the website colored Green (Correct Answer)
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

                    // 4. Re-enable radio buttons so the user can manually click them
                    for (var i = 1; i <= 4; i++) {
                        var radio = document.getElementById('radio' + i + '' + i);
                        if (radio) { radio.disabled = false; radio.style.visibility = 'visible'; }
                    }
                    if (form.confirm) form.confirm.disabled = false;

                    // 5. External Highlighter & Manual Submit Restore
                    if (correctIndex !== -1) {
                        var correctLab = document.getElementById('lab' + correctIndex);
                        var correctRadio = document.getElementById('radio' + correctIndex + '' + correctIndex);

                        // Apply a glowing border to the correct label
                        if (correctLab) {
                            correctLab.style.border = '5px solid #00ff00';
                            correctLab.style.borderRadius = '10px';
                            correctLab.style.boxShadow = '0 0 15px #00ff00';
                            correctLab.style.fontWeight = 'bold';
                            correctLab.style.color = '#00cc00';
                        }

                        // Add a big floating banner so you can't miss it
                        var banner = document.createElement('div');
                        banner.textContent = '✓ CORRECT ANSWER: OPTION ' + correctIndex;
                        banner.style.cssText = 'position:fixed; top:20px; right:20px; background:#00cc00; color:white; padding:15px; font-size:20px; font-weight:bold; z-index:999999; border-radius:10px; box-shadow:0 0 10px black;';
                        document.body.appendChild(banner);

                        // Restore submit ONLY when the user interacts with the correct answer
                        if (correctRadio) {
                            correctRadio.addEventListener('click', function() {
                                var selectedInput = document.getElementById("selected_answer");
                                if (selectedInput) selectedInput.value = correctRadio.value;
                                form.submit = originalSubmit; // Restore submit
                                if (banner) banner.remove(); // Remove banner
                            });
                        }
                        
                        // Also restore submit if the user clicks the confirm button manually
                        if (form.confirm) {
                            form.confirm.addEventListener('click', function() {
                                form.submit = originalSubmit;
                                if (banner) banner.remove();
                            });
                        }
                    } else {
                        // If no answer was found, restore submit so the user isn't stuck
                        form.submit = originalSubmit;
                    }
                }

                // Check continuously to catch new questions loading via AJAX
                setInterval(setupExamInterceptor, 500);
            '''
        })
    except Exception:
        pass
    
    # CRITICAL FIX: Stop swallowing navigation errors. If the network fails,
    # let the exception propagate so _patched_open_browser catches it cleanly.
    _orig_get(self, url)

_Chrome.get = _safe_get

# ==============================================================================
# 5. PYTHON CONTROLLER BYPASS
# Smartlock's controller.py does hardware checks, network locks, and VM detection.
# We replace all these functions with harmless lambdas.
# ==============================================================================
import controller as _c
_c.is_vm = lambda: False
_c.is_multiple_display = lambda: False
_c.is_wifi_available = lambda: False
_c.is_multiple_interface = lambda: False
_c.lock_keyboard_keys = lambda: None
_c.initiate_lock = lambda: None
_c.hide_taskbar = lambda: None
_c.start_firewall = lambda: None
_c.disconnect_display = lambda: None
_c.touchpad_gesture = lambda *a: None
_c.disable_services = lambda: None

# Spoof hardware identifiers so the monitoring loop never detects changes
_c.get_usb_list = lambda: []
_c.get_mac = lambda: '00:00:00:00:00:00'
_c.get_system_ip = lambda: '127.0.0.1'
_c.get_system_ipv6 = lambda: '::1'

# Patch the browser launcher to handle network failures gracefully
def _patched_open_browser(driver):
    try:
        _c.driver_g = driver
        _c.lock_keyboard_keys()
        url = _c.read_config_value('ExamUrl')
        driver.get(url)
    except Exception as url_ex:
        _c.messagebox.showerror('Error', 'Cannot connect to the exam url')
        driver.quit()
        _c.root_g.destroy()
        _c.generate_error_log(url_ex, '5')
_c.open_browser = _patched_open_browser

# ==============================================================================
# 6. REGISTRY EDIT BYPASS
# Prevent the app from writing to the Windows Registry (blocking task manager, etc)
# ==============================================================================
import registry_edit
registry_edit.Regedit.reg_edit_start = lambda self: True
registry_edit.Regedit.reg_edit_end = lambda self: True
"""
