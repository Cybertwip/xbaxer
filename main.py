import pygame
import cv2
import numpy as np
import websocket
import threading
import ssl
import sys
import time
import os
import queue
import struct
import requests
import socket
import concurrent.futures
import urllib3
import paramiko
import io
import re
import json
import subprocess
import tempfile
import shutil
import zipfile
from datetime import datetime
import xml.etree.ElementTree as ET

try:
    import tkinter as tk
    from tkinter import filedialog
except Exception:
    tk = None
    filedialog = None

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ================== CONFIG ==================
MENU_SIZE = (1280, 720)
STREAM_SIZE = (1280, 920)
FPS = 60
CONFIG_FILE = "xbox_config.json"
TELNET_CONNECT_TIMEOUT = 15
TELNET_KEEPALIVE_INTERVAL = 10
SSH_KEEPALIVE_INTERVAL = 20
HEADER_HEIGHT = 80
DEFAULT_TERMINAL_HEIGHT = 290
MIN_TERMINAL_HEIGHT = 180
MIN_VIDEO_HEIGHT = 180
SPLITTER_HEIGHT = 10
LIVE_TERMINAL_ROW_SHARE_NUM = 2
LIVE_TERMINAL_ROW_SHARE_DEN = 3
MIN_LIVE_TERMINAL_ROWS = 6
REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(REPO_ROOT, "build")
LOCAL_PACKAGE_DIR = os.path.join(BUILD_DIR, "package", "Xbax")
REMOTE_INSTALL_DIR = "D:/DevelopmentFiles/Sandbox/Xbax"
REMOTE_INSTALL_MANIFEST = ".xbax-install-manifest.json"
REMOTE_INSTALL_BUNDLE = ".xbax-install-bundle.zip"
REMOTE_BOOTSTRAP_DIRNAME = ".xbax-bootstrap"
BS_RELAY_LISTEN = "0.0.0.0:17777"
BS_RELAY_PORT = 17777
REMOTE_SARVER_DIR = REMOTE_INSTALL_DIR + "/sarver"
APPX_UTIL_SOURCE_DIR = os.path.join(REPO_ROOT, "third_party", "appx-util", "appx-util-main")
APPX_UTIL_BUILD_DIR = os.path.join(REPO_ROOT, "third_party", "appx-util", "build")
APPX_UTIL_BINARY = os.path.join(APPX_UTIL_BUILD_DIR, "appx.exe" if os.name == "nt" else "appx")
DEVICE_PORTAL_PACKAGE_APIS = [
    "/api/app/packagemanager/package",
    "/api/appx/packagemanager/package",
]
DEVICE_PORTAL_PACKAGE_STATE_APIS = [
    "/api/app/packagemanager/state",
    "/api/appx/packagemanager/state",
]

APPX_MANIFEST_NS = {
    "pkg": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
    "uap": "http://schemas.microsoft.com/appx/manifest/uap/windows10",
}

APPX_DEFAULT_ASSETS = {
    "Assets/StoreLogo.png": (50, 50),
    "Assets/Square44x44Logo.png": (44, 44),
    "Assets/Square150x150Logo.png": (150, 150),
    "Assets/Square310x310Logo.png": (310, 310),
    "Assets/Wide310x150Logo.png": (310, 150),
    "Assets/SplashScreen.png": (620, 300),
}


def choose_directory_dialog(title, initial_dir=None, must_exist=True):
    if tk is None or filedialog is None:
        raise RuntimeError("tkinter is unavailable, so folder picking is not available in this environment")

    root = tk.Tk()
    root.withdraw()
    try:
        root.attributes("-topmost", True)
    except Exception:
        pass
    root.update_idletasks()
    selected = filedialog.askdirectory(
        title=title,
        initialdir=initial_dir or REPO_ROOT,
        mustexist=must_exist,
        parent=root,
    )
    root.destroy()
    return selected or None

# ================== PIN STORAGE ==================
def load_pin(ip):
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE, 'r') as f:
                return json.load(f).get(ip)
    except: pass
    return None

def save_pin(ip, pin):
    try:
        data = {}
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE, 'r') as f:
                data = json.load(f)
        data[ip] = pin
        with open(CONFIG_FILE, 'w') as f:
            json.dump(data, f)
    except: pass

def remove_pin(ip):
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE, 'r') as f:
                data = json.load(f)
            if ip in data:
                del data[ip]
                with open(CONFIG_FILE, 'w') as f:
                    json.dump(data, f)
    except: pass

def configure_keepalive(sock_obj):
    sock_obj.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

    for opt_name, value in (("TCP_KEEPIDLE", 30), ("TCP_KEEPINTVL", 10), ("TCP_KEEPCNT", 3)):
        opt = getattr(socket, opt_name, None)
        if opt is None:
            continue
        try:
            sock_obj.setsockopt(socket.IPPROTO_TCP, opt, value)
        except OSError:
            pass

# ================== KEY & MOUSE CONSTANTS ==================
VK_MAP = {
    pygame.K_BACKSPACE: 0x08, pygame.K_TAB: 0x09, pygame.K_RETURN: 0x0D,
    pygame.K_ESCAPE: 0x1B, pygame.K_SPACE: 0x20,
    pygame.K_UP: 0x26, pygame.K_DOWN: 0x28, pygame.K_LEFT: 0x25, pygame.K_RIGHT: 0x27,
    pygame.K_z: 0xC3, pygame.K_x: 0xC4,
}

MOUSE_MOVE = 0x0001
L_DOWN = 0x0002; L_UP = 0x0004; R_DOWN = 0x0008; R_UP = 0x0010
M_DOWN = 0x0020; M_UP = 0x0040; WHEEL_V = 0x0800

# ================== NETWORK SCANNER ==================
def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('10.255.255.255', 1))
        ip = s.getsockname()[0]
    except:
        ip = '192.168.0.1'
    finally:
        s.close()
    return ip

def check_xbox(ip):
    try:
        res = requests.get(f"https://{ip}:11443/ext/screenshot",
                           params={'download': 'false'},
                           verify=False, timeout=0.6)
        if res.status_code == 200:
            try:
                hostname = socket.gethostbyaddr(ip)[0].split('.')[0]
            except:
                hostname = "Xbox Devkit"
            return (ip, hostname)
    except:
        pass
    return None

def scan_network_async(result_list, callback):
    local_ip = get_local_ip()
    base = '.'.join(local_ip.split('.')[:-1])
    ips = [f"{base}.{i}" for i in range(1, 255)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=60) as exe:
        for res in exe.map(check_xbox, ips):
            if res:
                result_list.append(res)
    callback()

# ================== UI ==================
class Button:
    def __init__(self, x, y, w, h, text, color=(0, 120, 215), hover=(0, 160, 255), disabled=(60, 70, 80)):
        self.text = text
        self.color = color
        self.hover = hover
        self.disabled = disabled
        self.enabled = True
        self.font = pygame.font.SysFont("consolas", 20, bold=True)
        # Measure text ONCE at construction — no runtime overflow
        txt_w = self.font.size(text)[0]
        actual_w = max(w, txt_w + 30)
        self.rect = pygame.Rect(x, y, actual_w, h)

    def draw(self, surf):
        if not self.enabled:
            col = self.disabled
            txt_color = (185, 185, 185)
        else:
            col = self.hover if self.rect.collidepoint(pygame.mouse.get_pos()) else self.color
            txt_color = (255, 255, 255)
        pygame.draw.rect(surf, col, self.rect, border_radius=8)
        txt = self.font.render(self.text, True, txt_color)
        surf.blit(txt, txt.get_rect(center=self.rect.center))

    def clicked(self, pos):
        return self.enabled and self.rect.collidepoint(pos)

