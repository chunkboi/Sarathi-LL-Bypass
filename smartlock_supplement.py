def get_hook_code():
    return r"""
import sys, types, os, tempfile, traceback

# 1. Line-buffered crash log with explicit flush
try:
    crash_log = os.path.join(tempfile.gettempdir(), 'smartlock_crash.log')
    sys.stderr = open(crash_log, 'w', buffering=1, encoding='utf-8')
except Exception:
    pass

def _excepthook(type, value, tb):
    try:
        traceback.print_exception(type, value, tb, file=sys.stderr)
        sys.stderr.flush()
    except:
        pass
    sys.__excepthook__(type, value, tb)
sys.excepthook = _excepthook

if 'win32gui' in sys.modules:
    _wg = sys.modules['win32gui']
else:
    _wg = types.ModuleType('win32gui')
    sys.modules['win32gui'] = _wg
_wg.GetForegroundWindow = lambda: 0
_wg.ShowWindow = lambda *a, **b: None

if 'win32con' in sys.modules:
    _wc = sys.modules['win32con']
else:
    _wc = types.ModuleType('win32con')
    sys.modules['win32con'] = _wc
_wc.SW_HIDE = 0

from selenium.webdriver.chrome.options import Options as _Opt
_orig_add_arg = _Opt.add_argument
def _safe_add_arg(self, arg):
    if 'kiosk' in str(arg).lower():
        return
    return _orig_add_arg(self, arg)
_Opt.add_argument = _safe_add_arg

from selenium.webdriver import Chrome as _Chrome
_Chrome.fullscreen_window = lambda self: None

_orig_get = _Chrome.get
def _safe_get(self, url):
    try:
        self.execute_cdp_cmd('Page.addScriptToEvaluateOnNewDocument', {
            'source': '''
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
                
                // Blind visibility checks
                Object.defineProperty(document, 'hidden', { value: false, writable: false, configurable: true });
                Object.defineProperty(document, 'visibilityState', { value: 'visible', writable: false, configurable: true });
                document.addEventListener('visibilitychange', function(e) { e.stopImmediatePropagation(); }, true);

                // Spoof navigation type (Fixed Proxy using Reflect.get)
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

                // Fake WebSocket (Removed meme hash fallback)
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

                // Gut webcam and AI (Fixed base64 whitespace)
                window.videoload = function() {};
                window.loadModels = function() { isModelsLoaded = true; return Promise.resolve(); };
                const dummyPhoto = "data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAAQABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAr/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/8QAFAEBAAAAAAAAAAAAAAAAAAAAAP/EABQRAQAAAAAAAAAAAAAAAAAAAAD/2gAMAwEAAhEDEQA/AKgABf/Z";
                const fakeAware = {
                    start: function(base64Image) { return new Promise(function(resolve) { setTimeout(function() { resolve({ isMatch: true, message: 'Validation is in process...', nextExp: '', photo: base64Image || dummyPhoto }); }, 100); }); },
                    match: function() { return new Promise(function(resolve) { resolve({ isMatch: true, message: 'Validation is in process...', nextExp: '', photo: dummyPhoto }); }); },
                    reStart: function() { return new Promise(function(resolve) { resolve(); }); },
                    stop: function() {}, clearCanvas: function() {},
                    checkPhotoResolution: function() { return new Promise(function(resolve) { resolve(true); }); }
                };
                Object.defineProperty(window, 'aware', { value: fakeAware, writable: false, configurable: false });

                // DISABLE ALL PROTECTIONS
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

                function _highlightCorrectAnswer() {
                    var form = document.StallExam || document.getElementById('examform');
                    if (!form || !form.confirm) return;
                    if (form.confirm.disabled) return;
                    var originalSubmit = form.submit;
                    form.submit = function() {};
                    if (typeof show === 'function') show(0);
                    var correctIndex = -1;
                    for (var i = 1; i <= 4; i++) {
                        var lab = document.getElementById('lab' + i);
                        if (lab) {
                            var bg = lab.style.background.toLowerCase();
                            if (bg.includes('green') || bg.includes('#8ac007') || bg.includes('rgb(138, 192, 7)')) {
                                correctIndex = i; break;
                            }
                        }
                    }
                    form.submit = originalSubmit;
                    for (var i = 1; i <= 4; i++) {
                        var radio = document.getElementById('radio' + i + '' + i);
                        if (radio) { radio.disabled = false; radio.style.visibility = 'visible'; }
                    }
                    if (form.confirm) form.confirm.disabled = false;
                    if (correctIndex !== -1) {
                        var correctLab = document.getElementById('lab' + correctIndex);
                        var correctRadio = document.getElementById('radio' + correctIndex + '' + correctIndex);
                        if (correctLab) {
                            correctLab.style.border = '5px solid #00ff00'; correctLab.style.borderRadius = '10px';
                            correctLab.style.boxShadow = '0 0 15px #00ff00'; correctLab.style.fontWeight = 'bold'; correctLab.style.color = '#00cc00';
                        }
                        if (correctRadio) {
                            correctRadio.checked = true;
                            var selectedInput = document.getElementById("selected_answer");
                            if (selectedInput) selectedInput.value = correctRadio.value;
                        }
                    }
                }
                window.addEventListener('load', function() { setTimeout(_highlightCorrectAnswer, 500); });
                setTimeout(_highlightCorrectAnswer, 500);
            '''
        })
    except Exception:
        pass
    
    # 2. Stop swallowing navigation errors. Let it propagate so _patched_open_browser handles it cleanly.
    _orig_get(self, url)

_Chrome.get = _safe_get

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

_c.get_usb_list = lambda: []
_c.get_mac = lambda: '00:00:00:00:00:00'
_c.get_system_ip = lambda: '127.0.0.1'
_c.get_system_ipv6 = lambda: '::1'

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

import registry_edit
registry_edit.Regedit.reg_edit_start = lambda self: True
registry_edit.Regedit.reg_edit_end = lambda self: True
"""
