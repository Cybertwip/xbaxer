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

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ================== CONFIG ==================
MENU_SIZE = (1280, 720)
STREAM_SIZE = (1280, 920)   
FPS = 60
CONFIG_FILE = "xbox_config.json"

# Regex to strip ANSI escape sequences
ANSI_ESCAPE = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

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
    def __init__(self, x, y, w, h, text, color=(0, 120, 215), hover=(0, 160, 255)):
        self.font = pygame.font.SysFont("consolas", 20, bold=True)
        self.text = text
        
        # Pre-measure text to prevent overflow & overlapping
        txt_width = self.font.render(text, True, (255, 255, 255)).get_width()
        actual_w = max(w, txt_width + 40) 
        
        self.rect = pygame.Rect(x, y, actual_w, h)
        self.color = color
        self.hover = hover

    def draw(self, surf):
        col = self.hover if self.rect.collidepoint(pygame.mouse.get_pos()) else self.color
        pygame.draw.rect(surf, col, self.rect, border_radius=8)
        txt = self.font.render(self.text, True, (255, 255, 255))
        surf.blit(txt, txt.get_rect(center=self.rect.center))

    def clicked(self, pos):
        return self.rect.collidepoint(pos)

# ================== TRUE VT100 TERMINAL EMULATOR ==================
class IntegratedTerminal:
    def __init__(self, x, y, w, h):
        self.rect = pygame.Rect(x, y, w, h)
        self.font = pygame.font.SysFont("consolas", 17)
        
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
        self.needs_reboot_prompt = False  # <-- NEW: triggers reboot popup
        
        self.retry_count = 0 
        self.ip = None
        self.pin = None

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
            
            if self.connected and self.sock:
                try:
                    naws = b'\xff\xfb\x1f\xff\xfa\x1f' + struct.pack('!HH', self.cols, self.rows) + b'\xff\xf0'
                    self.sock.send(naws)
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

    def clear_screen(self):
        self.grid = [[' ' for _ in range(self.cols)] for _ in range(self.rows)]
        self.cx = 0
        self.cy = 0

    def add_line(self, text):
        clean_text = text.replace('\t', '    ')
        clean_text = ANSI_ESCAPE.sub('', clean_text)
        clean_text = clean_text.replace('\r\n', '\n')

        with self.lock:
            for line in clean_text.split('\n'): 
                if '\r' in line:
                    line = line.split('\r')[-1]
                
                while '\x08' in line:
                    new_line = re.sub(r'[^\x08]\x08', '', line, count=1)
                    if new_line == line: break
                    line = new_line
                line = line.replace('\x08', '') 
                
                line = re.sub(r'[^\x20-\x7E]', '', line)
                line = line.rstrip()

                if line or not self.lines or self.lines[-1] != "":
                    if not self.connected:
                        self.history.append(line)
                    else:
                        self.write(line.encode('utf-8'))
                    
            if len(self.history) > 2000: 
                self.history = self.history[-1500:]

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
                        elif char == '\x08' or char == '\b':
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
        
    def scroll(self, amount):
        max_scroll = max(0, len(self.history))
        self.scroll_offset += amount
        if self.scroll_offset < 0: self.scroll_offset = 0
        if self.scroll_offset > max_scroll: self.scroll_offset = max_scroll

    def handle_key(self, event):
        if event.type != pygame.KEYDOWN: return False
        self.scroll_offset = 0 
        
        # Offline Terminal Logic (Remote Reboot)
        if not self.connected:
            if event.key == pygame.K_RETURN:
                if self.input_buffer.strip() == "REBOOT" and self.ip:
                    try:
                        self.add_line("[*] Sending remote reboot command...")
                        requests.post(f"https://{self.ip}:11443/ext/power?action=reboot", verify=False, timeout=3)
                        self.add_line("[+] Reboot command sent! Wait a few minutes for the console to restart.")
                        self.retry_count = 0
                    except Exception as e:
                        self.add_line(f"[-] Reboot failed: {e}")
                self.input_buffer = ""
                return True
            elif event.key == pygame.K_BACKSPACE:
                self.input_buffer = self.input_buffer[:-1]
                return True
            elif event.unicode and event.unicode.isprintable():
                self.input_buffer += event.unicode
                return True
            return False

        # Toggle Raw Mode via TAB
        if event.key == pygame.K_TAB:
            self.raw_input_mode = not self.raw_input_mode
            return True

        # Process Control Keys (Ctrl+C, Ctrl+D, Ctrl+Z, ESC)
        mods = pygame.key.get_mods()
        ctrl_held = (mods & pygame.KMOD_CTRL) or (mods & pygame.KMOD_META)
        
        if ctrl_held:
            try:
                if event.key == pygame.K_c: self.sock.send(b"\x03")
                elif event.key == pygame.K_d: self.sock.send(b"\x04")
                elif event.key == pygame.K_z: self.sock.send(b"\x1a")
                elif event.key == pygame.K_l: self.sock.send(b"\x0c") # Form feed/clear
            except: pass
            return True

        if event.key == pygame.K_ESCAPE:
            try: self.sock.send(b"\x1b")
            except: pass
            return True
            
        # ANSI Navigation Keys
        if event.key == pygame.K_UP:
            try: self.sock.send(b"\x1b[A")
            except: pass
            return True
        elif event.key == pygame.K_DOWN:
            try: self.sock.send(b"\x1b[B")
            except: pass
            return True
        elif event.key == pygame.K_RIGHT:
            try: self.sock.send(b"\x1b[C")
            except: pass
            return True
        elif event.key == pygame.K_LEFT:
            try: self.sock.send(b"\x1b[D")
            except: pass
            return True

        # Raw Keystroke Transmission
        if self.raw_input_mode:
            try:
                if event.key == pygame.K_RETURN:
                    self.sock.send(b"\r\n")
                elif event.key == pygame.K_BACKSPACE:
                    self.sock.send(b"\x08")
                elif event.unicode:
                    self.sock.send(event.unicode.encode('utf-8'))
            except: pass
            return True
            
        # Buffered Keystroke Transmission (Wait for Enter)
        if event.key == pygame.K_RETURN:
            cmd = (self.input_buffer + "\r\n").encode('utf-8')
            try:
                self.sock.send(cmd)
            except: pass
            self.input_buffer = ""
            return True
        elif event.key == pygame.K_BACKSPACE:
            self.input_buffer = self.input_buffer[:-1]
            return True
        elif event.unicode and event.unicode.isprintable():
            self.input_buffer += event.unicode
            return True
        return False

    def draw(self, screen):
        visible_rows = (self.rect.height - 50) // 19
        if visible_rows != self.rows and visible_rows > 0:
            self.resize_grid(visible_rows)
            
        pygame.draw.rect(screen, (15, 15, 25), self.rect)
        outline_color = (0, 255, 120) if self.focused else (50, 70, 80)
        pygame.draw.rect(screen, outline_color, self.rect, 3)
        
        with self.lock:
            active_lines = [''.join(r).rstrip() for r in self.grid]
            all_lines = self.history + active_lines
            
        start_idx = max(0, len(all_lines) - self.rows - self.scroll_offset)
        end_idx = start_idx + self.rows
        visible_lines = all_lines[start_idx:end_idx]
        
        y = self.rect.y + 10
        for line in visible_lines:
            if line:
                surf = self.font.render(line, True, (0, 255, 120))
                screen.blit(surf, (self.rect.x + 12, y))
            y += 19
        
        footer_y = self.rect.bottom - 30
        
        if not self.connected:
            prompt = "> " + self.input_buffer + ("_" if (self.focused and time.time() % 1 > 0.5) else "")
            ps = self.font.render(prompt, True, (150, 150, 150))
            screen.blit(ps, (self.rect.x + 12, footer_y))
            return

        mode_text = "[RAW INPUT ON]" if self.raw_input_mode else "[BUFFERED INPUT]"
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
            self.add_line("[-] SSH disconnected. Please wait for auto-reconnect.")
            return
            
        filename = os.path.basename(filepath)
        self.add_line(f"[*] Uploading '{filename}' to Sandbox via SFTP...")
        try:
            sftp = self.ssh_client.open_sftp()
            remote_path = f"D:/DevelopmentFiles/Sandbox/{filename}"
            sftp.put(filepath, remote_path)
            sftp.close()
            
            self.add_line(f"[+] '{filename}' uploaded successfully!")
            
            if self.connected:
                self.sock.send(b"dir\r\n")
                
        except Exception as e:
            self.add_line(f"[-] Upload failed: {e}")

    def start_telnet(self, ip, pin, ssh_client, port=24):
        self.ip = ip
        self.pin = pin
        self.ssh_client = ssh_client 
        self.intentional_disconnect = False
        
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            self.sock.settimeout(15)
            self.sock.connect((ip, port))
            
            try:
                naws = b'\xff\xfb\x1f\xff\xfa\x1f' + struct.pack('!HH', self.cols, self.rows) + b'\xff\xf0'
                self.sock.send(naws)
            except: pass

            self.connected = True
            self.focused = True 
            self.add_line(f"[+] Full SYSTEM shell via raw Telnet ({ip}:{port})")
            
            def reader():
                time.sleep(1.2) 
                
                startup_cmds = [
                    b"d:\r\n",
                    b"cd \\DevelopmentFiles\r\n",
                    b"if not exist Sandbox mkdir Sandbox\r\n",
                    b"cd Sandbox\r\n",
                    b"cls\r\n"
                ]
                
                for cmd in startup_cmds:
                    try:
                        self.sock.send(cmd)
                        time.sleep(0.3)
                    except: break
                
                while self.connected:
                    try:
                        data = self.sock.recv(4096)
                        if data:
                            self.write(data)
                        else:
                            raise ConnectionError("Empty data")
                    except Exception as e:
                        self.connected = False
                        if not self.intentional_disconnect and self.ip and self.pin:
                            self.retry_count += 1
                            self.add_line("[-] Connection dropped (Xbox likely killed the process).")
                            self.add_line(f"[*] Auto-reconnecting... (Attempt {self.retry_count}/5)")
                            time.sleep(3)
                            threading.Thread(target=connect_ssh, args=(self.ip, self.pin, self, False), daemon=True).start()
                        break

            def ssh_keepalive():
                while self.connected and self.ssh_client:
                    time.sleep(45)
                    try:
                        self.ssh_client.exec_command("echo 1")
                    except:
                        pass

            threading.Thread(target=reader, daemon=True).start()
            threading.Thread(target=ssh_keepalive, daemon=True).start()
            return True
        except Exception as e:
            self.add_line(f"[-] Connection failed: {e}")
            return False

    def close(self):
        self.intentional_disconnect = True
        self.connected = False
        
        if self.ssh_client and self.ssh_client.get_transport() and self.ssh_client.get_transport().is_active():
            try:
                self.ssh_client.exec_command("taskkill /F /IM telnetd.exe /T")
                time.sleep(0.2)
            except: pass
            try: self.ssh_client.close()
            except: pass

        if self.sock:
            try: self.sock.close()
            except: pass