# ================== TRUE VT100 TERMINAL EMULATOR ==================
class IntegratedTerminal:
    def __init__(self, x, y, w, h):
        self.rect = pygame.Rect(x, y, w, h)
        self.font = pygame.font.SysFont("consolas", 17)
        self.line_h = 19

        self.cols = 140
        self.rows = 40
        self.grid = [[' ' for _ in range(self.cols)] for _ in range(self.rows)]
        self.history = [
            "[INFO] Xbox Devkit VT100 Terminal Emulation Active",
            "[INFO] Drag and drop any file onto this window to upload to Sandbox."
        ]
        self.screen_history = []

        self.cx = 0
        self.cy = 0

        self.input_buffer = ""
        self.command_history = []
        self.command_history_index = None
        self.command_history_draft = ""
        self.sock = None
        self.ssh_client = None
        self.connected = False
        self.intentional_disconnect = False
        self.lock = threading.Lock()

        self.focused = False
        self.fullscreen_mode = False
        self.raw_input_mode = False
        self.scroll_offset = 0
        self.needs_pin_prompt = False
        self.needs_reboot_prompt = False
        self.retry_count = 0
        self.ip = None
        self.pin = None
        self.installing = False
        self.package_busy = False
        self.bs_running = False
        self.bs_busy = False
        self.bs_stop_requested = False
        self.bs_process = None
        self.bs_relay_url = None
        self.bs_lock = threading.Lock()

        # Dirty-cache: only re-render the body surface when content changes
        self._dirty = True
        self._cached_surf = None
        self._last_focused = None
        self._last_scroll = None
        self._last_rect = None

    def _mark_dirty(self):
        self._dirty = True

    def _trim_history_locked(self):
        if len(self.history) > 2500:
            self.history = self.history[-1800:]
        if len(self.screen_history) > 2500:
            self.screen_history = self.screen_history[-1800:]

    def _active_display_lines_locked(self):
        active_lines = [''.join(r).rstrip() for r in self.grid]
        last_nonempty = -1
        for idx, line in enumerate(active_lines):
            if line:
                last_nonempty = idx
        last_visible = max(last_nonempty, min(self.cy, self.rows - 1))
        return active_lines[:max(1, last_visible + 1)]

    def _display_lines_locked(self):
        # Host-side status messages should read as scrollback above the live
        # VT100 screen so the current remote prompt stays at the bottom.
        return self.history + self.screen_history + self._active_display_lines_locked()

    def _visible_lines_locked(self):
        all_lines = self._display_lines_locked()
        active_lines = self.screen_history + self._active_display_lines_locked()
        if self.scroll_offset > 0 or not self.connected or not active_lines:
            start_idx = max(0, len(all_lines) - self.rows - self.scroll_offset)
            return all_lines[start_idx: start_idx + self.rows]

        min_active_rows = max(
            1,
            min(
                self.rows,
                max(MIN_LIVE_TERMINAL_ROWS, (self.rows * LIVE_TERMINAL_ROW_SHARE_NUM) // LIVE_TERMINAL_ROW_SHARE_DEN),
            ),
        )
        active_rows = min(len(active_lines), min_active_rows)
        history_rows = min(len(self.history), max(0, self.rows - active_rows))

        remaining_rows = self.rows - active_rows - history_rows
        if remaining_rows > 0:
            extra_active = min(len(active_lines) - active_rows, remaining_rows)
            active_rows += extra_active
            remaining_rows -= extra_active
        if remaining_rows > 0:
            history_rows += min(len(self.history) - history_rows, remaining_rows)

        if history_rows:
            visible_lines = self.history[-history_rows:] + active_lines[-active_rows:]
        else:
            visible_lines = active_lines[-active_rows:]
        return visible_lines[-self.rows:]

    def _remember_command(self, command):
        command = command.rstrip()
        if not command:
            self.command_history_index = None
            self.command_history_draft = ""
            return
        if not self.command_history or self.command_history[-1] != command:
            self.command_history.append(command)
            if len(self.command_history) > 300:
                self.command_history = self.command_history[-200:]
        self.command_history_index = None
        self.command_history_draft = ""

    def _navigate_command_history(self, step):
        if not self.command_history:
            return False

        if self.command_history_index is None:
            if step > 0:
                return False
            self.command_history_draft = self.input_buffer
            next_index = len(self.command_history) - 1
        else:
            next_index = self.command_history_index + step

        if next_index >= len(self.command_history):
            self.input_buffer = self.command_history_draft
            self.command_history_index = None
            self.command_history_draft = ""
        else:
            self.command_history_index = max(0, next_index)
            self.input_buffer = self.command_history[self.command_history_index]
        return True

    def _delete_previous_word(self):
        if not self.input_buffer:
            return False

        trimmed = self.input_buffer.rstrip()
        if not trimmed:
            self.input_buffer = ""
            return True

        boundary = len(trimmed)
        while boundary > 0 and not trimmed[boundary - 1].isspace():
            boundary -= 1
        self.input_buffer = trimmed[:boundary]
        return True

    def _get_clipboard_text(self):
        scrap = getattr(pygame, "scrap", None)
        if scrap is None:
            return ""
        try:
            if not scrap.get_init():
                scrap.init()
        except Exception:
            return ""

        text_types = []
        scrap_text = getattr(pygame, "SCRAP_TEXT", None)
        if scrap_text is not None:
            text_types.append(scrap_text)
        text_types.extend(["text/plain;charset=utf-8", "UTF8_STRING", "text/plain"])

        for text_type in text_types:
            try:
                payload = scrap.get(text_type)
            except Exception:
                continue
            if not payload:
                continue
            if isinstance(payload, memoryview):
                payload = payload.tobytes()
            if isinstance(payload, bytes):
                try:
                    text = payload.decode("utf-8")
                except UnicodeDecodeError:
                    text = payload.decode("latin-1", errors="replace")
            else:
                text = str(payload)
            return text.replace("\x00", "")
        return ""

    def _paste_clipboard(self):
        text = self._get_clipboard_text()
        if not text:
            return False

        if self.raw_input_mode:
            payload = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
            try:
                self.sock.sendall(payload.encode("utf-8"))
                return True
            except Exception as exc:
                self.log(f"[-] Failed to paste clipboard text: {exc}")
                return False

        pasted = " ".join(part for part in text.replace("\r\n", "\n").replace("\r", "\n").split("\n") if part)
        if not pasted:
            return False
        self.input_buffer += pasted
        self.command_history_index = None
        self.command_history_draft = ""
        self._mark_dirty()
        return True

    def log(self, message):
        with self.lock:
            self.history.append(message)
            self._trim_history_locked()
        self._mark_dirty()

    def _send_shell_bytes(self, payload, description=None):
        if not self.connected or not self.sock:
            return False
        try:
            self.sock.sendall(payload)
            if description:
                self.log(description)
            return True
        except Exception as exc:
            self.log(f"[-] Failed to send shell input: {exc}")
            return False

    def _send_shell_interrupt(self):
        if not self.connected or not self.sock:
            return False

        sent_parts = []

        for payload, label in (
            (b"\xff\xf4", "telnet IP"),
            (b"\xff\xf2", "telnet DM"),
            (b"\x03", "Ctrl+C"),
        ):
            try:
                self.sock.sendall(payload)
                sent_parts.append(label)
            except Exception:
                pass

        try:
            if hasattr(socket, "MSG_OOB"):
                self.sock.send(b"\xf2", socket.MSG_OOB)
                sent_parts.append("urgent DM")
        except Exception:
            pass

        if sent_parts:
            self.log(f"[*] Sent interrupt to remote shell ({', '.join(sent_parts)}).")
            return True

        self.log("[-] Failed to send Ctrl+C interrupt to remote shell.")
        return False

    def has_active_ssh(self):
        transport = self.ssh_client.get_transport() if self.ssh_client else None
        return bool(transport and transport.is_active())

    def is_package_busy(self):
        return self.package_busy

    def can_package_appx(self):
        return not self.installing and not self.package_busy

    def can_deploy_appx(self):
        return not self.installing and not self.package_busy

    def can_install(self):
        return self.connected and self.has_active_ssh() and not self.installing and not self.package_busy

    def is_bs_running(self):
        with self.bs_lock:
            return self.bs_running

    def can_toggle_bs(self):
        with self.bs_lock:
            if self.bs_busy:
                return False
            if self.bs_running:
                return True
        return self.connected and self.has_active_ssh() and not self.installing and not self.package_busy

    def _local_cliant_path(self):
        candidates = [
            os.path.join(BUILD_DIR, "host", "cliant", "cliant"),
            os.path.join(BUILD_DIR, "host", "cliant", "cliant.exe"),
        ]
        for path in candidates:
            if os.path.isfile(path):
                return path
        return candidates[0]

    def _remote_sarver_path(self):
        return REMOTE_SARVER_DIR.rstrip("/") + "/sarver.exe"

    def _ensure_local_cliant(self):
        cliant_path = self._local_cliant_path()
        if os.path.isfile(cliant_path):
            return cliant_path
        self.log("[*] Building host cliant relay...")
        self._run_local_command(["cmake", "--build", BUILD_DIR, "--target", "host-cliant", "-j4"], REPO_ROOT)
        if not os.path.isfile(cliant_path):
            raise RuntimeError(f"host cliant not found after build: {cliant_path}")
        return cliant_path

    def _ensure_remote_sarver_installed(self):
        remote_sarver = self._remote_sarver_path().replace("/", "\\")
        command = f'cmd /c if exist {self._cmd_quote(remote_sarver)} (exit /b 0) else (exit /b 1)'
        exit_status, _, _ = self._run_remote_command(command)
        if exit_status != 0:
            raise RuntimeError(
                f"remote sarver.exe not found at {remote_sarver}; run Install first"
            )
        return remote_sarver

    def _watch_bs_process(self, process):
        try:
            if process.stdout:
                for line in process.stdout:
                    line = line.rstrip()
                    if line:
                        self.log(f"[bs] {line}")
        except Exception as exc:
            self.log(f"[*] BS relay log stream ended unexpectedly: {exc}")
        finally:
            return_code = process.wait()
            should_stop_remote = False
            stop_requested = False
            with self.bs_lock:
                if self.bs_process is process:
                    stop_requested = self.bs_stop_requested
                    should_stop_remote = not stop_requested
                    self.bs_process = None
                    self.bs_running = False
                    self.bs_busy = False
                    self.bs_stop_requested = False
                    self.bs_relay_url = None
            self._mark_dirty()

            if should_stop_remote and self.has_active_ssh():
                try:
                    self._run_remote_command('taskkill /F /IM sarver.exe /T >NUL 2>&1')
                except Exception:
                    pass

            if stop_requested:
                self.log("[*] BS relay stopped.")
            elif return_code == 0:
                self.log("[*] BS relay exited.")
            else:
                self.log(f"[-] BS relay exited with code {return_code}.")

    def start_bs(self):
        with self.bs_lock:
            if self.bs_busy:
                self.log("[-] BS start/stop already in progress.")
                return
            if self.bs_running:
                self.log("[-] BS is already running.")
                return
            self.bs_busy = True
            self.bs_stop_requested = False
        self._mark_dirty()

        relay_process = None
        try:
            if not self.connected or not self.has_active_ssh():
                raise RuntimeError("Connect Dev Shell first to start BS.")

            cliant_path = self._ensure_local_cliant()
            remote_sarver = self._ensure_remote_sarver_installed()
            relay_url = f"http://{get_local_ip()}:{BS_RELAY_PORT}"

            self.log(f"[*] Starting local cliant relay on http://{BS_RELAY_LISTEN}...")
            relay_process = subprocess.Popen(
                [cliant_path, "serve", "-listen", BS_RELAY_LISTEN],
                cwd=REPO_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            time.sleep(0.35)
            if relay_process.poll() is not None:
                startup_log = ""
                if relay_process.stdout:
                    startup_log = relay_process.stdout.read().strip()
                raise RuntimeError(startup_log or "cliant relay exited immediately")

            self.log(f"[*] Launching remote sarver.exe -reverse {relay_url} ...")
            try:
                self._run_remote_command('taskkill /F /IM sarver.exe /T >NUL 2>&1')
            except Exception:
                pass

            exit_status, output, error = self._run_remote_command(
                f'cmd /c start "" /b {self._cmd_quote(remote_sarver)} -reverse {self._cmd_quote(relay_url)}'
            )
            if exit_status != 0:
                raise RuntimeError((error or output or "remote sarver launch failed").strip())

            with self.bs_lock:
                self.bs_process = relay_process
                self.bs_running = True
                self.bs_busy = False
                self.bs_stop_requested = False
                self.bs_relay_url = relay_url
            self._mark_dirty()

            threading.Thread(target=self._watch_bs_process, args=(relay_process,), daemon=True).start()
            self.log(f"[+] BS started. Relay URL: {relay_url}")
        except Exception as exc:
            if self.has_active_ssh():
                try:
                    self._run_remote_command('taskkill /F /IM sarver.exe /T >NUL 2>&1')
                except Exception:
                    pass
            if relay_process and relay_process.poll() is None:
                try:
                    relay_process.terminate()
                    relay_process.wait(timeout=3)
                except Exception:
                    try:
                        relay_process.kill()
                    except Exception:
                        pass
            with self.bs_lock:
                self.bs_running = False
                self.bs_busy = False
                self.bs_stop_requested = False
                self.bs_process = None
                self.bs_relay_url = None
            self._mark_dirty()
            self.log(f"[-] Start BS failed: {exc}")

    def stop_bs(self):
        with self.bs_lock:
            if self.bs_busy:
                self.log("[-] BS start/stop already in progress.")
                return
            process = self.bs_process
            running = self.bs_running
            self.bs_busy = True
            self.bs_stop_requested = True
        self._mark_dirty()

        try:
            if self.has_active_ssh():
                self.log("[*] Stopping remote sarver.exe...")
                self._run_remote_command('taskkill /F /IM sarver.exe /T >NUL 2>&1')
            else:
                self.log("[*] SSH is unavailable; stopping the local BS relay only.")

            if process and process.poll() is None:
                self.log("[*] Stopping local cliant relay...")
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            elif not running:
                self.log("[*] BS was not running.")

            with self.bs_lock:
                if self.bs_process is process:
                    self.bs_process = None
                    self.bs_running = False
                    self.bs_busy = False
                    self.bs_stop_requested = False
                    self.bs_relay_url = None
            self._mark_dirty()
            self.log("[*] BS relay stopped.")
        except Exception as exc:
            with self.bs_lock:
                self.bs_busy = False
            self._mark_dirty()
            self.log(f"[-] Stop BS failed: {exc}")

    def resize_grid(self, new_rows):
        if new_rows <= 0 or new_rows == self.rows: return
        with self.lock:
            new_grid = [[' ' for _ in range(self.cols)] for _ in range(new_rows)]
            min_r = min(self.rows, new_rows)
            for i in range(min_r):
                new_grid[new_rows - 1 - i] = self.grid[self.rows - 1 - i]
            self.grid = new_grid
            self.cy = min(self.cy, new_rows - 1)
            self.rows = new_rows
            self._mark_dirty()
            if self.connected and self.sock:
                try:
                    naws = b'\xff\xfb\x1f\xff\xfa\x1f' + struct.pack('!HH', self.cols, self.rows) + b'\xff\xf0'
                    self.sock.sendall(naws)
                except: pass

    def scroll_up(self):
        top_line = ''.join(self.grid[0]).rstrip()
        if top_line:
            self.screen_history.append(top_line)
        self._trim_history_locked()
        self.grid.pop(0)
        self.grid.append([' ' for _ in range(self.cols)])
        self.cy = max(0, self.cy - 1)
        self._mark_dirty()

    def clear_screen(self):
        self.grid = [[' ' for _ in range(self.cols)] for _ in range(self.rows)]
        self.cx = 0
        self.cy = 0
        self._mark_dirty()

    def write(self, data):
        text = data.decode('utf-8', errors='replace')
        text = re.sub(r'\xff[\xfb-\xfe].', '', text)
        text = re.sub(r'\xff[\xf0-\xfa]', '', text)
        tokens = re.split(r'(\x1b\[[0-9;?]*[A-Za-z])', text)

        with self.lock:
            for token in tokens:
                if not token: continue
                if token.startswith('\x1b['):
                    code = token[-1]
                    args_str = token[2:-1].replace('?', '')
                    args = args_str.split(';') if args_str else []
                    if code == 'J':
                        arg = args[0] if args else '0'
                        if arg == '2':
                            self.clear_screen()
                        elif arg == '0':
                            for c in range(self.cx, self.cols): self.grid[self.cy][c] = ' '
                            for r in range(self.cy + 1, self.rows): self.grid[r] = [' '] * self.cols
                    elif code == 'K':
                        arg = args[0] if args else '0'
                        if arg == '0':
                            for c in range(self.cx, self.cols): self.grid[self.cy][c] = ' '
                        elif arg == '2':
                            self.grid[self.cy] = [' '] * self.cols
                    elif code in ('H', 'f'):
                        r = int(args[0])-1 if len(args)>0 and args[0] else 0
                        c = int(args[1])-1 if len(args)>1 and args[1] else 0
                        self.cy = max(0, min(self.rows-1, r))
                        self.cx = max(0, min(self.cols-1, c))
                    elif code == 'A': self.cy = max(0, self.cy - (int(args[0]) if args and args[0] else 1))
                    elif code == 'B': self.cy = min(self.rows-1, self.cy + (int(args[0]) if args and args[0] else 1))
                    elif code == 'C': self.cx = min(self.cols-1, self.cx + (int(args[0]) if args and args[0] else 1))
                    elif code == 'D': self.cx = max(0, self.cx - (int(args[0]) if args and args[0] else 1))
                else:
                    for char in token:
                        if char == '\n':
                            self.cy += 1
                            self.cx = 0
                            if self.cy >= self.rows:
                                self.scroll_up()
                                self.cy = self.rows - 1
                        elif char == '\r':
                            self.cx = 0
                        elif char in ('\x08', '\b'):
                            self.cx = max(0, self.cx - 1)
                        elif char == '\t':
                            self.cx = min(self.cols - 1, (self.cx + 4) // 4 * 4)
                        elif char == '\x0c':
                            self.clear_screen()
                        elif ord(char) >= 32:
                            if self.cx >= self.cols:
                                self.cx = 0
                                self.cy += 1
                                if self.cy >= self.rows:
                                    self.scroll_up()
                                    self.cy = self.rows - 1
                            self.grid[self.cy][self.cx] = char
                            self.cx += 1

        self._mark_dirty()

    def scroll(self, amount):
        with self.lock:
            total_lines = len(self._display_lines_locked())
        max_scroll = max(0, total_lines - self.rows)
        new_offset = max(0, min(self.scroll_offset + amount, max_scroll))
        if new_offset != self.scroll_offset:
            self.scroll_offset = new_offset
            self._mark_dirty()

    def handle_key(self, event):
        if not self.connected or event.type != pygame.KEYDOWN: return False
        self.scroll_offset = 0
        ctrl_pressed = bool(event.mod & pygame.KMOD_CTRL)
        shortcut_pressed = bool(event.mod & (pygame.KMOD_CTRL | pygame.KMOD_META))

        if ctrl_pressed and event.key == pygame.K_c:
            self.input_buffer = ""
            self.command_history_index = None
            self.command_history_draft = ""
            self._mark_dirty()
            return self._send_shell_interrupt()

        if shortcut_pressed and event.key == pygame.K_v:
            return self._paste_clipboard()

        if event.key == pygame.K_TAB:
            self.raw_input_mode = not self.raw_input_mode
            return True

        if self.raw_input_mode:
            try:
                if event.key == pygame.K_RETURN:
                    self.sock.sendall(b"\r\n")
                elif event.key == pygame.K_BACKSPACE:
                    self.sock.sendall(b"\x08")
                elif event.unicode:
                    self.sock.sendall(event.unicode.encode('utf-8'))
            except: pass
            return True

        if event.key == pygame.K_RETURN:
            self._remember_command(self.input_buffer)
            cmd = (self.input_buffer + "\r\n").encode('utf-8')
            try: self.sock.sendall(cmd)
            except: pass
            self.input_buffer = ""
            self._mark_dirty()
            return True
        elif event.key == pygame.K_UP:
            if self._navigate_command_history(-1):
                self._mark_dirty()
                return True
        elif event.key == pygame.K_DOWN:
            if self._navigate_command_history(1):
                self._mark_dirty()
                return True
        elif event.key == pygame.K_BACKSPACE:
            if shortcut_pressed:
                if self._delete_previous_word():
                    self.command_history_index = None
                    self.command_history_draft = ""
                    self._mark_dirty()
                    return True
            else:
                self.input_buffer = self.input_buffer[:-1]
                self.command_history_index = None
                self.command_history_draft = ""
                self._mark_dirty()
                return True
        elif event.unicode and event.unicode.isprintable():
            self.input_buffer += event.unicode
            self.command_history_index = None
            self.command_history_draft = ""
            self._mark_dirty()
            return True
        return False

    def draw(self, screen):
        # Dynamic row resize
        visible_rows = (self.rect.height - 50) // self.line_h
        if visible_rows != self.rows and visible_rows > 0:
            self.resize_grid(visible_rows)

        # Invalidate cache on layout/state changes
        if (self._last_focused != self.focused or
                self._last_scroll  != self.scroll_offset or
                self._last_rect    != self.rect.size):
            self._mark_dirty()
            self._last_focused = self.focused
            self._last_scroll  = self.scroll_offset
            self._last_rect    = self.rect.size

        # Rebuild surface only when dirty
        if self._dirty or self._cached_surf is None:
            body_h = max(1, self.rect.height - 40)
            surf = pygame.Surface((self.rect.width, body_h))
            surf.fill((15, 15, 25))

            with self.lock:
                visible_lines = self._visible_lines_locked()

            y = 10
            for line in visible_lines:
                if line:
                    surf.blit(self.font.render(line, True, (0, 255, 120)), (12, y))
                y += self.line_h

            self._cached_surf = surf
            self._dirty = False

        screen.blit(self._cached_surf, (self.rect.x, self.rect.y))
        outline_color = (0, 255, 120) if self.focused else (50, 70, 80)
        pygame.draw.rect(screen, outline_color, self.rect, 3)

        # Footer bar
        footer_y = self.rect.bottom - 30
        pygame.draw.rect(screen, (15, 15, 25),
                         (self.rect.x, footer_y - 4, self.rect.width, 34))

        mode_text  = "[RAW INPUT ON]" if self.raw_input_mode else "[BUFFERED INPUT]"
        mode_color = (255, 100, 100) if self.raw_input_mode else (100, 150, 255)
        m_surf = self.font.render(mode_text, True, mode_color)
        screen.blit(m_surf, (self.rect.right - m_surf.get_width() - 20, footer_y))

        if not self.raw_input_mode:
            cursor = "_" if (self.focused and time.time() % 1 > 0.5) else ""
            prompt = "> " + self.input_buffer + cursor
            ps = self.font.render(prompt, True, (0, 255, 200))
            screen.blit(ps, (self.rect.x + 12, footer_y))

    def upload_file(self, filepath):
        if not self.ssh_client or not self.ssh_client.get_transport().is_active():
            with self.lock: self.history.append("[-] SSH disconnected. Please wait for auto-reconnect.")
            self._mark_dirty(); return

        filename = os.path.basename(filepath)
        remote_dir = self._current_remote_directory() or "D:/DevelopmentFiles/Sandbox"
        remote_path = remote_dir.rstrip("/") + "/" + filename
        with self.lock: self.history.append(f"[*] Uploading '{filename}' to {remote_dir} via SFTP...")
        self._mark_dirty()
        try:
            sftp = self.ssh_client.open_sftp()
            self._remote_mkdirs(sftp, remote_dir)
            sftp.put(filepath, remote_path)
            sftp.close()
            with self.lock: self.history.append(f"[+] '{filename}' uploaded successfully to {remote_dir}!")
            self._mark_dirty()
            if self.connected: self.sock.sendall(b"dir\r\n")
        except Exception as e:
            with self.lock: self.history.append(f"[-] Upload failed: {e}")
            self._mark_dirty()

    def _current_remote_directory(self):
        prompt_pattern = re.compile(r"([A-Za-z]:(?:\\[^>\r\n]*)?)>")
        with self.lock:
            all_lines = self._display_lines_locked()

        for line in reversed(all_lines):
            match = prompt_pattern.search(line)
            if match:
                return match.group(1).replace("\\", "/")
        return None

    def _run_local_command(self, cmd, cwd):
        self.log(f"[*] Running: {' '.join(cmd)}")
        try:
            process = subprocess.Popen(
                cmd,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except FileNotFoundError as exc:
            raise RuntimeError(f"missing build tool: {exc.filename}") from exc

        if process.stdout:
            for line in process.stdout:
                line = line.rstrip()
                if line:
                    self.log(f"    {line}")

        return_code = process.wait()
        if return_code != 0:
            raise RuntimeError(f"command failed with exit code {return_code}: {' '.join(cmd)}")

    def _sanitize_package_token(self, value, fallback="HelloWin"):
        token = re.sub(r"[^A-Za-z0-9]+", "", value or "")
        if not token:
            token = fallback
        if not token[0].isalpha():
            token = fallback + token
        return token[:40]

    def _default_appx_name(self, source_dir):
        base_name = os.path.basename(os.path.abspath(source_dir)) or "hellowin"
        safe_name = re.sub(r"[^A-Za-z0-9._-]+", "-", base_name).strip("._-") or "hellowin"
        return safe_name + ".appx"

    def _default_appx_version(self):
        now = datetime.now()
        minute_of_day = (now.hour * 60) + now.minute
        return f"{now.year}.{now.month}.{now.day}.{minute_of_day}"

    def _find_entry_executable(self, root_dir, preferred_token=None):
        candidates = []
        folder_token = self._sanitize_package_token(preferred_token or os.path.basename(root_dir), fallback="App").lower()
        for walk_root, _, filenames in os.walk(root_dir):
            filenames.sort()
            for filename in filenames:
                if not filename.lower().endswith(".exe"):
                    continue
                rel_path = os.path.relpath(os.path.join(walk_root, filename), root_dir).replace(os.sep, "/")
                exe_token = self._sanitize_package_token(os.path.splitext(filename)[0], fallback="App").lower()
                candidates.append((rel_path, exe_token))

        if not candidates:
            raise RuntimeError(f"no .exe was found under {root_dir}")

        candidates.sort(
            key=lambda item: (
                item[0].count("/"),
                0 if item[1] == folder_token else 1,
                len(item[0]),
                item[0].lower(),
            )
        )
        return candidates[0][0]

    def _guess_asset_size(self, rel_path):
        normalized = rel_path.replace("\\", "/")
        if normalized in APPX_DEFAULT_ASSETS:
            return APPX_DEFAULT_ASSETS[normalized]

        lower_name = os.path.basename(normalized).lower()
        if "44x44" in lower_name:
            return (44, 44)
        if "150x150" in lower_name:
            return (150, 150)
        if "310x310" in lower_name:
            return (310, 310)
        if "310x150" in lower_name or "wide" in lower_name:
            return (310, 150)
        if "splash" in lower_name:
            return (620, 300)
        return (64, 64)

    def _create_placeholder_asset(self, path, size):
        os.makedirs(os.path.dirname(path), exist_ok=True)

        surface = pygame.Surface(size)
        surface.fill((14, 18, 28))
        pygame.draw.rect(surface, (0, 180, 120), surface.get_rect(), width=max(2, min(size) // 18))
        pygame.draw.line(surface, (0, 120, 215), (0, 0), (size[0] - 1, size[1] - 1), max(2, min(size) // 22))
        pygame.draw.line(surface, (0, 120, 215), (size[0] - 1, 0), (0, size[1] - 1), max(2, min(size) // 22))
        pygame.image.save(surface, path)

    def _normalize_manifest_text(self, manifest_text, exe_rel_path):
        exe_name = os.path.splitext(os.path.basename(exe_rel_path))[0]
        return manifest_text.lstrip("\ufeff").replace("$targetnametoken$", exe_name)

    def _generated_manifest_text(self, exe_rel_path, package_name, display_name):
        executable = exe_rel_path.replace("/", "\\")
        display = (
            display_name.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace('"', "&quot;")
        )
        version = self._default_appx_version()
        return f"""<?xml version="1.0" encoding="utf-8"?>
<Package IgnorableNamespaces="uap uap10 rescap"
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities">
  <Identity Name="{package_name}" Publisher="CN=Xbax" Version="{version}" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>{display}</DisplayName>
    <PublisherDisplayName>Xbax</PublisherDisplayName>
    <Logo>Assets\\StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.19041.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>
  <Resources>
    <Resource Language="x-generate" />
  </Resources>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
    <rescap:Capability Name="unvirtualizedResources" />
  </Capabilities>
  <Applications>
    <Application Id="App" Executable="{executable}" uap10:TrustLevel="mediumIL" uap10:RuntimeBehavior="win32App">
      <uap:VisualElements DisplayName="{display}" Description="{display}" BackgroundColor="transparent"
        Square150x150Logo="Assets\\Square150x150Logo.png" Square44x44Logo="Assets\\Square44x44Logo.png">
        <uap:DefaultTile Wide310x150Logo="Assets\\Wide310x150Logo.png" Square310x310Logo="Assets\\Square310x310Logo.png" />
        <uap:SplashScreen Image="Assets\\SplashScreen.png" />
      </uap:VisualElements>
    </Application>
  </Applications>
</Package>
"""

    def _manifest_asset_paths(self, manifest_path):
        asset_paths = set(APPX_DEFAULT_ASSETS.keys())
        try:
            tree = ET.parse(manifest_path)
        except ET.ParseError:
            return asset_paths

        root = tree.getroot()
        properties_logo = root.find("pkg:Properties/pkg:Logo", APPX_MANIFEST_NS)
        if properties_logo is not None and properties_logo.text:
            asset_paths.add(properties_logo.text.strip().replace("\\", "/"))

        visual = root.find(".//uap:VisualElements", APPX_MANIFEST_NS)
        if visual is not None:
            for attr_name in ("Square150x150Logo", "Square44x44Logo", "Wide310x150Logo", "Square310x310Logo"):
                attr_value = visual.attrib.get(attr_name)
                if attr_value:
                    asset_paths.add(attr_value.strip().replace("\\", "/"))
            splash = visual.find("uap:SplashScreen", APPX_MANIFEST_NS)
            if splash is not None:
                image = splash.attrib.get("Image")
                if image:
                    asset_paths.add(image.strip().replace("\\", "/"))

        return asset_paths

    def _ensure_manifest_assets(self, stage_dir, manifest_path):
        for rel_path in sorted(self._manifest_asset_paths(manifest_path)):
            normalized = rel_path.replace("\\", "/").lstrip("/")
            if not normalized:
                continue
            asset_path = os.path.join(stage_dir, *normalized.split("/"))
            if os.path.exists(asset_path):
                continue
            self._create_placeholder_asset(asset_path, self._guess_asset_size(normalized))

    def _prepare_appx_staging(self, source_dir):
        source_dir = os.path.abspath(source_dir)
        if not os.path.isdir(source_dir):
            raise RuntimeError(f"package source directory not found: {source_dir}")

        stage_dir = tempfile.mkdtemp(prefix="xbax-appx-stage-")
        shutil.copytree(source_dir, stage_dir, dirs_exist_ok=True)

        exe_rel_path = self._find_entry_executable(stage_dir, preferred_token=os.path.basename(source_dir))
        manifest_candidates = [
            os.path.join(stage_dir, "AppxManifest.xml"),
            os.path.join(stage_dir, "Package.appxmanifest"),
        ]
        manifest_target = os.path.join(stage_dir, "AppxManifest.xml")

        manifest_source = next((path for path in manifest_candidates if os.path.isfile(path)), None)
        if manifest_source:
            with open(manifest_source, "r", encoding="utf-8-sig") as manifest_file:
                manifest_text = self._normalize_manifest_text(manifest_file.read(), exe_rel_path)
        else:
            folder_name = os.path.basename(source_dir.rstrip(os.sep)) or "HelloWin"
            display_name = folder_name.replace("_", " ").replace("-", " ").strip() or "HelloWin"
            package_name = "Xbax." + self._sanitize_package_token(folder_name)
            manifest_text = self._generated_manifest_text(exe_rel_path, package_name, display_name)

        with open(manifest_target, "w", encoding="utf-8") as manifest_file:
            manifest_file.write(manifest_text)

        self._ensure_manifest_assets(stage_dir, manifest_target)
        return stage_dir

    def _ensure_appx_tool(self):
        if os.path.isfile(APPX_UTIL_BINARY):
            return APPX_UTIL_BINARY
        if not os.path.isdir(APPX_UTIL_SOURCE_DIR):
            raise RuntimeError(
                f"appx-util source was not found at {APPX_UTIL_SOURCE_DIR}; "
                f"download https://github.com/OSInside/appx-util and extract it under third_party/appx-util"
            )

        self.log("[*] Configuring local appx-util...")
        self._run_local_command(["cmake", "-S", APPX_UTIL_SOURCE_DIR, "-B", APPX_UTIL_BUILD_DIR], REPO_ROOT)
        self.log("[*] Building local appx-util...")
        self._run_local_command(["cmake", "--build", APPX_UTIL_BUILD_DIR, "-j4"], REPO_ROOT)

        if not os.path.isfile(APPX_UTIL_BINARY):
            raise RuntimeError(f"appx-util build did not produce {APPX_UTIL_BINARY}")
        return APPX_UTIL_BINARY

    def _package_directory_to_appx(self, source_dir, output_path):
        source_dir = os.path.abspath(source_dir)
        output_path = os.path.abspath(output_path)
        os.makedirs(os.path.dirname(output_path), exist_ok=True)

        appx_tool = self._ensure_appx_tool()
        stage_dir = None
        packaged = False
        try:
            stage_dir = self._prepare_appx_staging(source_dir)
            if os.path.exists(output_path):
                os.remove(output_path)
            self.log(f"[*] Packing {source_dir} into {output_path}...")
            self._run_local_command([appx_tool, "-o", output_path, stage_dir], REPO_ROOT)
            if not os.path.isfile(output_path):
                raise RuntimeError(f"appx output was not created: {output_path}")
            packaged = True
            return output_path
        finally:
            if stage_dir and packaged:
                shutil.rmtree(stage_dir, ignore_errors=True)
            elif stage_dir and not packaged:
                self.log(f"[*] Appx staging folder kept at {stage_dir}")

    def _device_portal_auth(self, ip):
        username, password = fetch_dev_credentials(ip)
        if password:
            return (username or "DevToolsUser", password)
        return None

    def _decode_device_portal_payload(self, response):
        try:
            return response.json()
        except ValueError:
            text = response.text.strip()
            return text or None

    def _summarize_device_portal_payload(self, payload):
        if isinstance(payload, dict):
            summary_parts = []
            error_code = None
            error_text = None
            success = None
            for key, value in payload.items():
                lowered = key.lower()
                if lowered in ("success", "succeeded", "issuccess"):
                    success = bool(value)
                elif "errorcode" in lowered or lowered in ("hresult", "extendederror"):
                    try:
                        error_code = int(str(value), 0)
                    except Exception:
                        if value not in (None, "", 0, "0"):
                            error_text = f"{key}={value}"
                elif lowered in ("message", "status", "phase", "packagename", "packagefullname"):
                    if value not in (None, ""):
                        summary_parts.append(f"{key}={value}")
                elif "error" in lowered and value not in (None, "", 0, "0"):
                    error_text = f"{key}={value}"

            summary = ", ".join(summary_parts) if summary_parts else json.dumps(payload, sort_keys=True)
            return summary, error_code, error_text, success

        text = str(payload).strip() if payload is not None else ""
        return text, None, None, None

    def _device_portal_post_package(self, ip, endpoint, appx_path, auth):
        package_name = os.path.basename(appx_path)
        mime = "application/vnd.ms-appx"
        last_response = None

        for active_auth in (auth, None) if auth else (None,):
            with open(appx_path, "rb") as package_file:
                response = requests.post(
                    f"https://{ip}:11443{endpoint}",
                    params={"package": package_name},
                    files=[("", (package_name, package_file, mime))],
                    auth=active_auth,
                    verify=False,
                    timeout=180,
                )
            last_response = response
            if response.status_code not in (401, 403):
                return response, active_auth

        return last_response, auth

    def _device_portal_get(self, ip, endpoint, auth, timeout=10):
        last_response = None
        for active_auth in (auth, None) if auth else (None,):
            response = requests.get(
                f"https://{ip}:11443{endpoint}",
                auth=active_auth,
                verify=False,
                timeout=timeout,
            )
            last_response = response
            if response.status_code not in (401, 403):
                return response, active_auth
        return last_response, auth

    def _deploy_appx_to_console(self, ip, appx_path):
        auth = self._device_portal_auth(ip)
        accepted_index = None
        active_auth = auth
        last_error = None

        # Use the package-manager endpoint that backs the Device Portal Apps
        # tab. `/ext/update/remote` is for system updates, not app deploys.
        for index, endpoint in enumerate(DEVICE_PORTAL_PACKAGE_APIS):
            response, used_auth = self._device_portal_post_package(ip, endpoint, appx_path, active_auth)
            active_auth = used_auth
            if response is None:
                continue
            if response.status_code == 404:
                last_error = RuntimeError(f"{endpoint} returned HTTP 404")
                continue
            if response.status_code >= 400:
                payload = self._decode_device_portal_payload(response)
                raise RuntimeError(
                    f"device portal rejected deploy request at {endpoint}: HTTP {response.status_code} "
                    f"{payload if payload is not None else ''}".rstrip()
                )
            accepted_index = index
            self.log(f"[*] Deploy accepted by {endpoint}. Waiting for install status...")
            break

        if accepted_index is None:
            raise last_error or RuntimeError("no compatible device-portal package endpoint accepted the deploy request")

        state_endpoint = DEVICE_PORTAL_PACKAGE_STATE_APIS[accepted_index]
        deadline = time.time() + 300
        next_progress_log = 0.0

        while time.time() < deadline:
            response, active_auth = self._device_portal_get(ip, state_endpoint, active_auth, timeout=15)
            if response is None:
                time.sleep(1.0)
                continue
            if response.status_code == 204:
                if time.time() >= next_progress_log:
                    self.log("[*] Deploy still running on the console...")
                    next_progress_log = time.time() + 2.0
                time.sleep(1.0)
                continue
            if response.status_code == 404:
                time.sleep(1.0)
                continue
            if response.status_code >= 400:
                payload = self._decode_device_portal_payload(response)
                raise RuntimeError(
                    f"device portal status request failed at {state_endpoint}: HTTP {response.status_code} "
                    f"{payload if payload is not None else ''}".rstrip()
                )

            payload = self._decode_device_portal_payload(response)
            summary, error_code, error_text, success = self._summarize_device_portal_payload(payload)
            if error_code not in (None, 0) or success is False or error_text:
                detail = error_text or summary or f"error code {error_code}"
                raise RuntimeError(f"deploy failed: {detail}")
            if summary:
                self.log(f"[+] Deploy completed. {summary}")
            else:
                self.log("[+] Deploy completed.")
            return

        raise RuntimeError("deploy timed out while waiting for the package-manager status to finish")

    def package_directory(self, source_dir, output_dir):
        if self.package_busy:
            self.log("[-] Packaging is already running.")
            return

        self.package_busy = True
        self._mark_dirty()
        try:
            source_dir = os.path.abspath(source_dir)
            output_dir = os.path.abspath(output_dir)
            output_path = os.path.join(output_dir, self._default_appx_name(source_dir))
            packaged_path = self._package_directory_to_appx(source_dir, output_path)
            self.log(f"[+] Package complete: {packaged_path}")
        except Exception as exc:
            self.log(f"[-] Package failed: {exc}")
        finally:
            self.package_busy = False
            self._mark_dirty()

    def deploy_directory(self, source_dir, ip):
        if self.package_busy:
            self.log("[-] Packaging or deploy is already running.")
            return

        self.package_busy = True
        self._mark_dirty()
        temp_output_dir = None
        try:
            source_dir = os.path.abspath(source_dir)
            temp_output_dir = tempfile.mkdtemp(prefix="xbax-appx-output-")
            appx_path = os.path.join(temp_output_dir, self._default_appx_name(source_dir))
            packaged_path = self._package_directory_to_appx(source_dir, appx_path)
            self.log(f"[*] Deploying {os.path.basename(packaged_path)} to {ip}...")
            self._deploy_appx_to_console(ip, packaged_path)
        except Exception as exc:
            self.log(f"[-] Deploy failed: {exc}")
        finally:
            if temp_output_dir:
                shutil.rmtree(temp_output_dir, ignore_errors=True)
            self.package_busy = False
            self._mark_dirty()

    def _remote_mkdirs(self, sftp, remote_dir):
        remote_dir = remote_dir.replace("\\", "/").rstrip("/")
        if not remote_dir:
            return

        if ":/" in remote_dir:
            drive, tail = remote_dir.split(":/", 1)
            current = drive + ":/"
            parts = [part for part in tail.split("/") if part]
        else:
            current = ""
            parts = [part for part in remote_dir.split("/") if part]

        for part in parts:
            if current.endswith("/"):
                current = current + part
            elif current:
                current = current + "/" + part
            else:
                current = part
            try:
                sftp.stat(current)
            except IOError:
                sftp.mkdir(current)

    def _remote_path_exists(self, sftp, remote_path):
        try:
            return sftp.stat(remote_path)
        except IOError:
            return None

    def _remote_manifest_path(self, remote_dir):
        return remote_dir.rstrip("/") + "/" + REMOTE_INSTALL_MANIFEST

    def _remote_install_parent_dir(self, remote_dir):
        remote_dir = remote_dir.replace("\\", "/").rstrip("/")
        parent = os.path.dirname(remote_dir)
        return parent.replace("\\", "/") if parent else remote_dir

    def _remote_bootstrap_dir(self, remote_dir):
        parent = self._remote_install_parent_dir(remote_dir).rstrip("/")
        return parent + "/" + REMOTE_BOOTSTRAP_DIRNAME if parent else REMOTE_BOOTSTRAP_DIRNAME

    def _remote_bundle_path(self, remote_dir):
        return self._remote_bootstrap_dir(remote_dir).rstrip("/") + "/" + REMOTE_INSTALL_BUNDLE

    def _is_windows_safe_relpath(self, rel_path):
        invalid_chars = set('<>:"|?*')
        reserved_names = {
            "con", "prn", "aux", "nul",
            "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
            "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
        }

        for part in rel_path.replace("\\", "/").split("/"):
            if not part:
                continue
            if any(ch in invalid_chars for ch in part):
                return False
            if part.endswith((" ", ".")):
                return False
            stem = part.rstrip(" .").split(".", 1)[0].lower()
            if stem in reserved_names:
                return False
        return True

    def _collect_local_package_files(self, local_dir):
        files = []
        manifest = {}
        skipped_unsafe = []
        for root, _, filenames in os.walk(local_dir):
            filenames.sort()
            for filename in filenames:
                local_path = os.path.join(root, filename)
                rel_path = os.path.relpath(local_path, local_dir).replace(os.sep, "/")
                if not self._is_windows_safe_relpath(rel_path):
                    skipped_unsafe.append(rel_path)
                    continue
                # Package installs should upload the real file bytes even when
                # the package tree exposes SDK headers via symlinks.
                resolved_path = os.path.realpath(local_path)
                if os.path.islink(local_path) and not os.path.exists(resolved_path):
                    raise RuntimeError(f"packaged symlink is broken: {local_path} -> {os.readlink(local_path)}")
                local_stat = os.stat(resolved_path)
                files.append((resolved_path, rel_path))
                manifest[rel_path] = {
                    "size": local_stat.st_size,
                    "mtime": int(local_stat.st_mtime),
                }
        return files, manifest, skipped_unsafe

    def _read_remote_manifest(self, sftp, remote_dir):
        remote_manifest_path = self._remote_manifest_path(remote_dir)
        try:
            with sftp.file(remote_manifest_path, "rb") as remote_file:
                payload = remote_file.read()
        except IOError:
            return {}
        except Exception as exc:
            self.log(f"[*] Ignoring unreadable remote install manifest: {exc}")
            return {}

        try:
            if isinstance(payload, bytes):
                payload = payload.decode("utf-8")
            manifest = json.loads(payload)
        except Exception as exc:
            self.log(f"[*] Ignoring invalid remote install manifest: {exc}")
            return {}

        return manifest if isinstance(manifest, dict) else {}

    def _write_remote_manifest(self, sftp, remote_dir, manifest):
        remote_manifest_path = self._remote_manifest_path(remote_dir)
        with sftp.file(remote_manifest_path, "wb") as remote_file:
            remote_file.write(json.dumps(manifest, separators=(",", ":"), sort_keys=True).encode("utf-8"))

    def _should_upload_manifest_entry(self, local_entry, remote_entry):
        if not isinstance(remote_entry, dict):
            return True

        local_mtime = int(local_entry.get("mtime", 0))
        remote_mtime = int(remote_entry.get("mtime", 0))
        if local_mtime > remote_mtime:
            return True
        if local_mtime < remote_mtime:
            return False

        return int(local_entry.get("size", -1)) != int(remote_entry.get("size", -1))

    def _create_install_bundle(self, bundle_files, manifest):
        fd, bundle_path = tempfile.mkstemp(prefix="xbax-install-", suffix=".zip")
        os.close(fd)
        try:
            with zipfile.ZipFile(bundle_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for local_path, rel_path in bundle_files:
                    archive.write(local_path, arcname=rel_path)
                archive.writestr(
                    REMOTE_INSTALL_MANIFEST,
                    json.dumps(manifest, separators=(",", ":"), sort_keys=True).encode("utf-8"),
                )
            return bundle_path
        except Exception:
            try:
                os.remove(bundle_path)
            except OSError:
                pass
            raise

    def _format_size(self, size_bytes):
        if size_bytes < 1024:
            return f"{size_bytes} B"
        if size_bytes < 1024 * 1024:
            return f"{size_bytes / 1024:.1f} KiB"
        return f"{size_bytes / (1024 * 1024):.1f} MiB"

    def _run_remote_command(self, command):
        stdin, stdout, stderr = self.ssh_client.exec_command(command)
        output = stdout.read().decode("utf-8", errors="ignore")
        error = stderr.read().decode("utf-8", errors="ignore")
        exit_status = stdout.channel.recv_exit_status()
        return exit_status, output, error

    def _powershell_quote(self, value):
        return value.replace("'", "''")

    def _cmd_quote(self, value):
        # cmd.exe quoting: wrap in double quotes and escape any embedded ones.
        return '"' + value.replace('"', '\\"') + '"'

    def _local_bootstrap_tool_path(self, tool_name):
        # Search the actual package layout produced by cmake. Some tools land
        # in <name>/bin/<name>.exe (cleng, sarver, cliant), others land in
        # <name>/<name>.exe (anzipper, cleener, gatter). Try both.
        candidates = [
            os.path.join(LOCAL_PACKAGE_DIR, tool_name, tool_name + ".exe"),
            os.path.join(LOCAL_PACKAGE_DIR, tool_name, "bin", tool_name + ".exe"),
        ]
        for path in candidates:
            if os.path.isfile(path):
                return path
        return candidates[0]

    def _local_anzipper_path(self):
        return self._local_bootstrap_tool_path("anzipper")

    def _remote_bootstrap_tool_path(self, remote_dir, tool_name):
        return self._remote_bootstrap_dir(remote_dir).rstrip("/") + "/" + tool_name + ".exe"

    def _remote_anzipper_path(self, remote_dir):
        return self._remote_bootstrap_tool_path(remote_dir, "anzipper")

    def _remote_cleener_path(self, remote_dir):
        return self._remote_bootstrap_tool_path(remote_dir, "cleener")

    def _ensure_remote_bootstrap_tool(self, remote_dir, tool_name):
        """Upload a bootstrap helper beside the remote bundle if needed.

        The Xbox dev image ships without PowerShell, so we use our own Go
        helper binaries as the canonical bundle bootstrap tools. They are
        part of the package we're installing, but we need them *before* the
        bundle is expanded — hence this out-of-tree bootstrap directory.
        """
        local_tool = self._local_bootstrap_tool_path(tool_name)
        if not os.path.isfile(local_tool):
            raise RuntimeError(
                f"{tool_name}.exe not found at {local_tool}; "
                f"run `cmake --build build --target {tool_name}` first"
            )
        remote_tool = self._remote_bootstrap_tool_path(remote_dir, tool_name)
        remote_bootstrap_dir = self._remote_bootstrap_dir(remote_dir)
        sftp = self.ssh_client.open_sftp()
        try:
            self._remote_mkdirs(sftp, remote_bootstrap_dir)
            local_size = os.path.getsize(local_tool)
            try:
                remote_stat = sftp.stat(remote_tool)
            except IOError:
                remote_stat = None
            if remote_stat is None or remote_stat.st_size != local_size:
                self.log(f"[*] Bootstrapping {tool_name}.exe on the remote machine...")
                sftp.put(local_tool, remote_tool)
        finally:
            sftp.close()
        return remote_tool

    def _ensure_remote_anzipper(self, remote_dir):
        return self._ensure_remote_bootstrap_tool(remote_dir, "anzipper")

    def _ensure_remote_cleener(self, remote_dir):
        return self._ensure_remote_bootstrap_tool(remote_dir, "cleener")

    def _wipe_remote_install_dir(self, remote_dir):
        remote_dir_windows = remote_dir.replace("/", "\\")
        remote_cleener = self._ensure_remote_cleener(remote_dir).replace("/", "\\")
        self.log("[*] Removing existing remote Xbax folder with cleener.exe...")
        command = (
            f'cmd /c if exist {self._cmd_quote(remote_dir_windows)} '
            f'({self._cmd_quote(remote_cleener)} -path {self._cmd_quote(remote_dir_windows)}) '
            f'else (exit /b 0)'
        )
        exit_status, output, error = self._run_remote_command(command)
        if exit_status != 0:
            raise RuntimeError((error or output or "remote cleanup failed").strip())
        if output.strip():
            self.log(f"[*] {output.strip()}")

    def _extract_remote_bundle(self, remote_dir):
        remote_bundle_path = self._remote_bundle_path(remote_dir).replace("/", "\\")
        remote_dir_windows = remote_dir.replace("/", "\\")
        remote_anzipper = self._ensure_remote_anzipper(remote_dir).replace("/", "\\")

        self._wipe_remote_install_dir(remote_dir)

        # anzipper handles directory creation, zip-slip protection, and
        # symlink/irregular-entry rejection. The bundle lives in a sibling
        # bootstrap directory so we can wipe the install root beforehand.
        command = (
            f"{self._cmd_quote(remote_anzipper)} "
            f"-zip {self._cmd_quote(remote_bundle_path)} "
            f"-out {self._cmd_quote(remote_dir_windows)} "
            f"&& del /Q {self._cmd_quote(remote_bundle_path)}"
        )
        exit_status, output, error = self._run_remote_command(command)
        if exit_status != 0:
            raise RuntimeError((error or output or "unknown extraction error").strip())
        if output.strip():
            self.log(f"[*] {output.strip()}")

    def _upload_files_individually(self, remote_dir, changed_files, manifest, skipped, wipe_first=False):
        uploaded = 0
        if wipe_first:
            self._wipe_remote_install_dir(remote_dir)
        sftp = self.ssh_client.open_sftp()
        try:
            self._remote_mkdirs(sftp, remote_dir)
            for index, (local_path, rel_path) in enumerate(changed_files, start=1):
                remote_path = remote_dir.rstrip("/") + "/" + rel_path
                self._remote_mkdirs(sftp, os.path.dirname(remote_path))
                try:
                    sftp.put(local_path, remote_path)
                except Exception as exc:
                    raise RuntimeError(f"{exc} while uploading {rel_path} -> {remote_path}") from exc
                uploaded += 1
                if index == len(changed_files) or index % 25 == 0:
                    self.log(f"[*] Uploaded {index}/{len(changed_files)} file(s) ({skipped} skipped)...")
            self._write_remote_manifest(sftp, remote_dir, manifest)
        finally:
            sftp.close()
        return uploaded, skipped

    def _upload_tree(self, local_dir, remote_dir):
        files, local_manifest, skipped_unsafe = self._collect_local_package_files(local_dir)
        if skipped_unsafe:
            self.log(
                f"[*] Skipping {len(skipped_unsafe)} Windows-incompatible packaged file(s) "
                f"(first: {skipped_unsafe[0]})"
            )
        self.log(f"[*] Scanning {len(files)} packaged file(s) for install sync...")

        sftp = self.ssh_client.open_sftp()
        try:
            self._remote_mkdirs(sftp, remote_dir)
            remote_manifest = self._read_remote_manifest(sftp, remote_dir)
        finally:
            sftp.close()

        changed_files = []
        for local_path, rel_path in files:
            local_entry = local_manifest[rel_path]
            remote_entry = remote_manifest.get(rel_path)
            if self._should_upload_manifest_entry(local_entry, remote_entry):
                changed_files.append((local_path, rel_path))
        skipped = len(files) - len(changed_files)

        if not changed_files:
            self.log(f"[+] Remote install already current. Skipped all {skipped} packaged file(s).")
            return 0, skipped

        bundle_path = self._create_install_bundle(files, local_manifest)
        bundle_size = os.path.getsize(bundle_path)
        remote_bundle_path = self._remote_bundle_path(remote_dir)
        try:
            self.log(
                f"[*] {len(changed_files)} packaged file(s) changed; "
                f"uploading a full clean-install bundle with {len(files)} file(s)."
            )
            self.log(
                f"[*] Uploading bootstrap bundle ({self._format_size(bundle_size)}) "
                f"to refresh {remote_dir} from scratch..."
            )
            sftp = self.ssh_client.open_sftp()
            try:
                self._remote_mkdirs(sftp, self._remote_bootstrap_dir(remote_dir))
                sftp.put(bundle_path, remote_bundle_path)
            finally:
                sftp.close()

            self.log("[*] Expanding install bundle on the remote machine...")
            self._extract_remote_bundle(remote_dir)
            return len(files), 0
        except Exception as exc:
            self.log(f"[*] Bundle install fallback activated: {exc}")
            return self._upload_files_individually(remote_dir, files, local_manifest, 0, wipe_first=True)
        finally:
            try:
                os.remove(bundle_path)
            except OSError:
                pass

    def _open_remote_install_dir(self):
        if not self.connected or not self.sock:
            return
        commands = [
            b"d:\r\n",
            b"cd \\DevelopmentFiles\\Sandbox\\Xbax\r\n",
            b"dir\r\n",
        ]
        for cmd in commands:
            try:
                self.sock.sendall(cmd)
                time.sleep(0.15)
            except Exception:
                break

    def install_package(self):
        if self.installing:
            self.log("[-] Install already running.")
            return
        if not self.has_active_ssh():
            self.log("[-] Connect Dev Shell first to install Xbax.")
            return

        self.installing = True
        self._mark_dirty()
        try:
            self.log("[*] Configuring Xbax package build...")
            self._run_local_command(["cmake", "-S", REPO_ROOT, "-B", BUILD_DIR], REPO_ROOT)
            self.log("[*] Building Xbax package artifacts and host cliant...")
            self._run_local_command(["cmake", "--build", BUILD_DIR, "--target", "package-xbax", "host-cliant", "-j4"], REPO_ROOT)

            if not os.path.isdir(LOCAL_PACKAGE_DIR):
                raise RuntimeError(f"package directory not found: {LOCAL_PACKAGE_DIR}")

            uploaded, skipped = self._upload_tree(LOCAL_PACKAGE_DIR, REMOTE_INSTALL_DIR)
            self.log(f"[+] Install complete. {uploaded} uploaded, {skipped} skipped in {REMOTE_INSTALL_DIR}")
            self._open_remote_install_dir()
        except Exception as exc:
            self.log(f"[-] Install failed: {exc}")
        finally:
            self.installing = False
            self._mark_dirty()

    def start_telnet(self, ip, pin, ssh_client, port=24):
        self.ip = ip
        self.pin = pin
        self.ssh_client = ssh_client
        self.intentional_disconnect = False

        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            configure_keepalive(self.sock)
            self.sock.settimeout(TELNET_CONNECT_TIMEOUT)
            self.sock.connect((ip, port))
            self.sock.settimeout(None)
            try:
                naws = b'\xff\xfb\x1f\xff\xfa\x1f' + struct.pack('!HH', self.cols, self.rows) + b'\xff\xf0'
                self.sock.sendall(naws)
            except: pass

            self.connected = True
            self.retry_count = 0
            self.focused = True
            with self.lock: self.history.append(f"[+] Full SYSTEM shell via raw Telnet ({ip}:{port})")
            self._mark_dirty()

            def reader():
                time.sleep(1.2)
                for cmd in [b"d:\r\n", b"cd \\DevelopmentFiles\r\n",
                             b"if not exist Sandbox mkdir Sandbox\r\n",
                             b"cd Sandbox\r\n", b"cls\r\n"]:
                    try: self.sock.sendall(cmd); time.sleep(0.3)
                    except: break

                while self.connected:
                    try:
                        data = self.sock.recv(4096)
                        if data: self.write(data)
                        else: raise ConnectionError("Empty data")
                    except Exception:
                        self.connected = False
                        if not self.intentional_disconnect and self.ip and self.pin:
                            self.retry_count += 1
                            with self.lock:
                                self.history.append("[-] Connection dropped (Xbox likely killed the process).")
                                self.history.append(f"[*] Auto-reconnecting in 3 seconds... (Attempt {self.retry_count}/5)")
                            self._mark_dirty()

                            if self.retry_count >= 5:
                                def check_and_prompt():
                                    try:
                                        res = requests.get(f"https://{self.ip}:11443/ext/screenshot",
                                                           params={'download': 'false'}, verify=False, timeout=2.0)
                                        network_up = (res.status_code == 200)
                                    except: network_up = False

                                    if network_up:
                                        with self.lock: self.history.append("[-] Max retries reached. Sandbox daemon appears hung.")
                                        self.needs_reboot_prompt = True
                                    else:
                                        with self.lock: self.history.append("[-] Xbox is unreachable on the network.")
                                    self._mark_dirty()
                                threading.Thread(target=check_and_prompt, daemon=True).start()
                            else:
                                time.sleep(3)
                                threading.Thread(target=connect_ssh, args=(self.ip, self.pin, self, False), daemon=True).start()
                        break

            def telnet_keepalive():
                while self.connected and self.sock:
                    time.sleep(TELNET_KEEPALIVE_INTERVAL)
                    try:
                        self.sock.sendall(b"\xff\xf1")
                    except Exception:
                        break

            def ssh_keepalive():
                while self.connected and self.ssh_client:
                    time.sleep(SSH_KEEPALIVE_INTERVAL)
                    try:
                        transport = self.ssh_client.get_transport()
                        if not transport or not transport.is_active():
                            break
                        transport.send_ignore()
                    except Exception:
                        break

            threading.Thread(target=reader, daemon=True).start()
            threading.Thread(target=telnet_keepalive, daemon=True).start()
            threading.Thread(target=ssh_keepalive, daemon=True).start()
            return True
        except Exception as e:
            with self.lock: self.history.append(f"[-] Connection failed: {e}")
            self._mark_dirty()
            return False

    def close(self):
        self.intentional_disconnect = True
        self.connected = False
        if self.is_bs_running():
            try:
                self.stop_bs()
            except Exception:
                pass
        if self.ssh_client and self.ssh_client.get_transport() and self.ssh_client.get_transport().is_active():
            try: self.ssh_client.exec_command("taskkill /F /IM telnetd.exe /T"); time.sleep(0.2)
            except: pass
            try: self.ssh_client.close()
            except: pass
        if self.sock:
            try: self.sock.close()
            except: pass

# ================== VIDEO & INPUT ==================
class FastVideoStream:
    def __init__(self, ip):
        self.url = f"rtsp://{ip}:11442/video/live"
        os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay"
        self.stream = cv2.VideoCapture(self.url, cv2.CAP_FFMPEG)
        self.stream.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.grabbed, self.frame = self.stream.read()
        self.stopped = False
        self.lock = threading.Lock()

    def start(self):
        threading.Thread(target=self.update, daemon=True).start()
        return self

    def update(self):
        while not self.stopped:
            g, f = self.stream.read()
            with self.lock:
                if g: self.frame = f; self.grabbed = g

    def read(self):
        with self.lock: return self.grabbed, self.frame

    def stop(self):
        self.stopped = True
        self.stream.release()

class IMGVideoStream:
    def __init__(self, ip):
        self.url = f"https://{ip}:11443/ext/screenshot"
        self.session = requests.Session()
        self.stopped = False
        self.frame_surf = None
        self.lock = threading.Lock()

    def start(self):
        threading.Thread(target=self.update, daemon=True).start()
        return self

    def update(self):
        while not self.stopped:
            try:
                r = self.session.get(self.url, params={'download':'false','hdr':'false','_':int(time.time()*1000)},
                                     verify=False, timeout=1.2)
                if r.status_code == 200:
                    surf = pygame.image.load(io.BytesIO(r.content))
                    with self.lock: self.frame_surf = surf
            except: pass
            time.sleep(0.033)

    def read(self):
        with self.lock: return self.frame_surf is not None, self.frame_surf

    def stop(self):
        self.stopped = True

class XboxInputClient:
    def __init__(self, ip):
        self.url = f"wss://{ip}:11443/ext/remoteinput"
        self.ws = None
        self.connected = False
        self.queue = queue.Queue()
        self.mouse_pos = None
        self.last_mouse = None
        threading.Thread(target=self._run, daemon=True).start()
        threading.Thread(target=self._process, daemon=True).start()

    def _run(self):
        while True:
            try:
                self.ws = websocket.WebSocketApp(self.url, on_open=self._open, on_error=lambda w,e: None)
                self.ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE})
            except: time.sleep(2)

    def _open(self, ws): self.connected = True

    def _process(self):
        while True:
            while not self.queue.empty():
                p = self.queue.get()
                if self.connected and self.ws:
                    try: self.ws.send(p, opcode=websocket.ABNF.OPCODE_BINARY)
                    except: pass
                self.queue.task_done()
            if self.mouse_pos and self.mouse_pos != self.last_mouse:
                if self.connected and self.ws:
                    x, y = self.mouse_pos
                    p = struct.pack('!B H I I', 0x03, MOUSE_MOVE, x, y)
                    try: self.ws.send(p, opcode=websocket.ABNF.OPCODE_BINARY); self.last_mouse = self.mouse_pos
                    except: pass
            time.sleep(0.008)

    def send_key(self, key, down):
        vk = VK_MAP.get(key)
        if vk is None:
            if 97 <= key <= 122: vk = key - 32
            elif 48 <= key <= 57: vk = key
            else: return
        self.queue.put(bytearray([0x01, vk, 1 if down else 0]))

    def send_mouse(self, action, x, y, wheel=0):
        p = struct.pack('!B H I I', 0x03, action, x, y)
        if action == WHEEL_V: p += struct.pack('!I', wheel & 0xFFFFFFFF)
        self.queue.put(p)

    def update_mouse(self, x, y): self.mouse_pos = (x, y)

def get_xbox_coords(mx, my, active_rect):
    vx, vy, vw, vh = active_rect
    if vw == 0 or vh == 0: return 0, 0
    rx = max(0, min(mx - vx, vw))
    ry = max(0, min(my - vy, vh))
    return int((rx / vw) * 65535), int((ry / vh) * 65535)

def fetch_dev_credentials(ip, timeout=5):
    """Fetch Visual Studio dev-shell credentials from the devkit's web service.

    The console exposes the auto-generated DevToolsUser password at
    `https://<ip>:11443/ext/smb/developerfolder` as JSON of the form:

        {"Path":"D:\\\\DevelopmentFiles","Username":"DevToolsUser","Password":"..."}

    Returns (username, password) on success, or (None, None) on any failure.
    Same trust model as the other 11443 endpoints: TLS verification disabled
    because the devkit ships a self-signed certificate.
    """
    try:
        res = requests.get(f"https://{ip}:11443/ext/smb/developerfolder",
                           verify=False, timeout=timeout)
        res.raise_for_status()
        data = res.json()
        return data.get("Username"), data.get("Password")
    except Exception:
        return None, None

def connect_ssh(ip, pin, terminal, save_on_success=False):
    try:
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(ip, 22, "DevToolsUser", pin, timeout=12)
        ssh.get_transport().set_keepalive(SSH_KEEPALIVE_INTERVAL)

        with terminal.lock: terminal.history.append("[*] Checking for existing telnetd process...")
        terminal._mark_dirty()

        stdin, stdout, stderr = ssh.exec_command('tasklist')
        tasks = stdout.read().decode('utf-8', errors='ignore')

        if "telnetd.exe" not in tasks:
            with terminal.lock: terminal.history.append("[+] Launching new telnetd instance...")
            terminal._mark_dirty()
            ssh.exec_command('devtoolslauncher LaunchForProfiling telnetd "cmd.exe 24"')
            time.sleep(2.8)
        else:
            with terminal.lock: terminal.history.append("[+] telnetd already running. Attaching to existing process...")
            terminal._mark_dirty()

        if save_on_success: save_pin(ip, pin)
        terminal.start_telnet(ip, pin, ssh, 24)

    except paramiko.AuthenticationException:
        with terminal.lock: terminal.history.append("[-] Error: Authentication failed. Re-enter PIN.")
        terminal._mark_dirty()
        terminal.needs_pin_prompt = True
    except Exception as e:
        with terminal.lock: terminal.history.append(f"[-] Error: SSH connection failed: {e}")
        terminal._mark_dirty()
        terminal.needs_pin_prompt = True

# ================== MENU ==================
def run_menu(screen, clock):
    font_big   = pygame.font.SysFont(None, 64)
    font_small = pygame.font.SysFont(None, 24)
    consoles = []
    scanning = True

    def done():
        nonlocal scanning
        scanning = False

    threading.Thread(target=scan_network_async, args=(consoles, done), daemon=True).start()
    refresh_btn = Button(MENU_SIZE[0]//2 - 150, MENU_SIZE[1] - 110, 300, 55, "Refresh Network")

    running = True
    while running:
        screen.fill((28, 28, 35))
        title = font_big.render("Xbox Devkit Launcher", True, (255, 255, 255))
        screen.blit(title, (MENU_SIZE[0]//2 - title.get_width()//2, 60))

        if scanning:
            txt = font_small.render("Scanning network for Dev Mode consoles...", True, (255, 220, 60))
            screen.blit(txt, (MENU_SIZE[0]//2 - txt.get_width()//2, 180))
        else:
            if not consoles:
                txt = font_small.render("No consoles found. Make sure Dev Mode is enabled.", True, (255, 80, 80))
                screen.blit(txt, (MENU_SIZE[0]//2 - txt.get_width()//2, 180))
            else:
                txt = font_small.render("Select your Xbox:", True, (100, 255, 100))
                screen.blit(txt, (MENU_SIZE[0]//2 - txt.get_width()//2, 160))
                for i, (ip, name) in enumerate(consoles):
                    btn = Button(MENU_SIZE[0]//2 - 220, 220 + i*65, 440, 58,
                                 f"{name}   ({ip})", (40, 160, 70), (70, 210, 100))
                    btn.draw(screen)

        refresh_btn.draw(screen)

        for event in pygame.event.get():
            if event.type == pygame.QUIT: return None
            if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if not scanning:
                    if refresh_btn.clicked(pygame.mouse.get_pos()):
                        scanning = True; consoles.clear()
                        threading.Thread(target=scan_network_async, args=(consoles, done), daemon=True).start()
                    else:
                        for i, (ip, name) in enumerate(consoles):
                            btn = Button(MENU_SIZE[0]//2 - 220, 220 + i*65, 440, 58,
                                         f"{name}   ({ip})", (40, 160, 70), (70, 210, 100))
                            if btn.clicked(pygame.mouse.get_pos()): return ip

        pygame.display.flip()
        clock.tick(30)
    return None

# ================== STREAM + TERMINAL ==================
def run_stream(screen, clock, ip):
    global STREAM_SIZE
    pygame.display.set_mode(STREAM_SIZE, pygame.RESIZABLE)
    pygame.display.set_caption(f"Xbox Devkit • {ip} • Live + Full Shell")

    input_client = XboxInputClient(ip)

    video = FastVideoStream(ip)
    if video.grabbed:
        mode = "RTSP"; video.start()
    else:
        video.stop(); mode = "IMG"; video = IMGVideoStream(ip).start()

    # Buttons auto-size their width from text at construction
    shell_btn     = Button(0, 18, 0, 52, "Connect Dev Shell",    (0, 120, 215), (0, 160, 255))
    install_btn   = Button(0, 18, 0, 52, "Install",              (35, 145, 85), (55, 185, 110))
    package_btn   = Button(0, 18, 0, 52, "Package",              (70, 105, 170), (95, 135, 210))
    deploy_btn    = Button(0, 18, 0, 52, "Deploy",               (150, 55, 60),  (195, 80, 90))
    bs_btn        = Button(0, 18, 0, 52, "Start BS",             (170, 95, 25), (205, 125, 40))
    full_term_btn = Button(0, 18, 0, 52, "Toggle Full Terminal", (60, 60, 80),  (90, 90, 110))

    terminal_height = DEFAULT_TERMINAL_HEIGHT
    terminal = IntegratedTerminal(0, STREAM_SIZE[1] - terminal_height, STREAM_SIZE[0], terminal_height)

    vid_rect = (0, HEADER_HEIGHT, STREAM_SIZE[0], STREAM_SIZE[1] - terminal_height - HEADER_HEIGHT)
    target_size = (vid_rect[2], vid_rect[3])
    active_vid_rect = vid_rect
    active_keys = set()
    running = True
    force_resize = True
    separator_rect = pygame.Rect(0, 0, STREAM_SIZE[0], SPLITTER_HEIGHT)
    dragging_separator = False

    prompting_pin = False
    pin_buffer = ""

    dim_surf = pygame.Surface(STREAM_SIZE, pygame.SRCALPHA)
    dim_surf.fill((0, 0, 0, 120))

    # Pre-build overlay fonts (avoids SysFont calls every frame)
    pin_font        = pygame.font.SysFont("consolas", 20)
    reboot_title_f  = pygame.font.SysFont("consolas", 19, bold=True)
    reboot_sub_f    = pygame.font.SysFont("consolas", 17)
    header_font     = pygame.font.SysFont("consolas", 26, bold=True)

    def layout_panels():
        nonlocal vid_rect, target_size, separator_rect, terminal_height, active_vid_rect
        if terminal.fullscreen_mode:
            terminal.rect = pygame.Rect(0, HEADER_HEIGHT, STREAM_SIZE[0], STREAM_SIZE[1] - HEADER_HEIGHT)
            vid_rect = (0, 0, 0, 0)
            target_size = (0, 0)
            separator_rect = pygame.Rect(0, 0, 0, 0)
            active_vid_rect = vid_rect
            return

        available_height = max(HEADER_HEIGHT + MIN_VIDEO_HEIGHT + MIN_TERMINAL_HEIGHT + SPLITTER_HEIGHT, STREAM_SIZE[1]) - HEADER_HEIGHT
        max_terminal_height = max(MIN_TERMINAL_HEIGHT, available_height - MIN_VIDEO_HEIGHT - SPLITTER_HEIGHT)
        terminal_height = max(MIN_TERMINAL_HEIGHT, min(terminal_height, max_terminal_height))
        terminal_top = STREAM_SIZE[1] - terminal_height
        separator_rect = pygame.Rect(0, terminal_top - SPLITTER_HEIGHT, STREAM_SIZE[0], SPLITTER_HEIGHT)
        terminal.rect = pygame.Rect(0, terminal_top, STREAM_SIZE[0], terminal_height)
        vid_rect = (0, HEADER_HEIGHT, STREAM_SIZE[0], max(1, separator_rect.top - HEADER_HEIGHT))
        target_size = (max(1, vid_rect[2]), max(1, vid_rect[3]))
        active_vid_rect = vid_rect

    while running:
        cur_size = screen.get_size()
        if cur_size != STREAM_SIZE or force_resize:
            STREAM_SIZE = cur_size
            dim_surf = pygame.Surface(STREAM_SIZE, pygame.SRCALPHA)
            dim_surf.fill((0, 0, 0, 120))

            layout_panels()

            # Right-align buttons — guaranteed no overlap
            full_term_btn.rect.x = STREAM_SIZE[0] - full_term_btn.rect.w - 12
            bs_btn.rect.x        = full_term_btn.rect.x - bs_btn.rect.w - 10
            deploy_btn.rect.x    = bs_btn.rect.x - deploy_btn.rect.w - 10
            package_btn.rect.x   = deploy_btn.rect.x - package_btn.rect.w - 10
            install_btn.rect.x   = package_btn.rect.x - install_btn.rect.w - 10
            shell_btn.rect.x     = install_btn.rect.x - shell_btn.rect.w - 10

            terminal._mark_dirty()
            force_resize = False
            screen.fill((0, 0, 0))

        if terminal.needs_pin_prompt:
            prompting_pin = True; pin_buffer = ""
            terminal.needs_pin_prompt = False

        install_btn.enabled = terminal.can_install()
        package_btn.enabled = terminal.can_package_appx()
        deploy_btn.enabled = terminal.can_deploy_appx()
        bs_btn.enabled = terminal.can_toggle_bs()
        bs_btn.text = "Stop BS" if terminal.is_bs_running() else "Start BS"

        # ── Video ────────────────────────────────────────────────────────
        if not terminal.fullscreen_mode:
            ret, data = video.read()
            frame_surf = None
            if ret and data is not None:
                if mode == "RTSP":
                    h, w = data.shape[:2]
                    scale = min(target_size[0]/max(1,w), target_size[1]/max(1,h))
                    nw, nh = max(1,int(w*scale)), max(1,int(h*scale))
                    frame = cv2.resize(data, (nw,nh), interpolation=cv2.INTER_LINEAR)
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                    frame_surf = pygame.surfarray.make_surface(np.transpose(frame,(1,0,2)))
                else:
                    w, h = data.get_width(), data.get_height()
                    scale = min(target_size[0]/max(1,w), target_size[1]/max(1,h))
                    nw, nh = max(1,int(w*scale)), max(1,int(h*scale))
                    frame_surf = pygame.transform.scale(data, (nw,nh))

                if frame_surf:
                    ox = vid_rect[0] + (target_size[0]-nw)//2
                    oy = vid_rect[1] + (target_size[1]-nh)//2
                    active_vid_rect = (ox,oy,nw,nh)
                    pygame.draw.rect(screen, (0,0,0), vid_rect)
                    screen.blit(frame_surf, (ox,oy))

        # ── Header ───────────────────────────────────────────────────────
        pygame.draw.rect(screen, (28,28,35), (0,0,STREAM_SIZE[0],HEADER_HEIGHT))
        screen.blit(header_font.render(f"Xbox Devkit • {ip} • {mode} mode", True, (0,200,255)), (20,20))
        shell_btn.draw(screen)
        install_btn.draw(screen)
        package_btn.draw(screen)
        deploy_btn.draw(screen)
        bs_btn.draw(screen)
        full_term_btn.draw(screen)

        if not terminal.fullscreen_mode:
            sep_color = (0, 170, 215) if dragging_separator else (55, 75, 95)
            pygame.draw.rect(screen, sep_color, separator_rect)
            handle = pygame.Rect(0, 0, 120, 4)
            handle.center = separator_rect.center
            pygame.draw.rect(screen, (190, 210, 225), handle, border_radius=3)

        terminal.draw(screen)

        # ── PIN overlay ──────────────────────────────────────────────────
        if prompting_pin:
            ow,oh = 400,150
            ox=(STREAM_SIZE[0]-ow)//2; oy=(STREAM_SIZE[1]-oh)//2
            pygame.draw.rect(screen,(40,40,50),(ox,oy,ow,oh),border_radius=10)
            pygame.draw.rect(screen,(0,120,215),(ox,oy,ow,oh),3,border_radius=10)
            screen.blit(pin_font.render("Enter Visual Studio PIN:",True,(255,255,255)),(ox+20,oy+30))
            cursor = "_" if time.time()%1>0.5 else ""
            screen.blit(pin_font.render(">"+pin_buffer+cursor,True,(0,255,120)),(ox+20,oy+80))

        # ── Reboot overlay ───────────────────────────────────────────────
        if terminal.needs_reboot_prompt:
            ow,oh = 500,200
            ox=(STREAM_SIZE[0]-ow)//2; oy=(STREAM_SIZE[1]-oh)//2
            screen.blit(dim_surf,(0,0))
            pygame.draw.rect(screen,(45,15,15),(ox,oy,ow,oh),border_radius=12)
            pygame.draw.rect(screen,(200,60,60),(ox,oy,ow,oh),3,border_radius=12)
            screen.blit(reboot_title_f.render("!  Xbox Sandbox is hung",True,(255,100,100)),(ox+20,oy+22))
            screen.blit(reboot_sub_f.render("The daemon failed to recover after 5 retries.",True,(200,200,200)),(ox+20,oy+60))
            pygame.draw.line(screen,(80,40,40),(ox+20,oy+95),(ox+ow-20,oy+95),1)
            screen.blit(reboot_sub_f.render("ENTER  —  Reboot console remotely",True,(255,255,255)),(ox+20,oy+112))
            screen.blit(reboot_sub_f.render("ESC    —  Dismiss",True,(130,130,130)),(ox+20,oy+155))

        pygame.display.flip()

        # ── Events ───────────────────────────────────────────────────────
        for event in pygame.event.get():
            if event.type == pygame.QUIT: running = False

            elif event.type == pygame.VIDEORESIZE:
                screen = pygame.display.set_mode((event.w,event.h), pygame.RESIZABLE)
                force_resize = True

            elif event.type == pygame.DROPFILE:
                if terminal.connected:
                    threading.Thread(target=terminal.upload_file, args=(event.file,), daemon=True).start()
                else:
                    with terminal.lock: terminal.history.append("[-] Connect Dev Shell first to upload files.")
                    terminal._mark_dirty()

            # Reboot popup — highest priority, blocks all input below
            if terminal.needs_reboot_prompt:
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        terminal.needs_reboot_prompt = False
                        terminal.log("[-] Reboot dismissed.")
                    elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                        terminal.needs_reboot_prompt = False
                        terminal.retry_count = 0
                        def do_reboot(tgt):
                            try:
                                requests.post(f"https://{tgt}:11443/ext/power?action=reboot", verify=False, timeout=3)
                                terminal.log("[+] Reboot command sent! Waiting for console to restart...")
                            except Exception as ex:
                                terminal.log(f"[-] Reboot failed: {ex}")
                        threading.Thread(target=do_reboot, args=(ip,), daemon=True).start()
                continue

            # PIN popup
            if prompting_pin:
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        prompting_pin = False
                        terminal.log("[-] PIN entry cancelled.")
                    elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                        clean_pin = pin_buffer.strip()
                        if clean_pin:
                            prompting_pin = False
                            terminal.log("[*] Connecting via SSH...")
                            threading.Thread(target=connect_ssh, args=(ip,clean_pin,terminal,True), daemon=True).start()
                            pin_buffer = ""
                        else:
                            terminal.log("[-] PIN required."); prompting_pin = False
                    elif event.key == pygame.K_BACKSPACE:
                        pin_buffer = pin_buffer[:-1]
                    elif event.unicode.isalnum() and len(pin_buffer) < 12:
                        pin_buffer += event.unicode
                continue

            elif event.type == pygame.KEYDOWN and event.key == pygame.K_F8:
                video.stop(); force_resize = True
                if mode == "RTSP": mode="IMG"; video=IMGVideoStream(ip).start()
                else:             mode="RTSP"; video=FastVideoStream(ip).start()
                pygame.display.set_caption(f"Xbox Devkit • {ip} • {mode} mode")

            elif event.type == pygame.MOUSEBUTTONDOWN:
                mx,my = pygame.mouse.get_pos()
                if event.button == 1 and not terminal.fullscreen_mode and separator_rect.collidepoint(mx, my):
                    dragging_separator = True
                    continue
                terminal.focused = terminal.rect.collidepoint(mx,my)
                if event.button == 1:
                    if shell_btn.clicked((mx,my)) and not terminal.connected:
                        # Try to grab the auto-generated DevToolsUser password
                        # from the devkit's developerfolder endpoint. If that
                        # works, skip the manual PIN prompt entirely; otherwise
                        # fall back to the legacy Visual Studio PIN flow.
                        for k in list(active_keys): input_client.send_key(k,False)
                        active_keys.clear()
                        def auto_connect(target_ip):
                            terminal.log("[*] Fetching dev-shell credentials...")
                            user, pwd = fetch_dev_credentials(target_ip)
                            if pwd:
                                terminal.log(f"[+] Got credentials for {user or 'DevToolsUser'}; connecting via SSH...")
                                connect_ssh(target_ip, pwd, terminal, True)
                            else:
                                terminal.log("[-] Could not fetch credentials; falling back to manual PIN.")
                                terminal.needs_pin_prompt = True
                        threading.Thread(target=auto_connect, args=(ip,), daemon=True).start()
                    elif install_btn.clicked((mx,my)):
                        threading.Thread(target=terminal.install_package, daemon=True).start()
                    elif package_btn.clicked((mx,my)):
                        try:
                            source_dir = choose_directory_dialog(
                                "Pick the folder to package into an .appx",
                                initial_dir=REPO_ROOT,
                            )
                            if not source_dir:
                                terminal.log("[*] Package cancelled.")
                                continue
                            output_dir = choose_directory_dialog(
                                "Pick the output folder for the packaged .appx",
                                initial_dir=source_dir,
                            )
                            if not output_dir:
                                terminal.log("[*] Package cancelled.")
                                continue
                        except Exception as exc:
                            terminal.log(f"[-] Folder picker failed: {exc}")
                            continue
                        threading.Thread(
                            target=terminal.package_directory,
                            args=(source_dir, output_dir),
                            daemon=True,
                        ).start()
                    elif deploy_btn.clicked((mx,my)):
                        try:
                            source_dir = choose_directory_dialog(
                                "Pick the folder to package and deploy",
                                initial_dir=REPO_ROOT,
                            )
                            if not source_dir:
                                terminal.log("[*] Deploy cancelled.")
                                continue
                        except Exception as exc:
                            terminal.log(f"[-] Folder picker failed: {exc}")
                            continue
                        threading.Thread(
                            target=terminal.deploy_directory,
                            args=(source_dir, ip),
                            daemon=True,
                        ).start()
                    elif bs_btn.clicked((mx,my)):
                        if terminal.is_bs_running():
                            threading.Thread(target=terminal.stop_bs, daemon=True).start()
                        else:
                            threading.Thread(target=terminal.start_bs, daemon=True).start()
                    elif full_term_btn.clicked((mx,my)):
                        terminal.fullscreen_mode = not terminal.fullscreen_mode
                        force_resize = True

            elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                dragging_separator = False

            elif event.type == pygame.MOUSEMOTION and dragging_separator:
                terminal_height = STREAM_SIZE[1] - max(HEADER_HEIGHT + MIN_VIDEO_HEIGHT + SPLITTER_HEIGHT, min(event.pos[1], STREAM_SIZE[1] - MIN_TERMINAL_HEIGHT))
                force_resize = True
                continue

            if event.type == pygame.KEYDOWN:
                if terminal.focused and terminal.connected:
                    terminal.handle_key(event)
                else:
                    mx,my = pygame.mouse.get_pos()
                    if not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx,my):
                        if event.key not in active_keys:
                            active_keys.add(event.key)
                            input_client.send_key(event.key, True)

            elif event.type == pygame.KEYUP:
                if event.key in active_keys:
                    active_keys.discard(event.key)
                    input_client.send_key(event.key, False)

            elif event.type in (pygame.MOUSEMOTION, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP):
                mx,my = pygame.mouse.get_pos()
                if not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx,my):
                    xbox_x,xbox_y = get_xbox_coords(mx,my,active_vid_rect)
                    if event.type == pygame.MOUSEMOTION:
                        input_client.update_mouse(xbox_x,xbox_y)
                    elif event.type == pygame.MOUSEBUTTONDOWN:
                        if event.button==1: input_client.send_mouse(L_DOWN,xbox_x,xbox_y)
                        elif event.button==2: input_client.send_mouse(M_DOWN,xbox_x,xbox_y)
                        elif event.button==3: input_client.send_mouse(R_DOWN,xbox_x,xbox_y)
                    elif event.type == pygame.MOUSEBUTTONUP:
                        if event.button==1: input_client.send_mouse(L_UP,xbox_x,xbox_y)
                        elif event.button==2: input_client.send_mouse(M_UP,xbox_x,xbox_y)
                        elif event.button==3: input_client.send_mouse(R_UP,xbox_x,xbox_y)

            elif event.type == pygame.MOUSEWHEEL:
                mx,my = pygame.mouse.get_pos()
                if terminal.rect.collidepoint(mx,my):
                    terminal.scroll(-event.y*3)
                elif not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx,my):
                    xbox_x,xbox_y = get_xbox_coords(mx,my,active_vid_rect)
                    input_client.send_mouse(WHEEL_V,xbox_x,xbox_y,event.y*120)

        clock.tick(FPS if mode=="RTSP" else 30)

    video.stop()
    terminal.close()

# ================== MAIN ==================
# ================== CLI ==================
#
# `main.py <subcommand> [...]` runs headlessly without bringing up pygame.
# `main.py` with no arguments still starts the GUI as before.
#
# The CLI reuses the existing IntegratedTerminal install/upload logic by
# subclassing it with a headless variant that skips pygame and prints log
# lines straight to stdout.

CLI_USAGE = """\
Usage:
  main.py                           # launch the GUI (default)
  main.py scan [--timeout S]        # scan the LAN and print discovered devkits
  main.py creds <ip>                # fetch DevToolsUser credentials from <ip>
  main.py exec  <ip> <cmd> [args..] # run a command on <ip> via SSH and print output
  main.py upload <ip> <local> [remote-dir]
                                    # SFTP-upload a single file (default dir: D:/DevelopmentFiles/Sandbox)
  main.py install <ip>              # build the Xbax package and sync it to <ip>
  main.py reboot <ip>               # POST a reboot to <ip>:11443
  main.py -h | --help               # show this message
"""

class HeadlessTerminal(IntegratedTerminal):
    """IntegratedTerminal stripped of pygame; reuses the SSH/install/upload code."""

    def __init__(self):
        # Skip the pygame.Rect/SysFont calls in the base __init__ — none of
        # the methods we reuse from the CLI touch the rendering state.
        self.history = []
        self.screen_history = []
        self.lock = threading.Lock()
        self.cols = 140
        self.rows = 40
        self.grid = [[' ' for _ in range(self.cols)] for _ in range(self.rows)]

        self.cx = 0
        self.cy = 0
        self.input_buffer = ""
        self.command_history = []
        self.command_history_index = None
        self.command_history_draft = ""

        self.sock = None
        self.ssh_client = None
        self.connected = False
        self.intentional_disconnect = False
        self.focused = False
        self.fullscreen_mode = False
        self.raw_input_mode = False
        self.scroll_offset = 0
        self.needs_pin_prompt = False
        self.needs_reboot_prompt = False
        self.retry_count = 0
        self.ip = None
        self.pin = None
        self.installing = False
        self.bs_running = False
        self.bs_busy = False
        self.bs_stop_requested = False
        self.bs_process = None
        self.bs_relay_url = None
        self.bs_lock = threading.Lock()

        self._dirty = False
        self._cached_surf = None
        self._last_focused = None
        self._last_scroll = None
        self._last_rect = None

    def _mark_dirty(self):
        pass

    def log(self, message):
        with self.lock:
            self.history.append(message)
        print(message, flush=True)

    def _open_remote_install_dir(self):
        # No-op in CLI mode — we never have an active telnet session.
        return

def _cli_connect_ssh(ip, timeout=12):
    """Open an SSH connection to <ip> using credentials fetched from the devkit.

    Returns (paramiko.SSHClient, username) or raises RuntimeError.
    """
    user, pwd = fetch_dev_credentials(ip)
    if not pwd:
        raise RuntimeError(f"could not fetch dev credentials from https://{ip}:11443/ext/smb/developerfolder")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        ssh.connect(ip, 22, user or "DevToolsUser", pwd, timeout=timeout)
    except paramiko.AuthenticationException as exc:
        raise RuntimeError(f"SSH authentication failed: {exc}") from exc
    except Exception as exc:
        raise RuntimeError(f"SSH connection failed: {exc}") from exc
    ssh.get_transport().set_keepalive(SSH_KEEPALIVE_INTERVAL)
    return ssh, (user or "DevToolsUser")

def _cli_scan(args):
    timeout = 6.0
    i = 0
    while i < len(args):
        if args[i] in ("--timeout", "-t") and i + 1 < len(args):
            try:
                timeout = float(args[i + 1])
            except ValueError:
                print(f"invalid timeout: {args[i + 1]}", file=sys.stderr)
                return 2
            i += 2
        else:
            print(f"unknown argument: {args[i]}", file=sys.stderr)
            return 2

    consoles = []
    done = threading.Event()
    threading.Thread(
        target=scan_network_async,
        args=(consoles, done.set),
        daemon=True,
    ).start()
    if not done.wait(timeout=timeout):
        print(f"scan still running after {timeout}s; printing partial results", file=sys.stderr)

    if not consoles:
        print("no devkits discovered", file=sys.stderr)
        return 1
    for entry in consoles:
        if isinstance(entry, tuple) and len(entry) >= 2:
            ip, hostname = entry[0], entry[1]
            print(f"{ip:<16} {hostname}")
        elif isinstance(entry, dict):
            print(f"{entry.get('ip','?'):<16} {entry.get('hostname','')}")
        else:
            print(entry)
    return 0

def _cli_creds(args):
    if not args:
        print("missing <ip>", file=sys.stderr); return 2
    ip = args[0]
    user, pwd = fetch_dev_credentials(ip)
    if not pwd:
        print(f"could not fetch credentials from {ip}", file=sys.stderr)
        return 1
    print(json.dumps({"ip": ip, "username": user, "password": pwd}))
    return 0

def _cli_exec(args):
    if len(args) < 2:
        print("usage: main.py exec <ip> <cmd> [args...]", file=sys.stderr); return 2
    ip = args[0]
    command = " ".join(args[1:])
    try:
        ssh, _ = _cli_connect_ssh(ip)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr); return 1
    try:
        _, stdout, stderr = ssh.exec_command(command, timeout=60)
        out = stdout.read().decode("utf-8", errors="replace")
        err = stderr.read().decode("utf-8", errors="replace")
        rc = stdout.channel.recv_exit_status()
        if out:
            sys.stdout.write(out)
        if err:
            sys.stderr.write(err)
        return rc
    finally:
        try: ssh.close()
        except Exception: pass

def _cli_upload(args):
    if len(args) < 2:
        print("usage: main.py upload <ip> <local-file> [remote-dir]", file=sys.stderr); return 2
    ip = args[0]
    local_file = args[1]
    remote_dir = args[2] if len(args) >= 3 else "D:/DevelopmentFiles/Sandbox"
    if not os.path.isfile(local_file):
        print(f"local file not found: {local_file}", file=sys.stderr); return 1
    try:
        ssh, _ = _cli_connect_ssh(ip)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr); return 1
    try:
        term = HeadlessTerminal()
        term.ssh_client = ssh
        term.upload_file(local_file)
        # upload_file logs success/failure via term.log → stdout. Detect
        # failure by scanning the captured history for the "[-] Upload" tag.
        for line in term.history:
            if line.startswith("[-] Upload"):
                return 1
        return 0
    finally:
        try: ssh.close()
        except Exception: pass

def _cli_install(args):
    if not args:
        print("usage: main.py install <ip>", file=sys.stderr); return 2
    ip = args[0]
    try:
        ssh, _ = _cli_connect_ssh(ip)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr); return 1
    try:
        term = HeadlessTerminal()
        term.ssh_client = ssh
        term.connected = True  # has_active_ssh checks transport; install_package also gates on this flag
        term.ip = ip
        term.install_package()
        for line in term.history:
            if line.startswith("[-] Install"):
                return 1
        return 0
    finally:
        try: ssh.close()
        except Exception: pass

def _cli_reboot(args):
    if not args:
        print("usage: main.py reboot <ip>", file=sys.stderr); return 2
    ip = args[0]
    try:
        res = requests.post(f"https://{ip}:11443/ext/power?action=reboot",
                            verify=False, timeout=5)
        if res.status_code >= 400:
            print(f"reboot failed: HTTP {res.status_code}", file=sys.stderr); return 1
        print(f"reboot requested for {ip}")
        return 0
    except Exception as exc:
        print(f"reboot failed: {exc}", file=sys.stderr); return 1

CLI_COMMANDS = {
    "scan":    _cli_scan,
    "creds":   _cli_creds,
    "exec":    _cli_exec,
    "upload":  _cli_upload,
    "install": _cli_install,
    "reboot":  _cli_reboot,
}

def run_cli(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        sys.stdout.write(CLI_USAGE)
        return 0
    cmd = argv[0]
    handler = CLI_COMMANDS.get(cmd)
    if handler is None:
        sys.stderr.write(f"unknown command: {cmd}\n\n{CLI_USAGE}")
        return 2
    return handler(argv[1:])

def main():
    # CLI mode: any positional arg → run a headless command and exit.
    # No args → start the pygame GUI as before.
    # `-h`/`--help`/`help` are also routed to the CLI usage banner.
    if len(sys.argv) > 1 and (
        not sys.argv[1].startswith("-") or sys.argv[1] in ("-h", "--help")
    ):
        sys.exit(run_cli(sys.argv[1:]))

    os.environ['SDL_VIDEO_CENTERED'] = '1'
    pygame.init()
    info = pygame.display.Info()
    screen = pygame.display.set_mode((info.current_w-40, info.current_h-100), pygame.RESIZABLE)
    pygame.display.set_caption("Xbox Devkit Launcher")
    global MENU_SIZE, STREAM_SIZE
    MENU_SIZE = STREAM_SIZE = screen.get_size()
    clock = pygame.time.Clock()
    ip = run_menu(screen, clock)
    if ip: run_stream(screen, clock, ip)
    pygame.quit()

if __name__ == "__main__":
    print("[INFO] Required: pip install pygame opencv-python numpy websocket-client requests urllib3 paramiko")
    main()
