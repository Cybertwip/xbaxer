import pygame
import cv2
import numpy as np
import websocket
import threading
import ssl
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
REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(REPO_ROOT, "build")
LOCAL_PACKAGE_DIR = os.path.join(BUILD_DIR, "package", "Xbax")
REMOTE_INSTALL_DIR = "D:/DevelopmentFiles/Xbax"

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

        self.cx = 0
        self.cy = 0

        self.input_buffer = ""
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

        # Dirty-cache: only re-render the body surface when content changes
        self._dirty = True
        self._cached_surf = None
        self._last_focused = None
        self._last_scroll = None
        self._last_rect = None

    def _mark_dirty(self):
        self._dirty = True

    def log(self, message):
        with self.lock:
            self.history.append(message)
            if len(self.history) > 2500:
                self.history = self.history[-1800:]
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

    def has_active_ssh(self):
        transport = self.ssh_client.get_transport() if self.ssh_client else None
        return bool(transport and transport.is_active())

    def can_install(self):
        return self.connected and self.has_active_ssh() and not self.installing

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
            self.history.append(top_line)
        if len(self.history) > 2000:
            self.history = self.history[-1500:]
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
        max_scroll = max(0, len(self.history))
        new_offset = max(0, min(self.scroll_offset + amount, max_scroll))
        if new_offset != self.scroll_offset:
            self.scroll_offset = new_offset
            self._mark_dirty()

    def handle_key(self, event):
        if not self.connected or event.type != pygame.KEYDOWN: return False
        self.scroll_offset = 0
        ctrl_pressed = bool(event.mod & pygame.KMOD_CTRL)

        if ctrl_pressed and event.key == pygame.K_c:
            self.input_buffer = ""
            self._mark_dirty()
            return self._send_shell_bytes(b"\x03", "[*] Sent Ctrl+C interrupt to remote shell.")

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
            cmd = (self.input_buffer + "\r\n").encode('utf-8')
            try: self.sock.sendall(cmd)
            except: pass
            self.input_buffer = ""
            self._mark_dirty()
            return True
        elif event.key == pygame.K_BACKSPACE:
            self.input_buffer = self.input_buffer[:-1]
            self._mark_dirty()
            return True
        elif event.unicode and event.unicode.isprintable():
            self.input_buffer += event.unicode
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
                active_lines = [''.join(r).rstrip() for r in self.grid]
                all_lines = self.history + active_lines

            start_idx = max(0, len(all_lines) - self.rows - self.scroll_offset)
            visible_lines = all_lines[start_idx: start_idx + self.rows]

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
        with self.lock: self.history.append(f"[*] Uploading '{filename}' to Sandbox via SFTP...")
        self._mark_dirty()
        try:
            sftp = self.ssh_client.open_sftp()
            sftp.put(filepath, f"D:/DevelopmentFiles/Sandbox/{filename}")
            sftp.close()
            with self.lock: self.history.append(f"[+] '{filename}' uploaded successfully!")
            self._mark_dirty()
            if self.connected: self.sock.sendall(b"dir\r\n")
        except Exception as e:
            with self.lock: self.history.append(f"[-] Upload failed: {e}")
            self._mark_dirty()

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

    def _remote_file_is_current(self, sftp, local_path, remote_path):
        remote_stat = self._remote_path_exists(sftp, remote_path)
        if remote_stat is None:
            return False

        local_stat = os.stat(local_path)
        if remote_stat.st_size != local_stat.st_size:
            return False

        local_mtime = int(local_stat.st_mtime)
        remote_mtime = int(getattr(remote_stat, "st_mtime", 0))
        return remote_mtime >= local_mtime

    def _upload_tree(self, local_dir, remote_dir):
        files = []
        for root, _, filenames in os.walk(local_dir):
            for filename in filenames:
                local_path = os.path.join(root, filename)
                rel_path = os.path.relpath(local_path, local_dir)
                remote_path = remote_dir.rstrip("/") + "/" + rel_path.replace(os.sep, "/")
                files.append((local_path, remote_path))

        self.log(f"[*] Syncing {len(files)} packaged file(s) to {remote_dir}...")
        sftp = self.ssh_client.open_sftp()
        uploaded = 0
        skipped = 0
        try:
            self._remote_mkdirs(sftp, remote_dir)
            for index, (local_path, remote_path) in enumerate(files, start=1):
                self._remote_mkdirs(sftp, os.path.dirname(remote_path))
                if self._remote_file_is_current(sftp, local_path, remote_path):
                    skipped += 1
                else:
                    sftp.put(local_path, remote_path)
                    uploaded += 1
                if index == len(files) or index % 25 == 0:
                    self.log(f"[*] Processed {index}/{len(files)} files ({uploaded} uploaded, {skipped} skipped)...")
        finally:
            sftp.close()
        return uploaded, skipped

    def _open_remote_install_dir(self):
        if not self.connected or not self.sock:
            return
        commands = [
            b"d:\r\n",
            b"cd \\DevelopmentFiles\\Xbax\r\n",
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
        remove_pin(ip)
        with terminal.lock: terminal.history.append("[-] Error: Authentication failed. Stored PIN cleared.")
        terminal._mark_dirty()
        terminal.needs_pin_prompt = True
    except Exception as e:
        remove_pin(ip)
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
            install_btn.rect.x   = full_term_btn.rect.x - install_btn.rect.w - 10
            shell_btn.rect.x     = install_btn.rect.x - shell_btn.rect.w - 10

            terminal._mark_dirty()
            force_resize = False
            screen.fill((0, 0, 0))

        if terminal.needs_pin_prompt:
            prompting_pin = True; pin_buffer = ""
            terminal.needs_pin_prompt = False

        install_btn.enabled = terminal.can_install()

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
                        saved_pin = load_pin(ip)
                        if saved_pin:
                            terminal.log("[*] Found saved PIN. Authenticating in background...")
                            threading.Thread(target=connect_ssh, args=(ip,saved_pin,terminal,False), daemon=True).start()
                        else:
                            prompting_pin = True; pin_buffer = ""
                        for k in list(active_keys): input_client.send_key(k,False)
                        active_keys.clear()
                    elif install_btn.clicked((mx,my)):
                        threading.Thread(target=terminal.install_package, daemon=True).start()
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
def main():
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
    print("[INFO] Required: pip install pygame opencv-python numpy websocket-client requests paramiko")
    main()