def connect_ssh(ip, pin, terminal, save_on_success=False):
    if terminal.retry_count >= 5:
        try:
            res = requests.get(f"https://{ip}:11443/ext/screenshot", params={'download': 'false'}, verify=False, timeout=1.5)
            network_up = (res.status_code == 200)
        except:
            network_up = False
            
        if network_up:
            terminal.add_line("[-] Max retries (5) reached. The Xbox Sandbox daemon is hung.")
            terminal.needs_reboot_prompt = True  # <-- CHANGED: trigger popup instead of terminal text
        else:
            terminal.add_line("[-] Network connection is lost. Xbox is unreachable.")
        return

    try:
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(ip, 22, "DevToolsUser", pin, timeout=12)

        ssh.get_transport().set_keepalive(30)
        
        terminal.add_line("[*] Checking for existing telnetd process...")
            
        stdin, stdout, stderr = ssh.exec_command('tasklist')
        tasks = stdout.read().decode('utf-8', errors='ignore')
        
        if "telnetd.exe" not in tasks:
            terminal.add_line("[+] Launching new telnetd instance...")
            ssh.exec_command('devtoolslauncher LaunchForProfiling telnetd "cmd.exe 24"')
            time.sleep(2.8)
        else:
            terminal.add_line("[+] telnetd already running. Attaching to existing process...")
        
        if save_on_success:
            save_pin(ip, pin)

        terminal.retry_count = 0 
        terminal.start_telnet(ip, pin, ssh, 24)
        
    except paramiko.AuthenticationException:
        remove_pin(ip)
        terminal.add_line("[-] Error: Authentication failed. Stored PIN cleared.")
        terminal.needs_pin_prompt = True
        terminal.retry_count = 0 
    except Exception as e:
        terminal.retry_count += 1
        terminal.add_line(f"[-] Error: SSH connection failed: {e}")
        terminal.add_line(f"[*] Auto-reconnecting... (Attempt {terminal.retry_count}/5)")
        time.sleep(3)
        threading.Thread(target=connect_ssh, args=(ip, pin, terminal, False), daemon=True).start()

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
                if g:
                    self.frame = f
                    self.grabbed = g

    def read(self):
        with self.lock:
            return self.grabbed, self.frame

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
                    with self.lock:
                        self.frame_surf = surf
            except:
                pass
            time.sleep(0.033)

    def read(self):
        with self.lock:
            return self.frame_surf is not None, self.frame_surf

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
            except:
                time.sleep(2)

    def _open(self, ws):
        self.connected = True

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
                    try:
                        self.ws.send(p, opcode=websocket.ABNF.OPCODE_BINARY)
                        self.last_mouse = self.mouse_pos
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
        if action == WHEEL_V:
            p += struct.pack('!I', wheel & 0xFFFFFFFF)
        self.queue.put(p)

    def update_mouse(self, x, y):
        self.mouse_pos = (x, y)

def get_xbox_coords(mx, my, active_rect):
    vx, vy, vw, vh = active_rect
    if vw == 0 or vh == 0: return 0, 0
    rx = max(0, min(mx - vx, vw))
    ry = max(0, min(my - vy, vh))
    return int((rx / vw) * 65535), int((ry / vh) * 65535)

# ================== MENU ==================
def run_menu(screen, clock):
    font_big = pygame.font.SysFont(None, 64)
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
            if event.type == pygame.QUIT:
                return None

            if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if not scanning:
                    if refresh_btn.clicked(pygame.mouse.get_pos()):
                        scanning = True
                        consoles.clear()
                        threading.Thread(target=scan_network_async, args=(consoles, done), daemon=True).start()
                    else:
                        for i, (ip, name) in enumerate(consoles):
                            btn = Button(MENU_SIZE[0]//2 - 220, 220 + i*65, 440, 58, 
                                         f"{name}   ({ip})", (40, 160, 70), (70, 210, 100))
                            if btn.clicked(pygame.mouse.get_pos()):
                                return ip

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
        mode = "RTSP"
        video.start()
    else:
        video.stop()
        mode = "IMG"
        video = IMGVideoStream(ip).start()

    shell_btn = Button(STREAM_SIZE[0] - 260, 18, 240, 52, "Connect Dev Shell", (0, 120, 215), (0, 160, 255))
    full_term_btn = Button(STREAM_SIZE[0] - 520, 18, 240, 52, "Toggle Full Terminal", (60, 60, 80), (90, 90, 110))
    
    terminal = IntegratedTerminal(0, STREAM_SIZE[1] - 290, STREAM_SIZE[0], 290)
    terminal.ip = ip 

    vid_rect = (0, 80, STREAM_SIZE[0], STREAM_SIZE[1] - 290 - 80)
    target_size = (vid_rect[2], vid_rect[3])
    active_vid_rect = vid_rect 
    active_keys = set()
    running = True
    force_resize = True
    
    prompting_pin = False
    pin_buffer = ""

    while running:
        cur_size = screen.get_size()
        if cur_size != STREAM_SIZE or force_resize:
            STREAM_SIZE = cur_size
            
            if terminal.fullscreen_mode:
                terminal.rect = pygame.Rect(0, 80, STREAM_SIZE[0], STREAM_SIZE[1] - 80)
                vid_rect = (0, 0, 0, 0)
            else:
                vid_rect = (0, 80, STREAM_SIZE[0], STREAM_SIZE[1] - 290 - 80)
                target_size = (max(1, vid_rect[2]), max(1, vid_rect[3]))
                terminal.rect = pygame.Rect(0, STREAM_SIZE[1] - 290, STREAM_SIZE[0], 290)
                
            # Dynamic button alignment
            shell_btn.rect.x = STREAM_SIZE[0] - shell_btn.rect.w - 20 
            full_term_btn.rect.x = shell_btn.rect.x - full_term_btn.rect.w - 20
            
            force_resize = False
            screen.fill((0,0,0))
            
        if terminal.needs_pin_prompt:
            prompting_pin = True
            pin_buffer = ""
            terminal.needs_pin_prompt = False

        if not terminal.fullscreen_mode:
            ret, data = video.read()
            frame_surf = None
            if ret and data is not None:
                if mode == "RTSP":
                    h, w = data.shape[:2]
                    scale = min(target_size[0] / max(1,w), target_size[1] / max(1,h))
                    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
                    frame = cv2.resize(data, (nw, nh), interpolation=cv2.INTER_LINEAR)
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                    frame = np.transpose(frame, (1, 0, 2))
                    frame_surf = pygame.surfarray.make_surface(frame)
                else:
                    w, h = data.get_width(), data.get_height()
                    scale = min(target_size[0] / max(1,w), target_size[1] / max(1,h))
                    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
                    frame_surf = pygame.transform.scale(data, (nw, nh))

                if frame_surf:
                    ox = vid_rect[0] + (target_size[0] - nw) // 2
                    oy = vid_rect[1] + (target_size[1] - nh) // 2
                    active_vid_rect = (ox, oy, nw, nh)
                    
                    pygame.draw.rect(screen, (0, 0, 0), vid_rect)
                    screen.blit(frame_surf, (ox, oy))

        pygame.draw.rect(screen, (28, 28, 35), (0, 0, STREAM_SIZE[0], 80))
        header = pygame.font.SysFont("consolas", 26, bold=True).render(f"Xbox Devkit • {ip} • {mode} mode", True, (0, 200, 255))
        screen.blit(header, (20, 20))

        shell_btn.draw(screen)
        full_term_btn.draw(screen)
        terminal.draw(screen)

        # ── PIN prompt overlay ──────────────────────────────────────────────
        if prompting_pin:
            overlay_w, overlay_h = 400, 150
            overlay_x, overlay_y = (STREAM_SIZE[0] - overlay_w) // 2, (STREAM_SIZE[1] - overlay_h) // 2
            pygame.draw.rect(screen, (40, 40, 50), (overlay_x, overlay_y, overlay_w, overlay_h), border_radius=10)
            pygame.draw.rect(screen, (0, 120, 215), (overlay_x, overlay_y, overlay_w, overlay_h), 3, border_radius=10)
            
            p_font = pygame.font.SysFont("consolas", 20)
            txt1 = p_font.render("Enter Visual Studio PIN:", True, (255, 255, 255))
            
            cursor = "_" if time.time() % 1 > 0.5 else ""
            txt2 = p_font.render(">" + pin_buffer + cursor, True, (0, 255, 120))
            
            screen.blit(txt1, (overlay_x + 20, overlay_y + 30))
            screen.blit(txt2, (overlay_x + 20, overlay_y + 80))

        # ── Reboot prompt overlay ───────────────────────────────────────────
        if terminal.needs_reboot_prompt:
            overlay_w, overlay_h = 500, 200
            overlay_x = (STREAM_SIZE[0] - overlay_w) // 2
            overlay_y = (STREAM_SIZE[1] - overlay_h) // 2

            # Dim the background slightly
            dim = pygame.Surface((STREAM_SIZE[0], STREAM_SIZE[1]), pygame.SRCALPHA)
            dim.fill((0, 0, 0, 120))
            screen.blit(dim, (0, 0))

            pygame.draw.rect(screen, (45, 15, 15), (overlay_x, overlay_y, overlay_w, overlay_h), border_radius=12)
            pygame.draw.rect(screen, (200, 60, 60), (overlay_x, overlay_y, overlay_w, overlay_h), 3, border_radius=12)

            p_font = pygame.font.SysFont("consolas", 19, bold=True)
            s_font = pygame.font.SysFont("consolas", 17)

            title_surf  = p_font.render("⚠  Xbox Sandbox is hung", True, (255, 100, 100))
            sub_surf    = s_font.render("The daemon failed to recover after 5 retries.", True, (200, 200, 200))
            enter_surf  = s_font.render("ENTER  —  Reboot console remotely", True, (255, 255, 255))
            esc_surf    = s_font.render("ESC    —  Dismiss", True, (130, 130, 130))

            screen.blit(title_surf, (overlay_x + 20, overlay_y + 22))
            screen.blit(sub_surf,   (overlay_x + 20, overlay_y + 60))

            # Divider
            pygame.draw.line(screen, (80, 40, 40),
                             (overlay_x + 20, overlay_y + 95),
                             (overlay_x + overlay_w - 20, overlay_y + 95), 1)

            screen.blit(enter_surf, (overlay_x + 20, overlay_y + 112))
            screen.blit(esc_surf,   (overlay_x + 20, overlay_y + 155))

        pygame.display.flip()

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

            elif event.type == pygame.VIDEORESIZE:
                screen = pygame.display.set_mode((event.w, event.h), pygame.RESIZABLE)
                force_resize = True
                
            elif event.type == pygame.DROPFILE:
                if terminal.connected:
                    filepath = event.file
                    threading.Thread(target=terminal.upload_file, args=(filepath,), daemon=True).start()
                else:
                    terminal.add_line("[-] Connect Dev Shell first to upload files.")

            # ── Reboot popup key handling (highest priority) ────────────────
            if terminal.needs_reboot_prompt:
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        terminal.needs_reboot_prompt = False
                        terminal.add_line("[-] Reboot dismissed.")
                    elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                        terminal.needs_reboot_prompt = False
                        terminal.retry_count = 0
                        try:
                            requests.post(f"https://{ip}:11443/ext/power?action=reboot",
                                          verify=False, timeout=3)
                            terminal.add_line("[+] Reboot command sent! Waiting for console to restart...")
                        except Exception as e:
                            terminal.add_line(f"[-] Reboot failed: {e}")
                continue  # block all other input while popup is up

            # ── PIN popup key handling ──────────────────────────────────────
            if prompting_pin:
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        prompting_pin = False
                        terminal.add_line("[-] PIN entry cancelled.")
                    elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                        clean_pin = pin_buffer.strip()
                        if clean_pin:
                            prompting_pin = False
                            terminal.add_line("[*] Connecting via SSH...")
                            threading.Thread(target=connect_ssh, args=(ip, clean_pin, terminal, True), daemon=True).start()
                            pin_buffer = ""
                        else:
                            terminal.add_line("[-] PIN required.")
                            prompting_pin = False
                    elif event.key == pygame.K_BACKSPACE:
                        pin_buffer = pin_buffer[:-1]
                    else:
                        if event.unicode.isalnum() and len(pin_buffer) < 12:
                            pin_buffer += event.unicode
                continue 

            elif event.type == pygame.KEYDOWN and event.key == pygame.K_F8:
                video.stop()
                force_resize = True
                if mode == "RTSP":
                    mode = "IMG"
                    video = IMGVideoStream(ip).start()
                else:
                    mode = "RTSP"
                    video = FastVideoStream(ip).start()
                pygame.display.set_caption(f"Xbox Devkit • {ip} • {mode} mode")

            elif event.type == pygame.MOUSEBUTTONDOWN:
                mx, my = pygame.mouse.get_pos()
                
                if terminal.rect.collidepoint(mx, my):
                    terminal.focused = True
                else:
                    terminal.focused = False

                if event.button == 1:
                    if shell_btn.clicked((mx, my)) and not terminal.connected:
                        saved_pin = load_pin(ip)
                        if saved_pin:
                            terminal.add_line("[*] Found saved PIN. Authenticating in background...")
                            threading.Thread(target=connect_ssh, args=(ip, saved_pin, terminal, False), daemon=True).start()
                        else:
                            prompting_pin = True
                            pin_buffer = ""
                        for k in list(active_keys):
                            input_client.send_key(k, False)
                        active_keys.clear()
                        
                    elif full_term_btn.clicked((mx, my)):
                        terminal.fullscreen_mode = not terminal.fullscreen_mode
                        force_resize = True
                        
            if event.type == pygame.KEYDOWN:
                if terminal.focused:
                    terminal.handle_key(event)
                else:
                    mx, my = pygame.mouse.get_pos()
                    if not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx, my):
                        if event.key not in active_keys:
                            active_keys.add(event.key)
                            input_client.send_key(event.key, True)
            
            elif event.type == pygame.KEYUP:
                if event.key in active_keys:
                    active_keys.remove(event.key)
                    input_client.send_key(event.key, False)

            elif event.type in (pygame.MOUSEMOTION, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP):
                mx, my = pygame.mouse.get_pos()
                
                if not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx, my):
                    xbox_x, xbox_y = get_xbox_coords(mx, my, active_vid_rect)
                    if event.type == pygame.MOUSEMOTION:
                        input_client.update_mouse(xbox_x, xbox_y)
                    elif event.type == pygame.MOUSEBUTTONDOWN:
                        if event.button == 1: input_client.send_mouse(L_DOWN, xbox_x, xbox_y)
                        elif event.button == 2: input_client.send_mouse(M_DOWN, xbox_x, xbox_y)
                        elif event.button == 3: input_client.send_mouse(R_DOWN, xbox_x, xbox_y)
                    elif event.type == pygame.MOUSEBUTTONUP:
                        if event.button == 1: input_client.send_mouse(L_UP, xbox_x, xbox_y)
                        elif event.button == 2: input_client.send_mouse(M_UP, xbox_x, xbox_y)
                        elif event.button == 3: input_client.send_mouse(R_UP, xbox_x, xbox_y)

            elif event.type == pygame.MOUSEWHEEL:
                mx, my = pygame.mouse.get_pos()
                if terminal.rect.collidepoint(mx, my):
                    terminal.scroll(-event.y * 3) 
                elif not terminal.fullscreen_mode and pygame.Rect(*active_vid_rect).collidepoint(mx, my):
                    xbox_x, xbox_y = get_xbox_coords(mx, my, active_vid_rect)
                    input_client.send_mouse(WHEEL_V, xbox_x, xbox_y, event.y * 120)

        clock.tick(FPS if mode == "RTSP" else 30)

    video.stop()
    terminal.close()

# ================== MAIN ==================
def main():
    os.environ['SDL_VIDEO_CENTERED'] = '1'
    pygame.init()
    
    info = pygame.display.Info()
    start_w = info.current_w - 40
    start_h = info.current_h - 100
    
    screen = pygame.display.set_mode((start_w, start_h), pygame.RESIZABLE)
    pygame.display.set_caption("Xbox Devkit Launcher")
    
    global MENU_SIZE, STREAM_SIZE
    MENU_SIZE = screen.get_size()
    STREAM_SIZE = screen.get_size()
    
    clock = pygame.time.Clock()

    ip = run_menu(screen, clock)
    if ip:
        run_stream(screen, clock, ip)

    pygame.quit()

if __name__ == "__main__":
    print("[INFO] Required: pip install pygame opencv-python numpy websocket-client requests paramiko")
    main()