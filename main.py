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
import io

# Suppress insecure request warnings for self-signed certificates
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# --- Configuration & Constants ---
WIN_SIZE = (1280, 720)
FPS = 60

VK_MAP = {
    pygame.K_BACKSPACE: 0x08, pygame.K_TAB: 0x09, pygame.K_RETURN: 0x0D,
    pygame.K_ESCAPE: 0x1B, pygame.K_SPACE: 0x20, pygame.K_UP: 0x26,
    pygame.K_DOWN: 0x28, pygame.K_LEFT: 0x25, pygame.K_RIGHT: 0x27,
    pygame.K_z: 0xC3, pygame.K_x: 0xC4, # A, B
}

MOUSE_MOVE = 0x0001; L_DOWN = 0x0002; L_UP = 0x0004; R_DOWN = 0x0008
R_UP = 0x0010; M_DOWN = 0x0020; M_UP = 0x0040; WHEEL_V = 0x0800

# --- Network Scanner ---
def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('10.255.255.255', 1))
        ip = s.getsockname()[0]
    except Exception:
        ip = '192.168.0.1'
    finally:
        s.close()
    return ip

def check_xbox(ip):
    url = f"https://{ip}:11443/ext/screenshot"
    try:
        res = requests.get(url, params={'download': 'false'}, verify=False, timeout=0.5)
        if res.status_code == 200:
            return ip
    except requests.exceptions.RequestException:
        pass
    return None

def scan_network_async(result_list, callback):
    local_ip = get_local_ip()
    base_ip = '.'.join(local_ip.split('.')[:-1])
    ips_to_check = [f"{base_ip}.{i}" for i in range(1, 255)]
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=50) as executor:
        results = executor.map(check_xbox, ips_to_check)
        for res in results:
            if res:
                result_list.append(res)
    callback()

# --- UI Components ---
class Button:
    def __init__(self, x, y, w, h, text, color, hover_color, text_color=(255, 255, 255)):
        self.rect = pygame.Rect(x, y, w, h)
        self.text = text
        self.color = color
        self.hover_color = hover_color
        self.text_color = text_color
        self.font = pygame.font.SysFont(None, 36)

    def draw(self, surface):
        mouse_pos = pygame.mouse.get_pos()
        current_color = self.hover_color if self.rect.collidepoint(mouse_pos) else self.color
        pygame.draw.rect(surface, current_color, self.rect, border_radius=5)
        
        text_surf = self.font.render(self.text, True, self.text_color)
        text_rect = text_surf.get_rect(center=self.rect.center)
        surface.blit(text_surf, text_rect)

    def is_clicked(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                return True
        return False

# --- Core Streaming Classes (Video & Input) ---
class FastVideoStream:
    """Handles RTSP H264 Streaming via OpenCV"""
    def __init__(self, ip):
        rtsp_url = f"rtsp://{ip}:11442/video/live"
        os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay"
        self.stream = cv2.VideoCapture(rtsp_url, cv2.CAP_FFMPEG)
        self.stream.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        (self.grabbed, self.frame) = self.stream.read()
        self.stopped = False
        self.lock = threading.Lock()
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.update, args=(), daemon=True)
        self.thread.start()
        return self

    def update(self):
        while not self.stopped:
            if not self.stream.isOpened():
                time.sleep(0.1)
                continue
            grabbed, frame = self.stream.read()
            with self.lock:
                if grabbed:
                    self.frame = frame
                    self.grabbed = grabbed

    def read(self):
        with self.lock:
            return self.grabbed, self.frame

    def stop(self):
        self.stopped = True
        self.stream.release()

class IMGVideoStream:
    """Handles HTTP Polling fallback in a separate thread to prevent input lag"""
    def __init__(self, ip):
        self.url = f"https://{ip}:11443/ext/screenshot"
        self.session = requests.Session()
        self.stopped = False
        self.frame_surface = None
        self.lock = threading.Lock()
        self.grabbed = True
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.update, daemon=True)
        self.thread.start()
        return self

    def update(self):
        while not self.stopped:
            try:
                params = {'download': 'false', 'hdr': 'false', '_': int(time.time() * 1000)}
                res = self.session.get(self.url, params=params, verify=False, timeout=1.0)
                if res.status_code == 200:
                    image_bytes = io.BytesIO(res.content)
                    surf = pygame.image.load(image_bytes)
                    with self.lock:
                        self.frame_surface = surf
            except Exception:
                pass
            time.sleep(0.03) # Cap HTTP requests to ~30 FPS to prevent console overload

    def read(self):
        with self.lock:
            return self.frame_surface is not None, self.frame_surface

    def stop(self):
        self.stopped = True


class XboxInputClient:
    def __init__(self, ip):
        self.url = f"wss://{ip}:11443/ext/remoteinput"
        self.ws = None
        self.connected = False
        self.input_queue = queue.Queue()
        self.latest_mouse_pos = None
        self.last_sent_mouse_pos = None
        
        threading.Thread(target=self._run_forever, daemon=True).start()
        threading.Thread(target=self._process_queue, daemon=True).start()

    def _run_forever(self):
        ssl_opt = {"cert_reqs": ssl.CERT_NONE}
        while True:
            try:
                self.ws = websocket.WebSocketApp(self.url, on_open=self._on_open, on_error=lambda ws, e: None)
                self.ws.run_forever(sslopt=ssl_opt)
            except:
                time.sleep(2)

    def _on_open(self, ws):
        self.connected = True

    def _process_queue(self):
        while True:
            while not self.input_queue.empty():
                payload = self.input_queue.get()
                if self.connected and self.ws:
                    try: self.ws.send(payload, opcode=websocket.ABNF.OPCODE_BINARY)
                    except: pass
                self.input_queue.task_done()
            
            if self.latest_mouse_pos and self.latest_mouse_pos != self.last_sent_mouse_pos:
                if self.connected and self.ws:
                    x, y = self.latest_mouse_pos
                    payload = struct.pack('!B H I I', 0x03, MOUSE_MOVE, x, y)
                    try:
                        self.ws.send(payload, opcode=websocket.ABNF.OPCODE_BINARY)
                        self.last_sent_mouse_pos = self.latest_mouse_pos
                    except: pass
            time.sleep(0.008)

    def send_key(self, key_code, is_down):
        vk = VK_MAP.get(key_code)
        if vk is None:
            if 97 <= key_code <= 122: vk = key_code - 32
            elif 48 <= key_code <= 57: vk = key_code
            else: return
        self.input_queue.put(bytearray([0x01, vk, 0x01 if is_down else 0x00]))

    def send_mouse_click(self, action, x, y, wheel_delta=0):
        payload = struct.pack('!B H I I', 0x03, action, x, y)
        if action == WHEEL_V:
            payload += struct.pack('!I', wheel_delta & 0xFFFFFFFF)
        self.input_queue.put(payload)

    def update_mouse_pos(self, x, y):
        self.latest_mouse_pos = (x, y)

def get_xbox_mouse_coords(mx, my, vid_rect):
    vx, vy, vw, vh = vid_rect
    if vw == 0 or vh == 0: return 0, 0
    rel_x = max(0, min(mx - vx, vw))
    rel_y = max(0, min(my - vy, vh))
    return int((rel_x / vw) * 65535), int((rel_y / vh) * 65535)

# --- Application States ---

def run_menu(screen, clock):
    font_large = pygame.font.SysFont(None, 64)
    font_small = pygame.font.SysFont(None, 24)
    
    found_consoles = []
    is_scanning = True
    
    def on_scan_complete():
        nonlocal is_scanning
        is_scanning = False
        
    threading.Thread(target=scan_network_async, args=(found_consoles, on_scan_complete), daemon=True).start()
    refresh_btn = Button(WIN_SIZE[0]//2 - 150, WIN_SIZE[1] - 100, 300, 50, "Refresh Network", (0, 120, 200), (0, 150, 255))
    console_buttons = []

    running = True
    while running:
        screen.fill((30, 30, 30))
        
        title = font_large.render("Xbox Dev Portal Launcher", True, (255, 255, 255))
        screen.blit(title, (WIN_SIZE[0]//2 - title.get_width()//2, 50))

        if is_scanning:
            status = font_small.render("Scanning local network for Dev Portals...", True, (200, 200, 0))
            screen.blit(status, (WIN_SIZE[0]//2 - status.get_width()//2, 120))
            console_buttons.clear()
        else:
            if not found_consoles:
                status = font_small.render("No consoles found. Ensure Dev Mode is running.", True, (255, 100, 100))
                screen.blit(status, (WIN_SIZE[0]//2 - status.get_width()//2, 120))
            else:
                status = font_small.render("Select a console to connect:", True, (100, 255, 100))
                screen.blit(status, (WIN_SIZE[0]//2 - status.get_width()//2, 120))
                
                console_buttons.clear()
                start_y = 180
                for i, ip in enumerate(found_consoles):
                    btn = Button(WIN_SIZE[0]//2 - 150, start_y + (i * 60), 300, 50, ip, (40, 160, 60), (60, 200, 80))
                    console_buttons.append((ip, btn))

        if not is_scanning:
            refresh_btn.draw(screen)
            for _, btn in console_buttons:
                btn.draw(screen)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return None
            if not is_scanning:
                if refresh_btn.is_clicked(event):
                    is_scanning = True
                    found_consoles.clear()
                    threading.Thread(target=scan_network_async, args=(found_consoles, on_scan_complete), daemon=True).start()
                for ip, btn in console_buttons:
                    if btn.is_clicked(event):
                        return ip

        pygame.display.flip()
        clock.tick(30)


def run_stream(screen, clock, ip):
    input_client = XboxInputClient(ip)
    
    # 1. Fallback Logic: Try RTSP first
    print("Attempting RTSP Connection...")
    video_stream = FastVideoStream(ip)
    
    if video_stream.grabbed:
        current_mode = "RTSP"
        video_stream.start()
    else:
        print("RTSP Failed. Falling back to IMG polling.")
        video_stream.stop()
        current_mode = "IMG"
        video_stream = IMGVideoStream(ip).start()

    def update_caption():
        pygame.display.set_caption(f"Xbox Stream: {ip} | Mode: {current_mode} | Press [F8] to toggle mode")
    
    update_caption()

    active_keys = set()
    running = True
    force_recalc = True
    last_screen_size = screen.get_size()
    vid_rect = (0, 0, WIN_SIZE[0], WIN_SIZE[1])
    target_size = WIN_SIZE

    while running:
        current_screen_size = screen.get_size()
        if current_screen_size != last_screen_size or force_recalc:
            last_screen_size = current_screen_size
            screen.fill((0,0,0)) 
            pygame.display.flip()

        frame_surface = None

        # --- GET & RENDER FRAME ---
        ret, frame_data = video_stream.read()
        if ret and frame_data is not None:
            if current_mode == "RTSP":
                img_h, img_w = frame_data.shape[:2]
                if force_recalc:
                    scale = min(current_screen_size[0] / img_w, current_screen_size[1] / img_h)
                    new_w, new_h = int(img_w * scale), int(img_h * scale)
                    vid_rect = ((current_screen_size[0] - new_w) // 2, (current_screen_size[1] - new_h) // 2, new_w, new_h)
                    target_size = (new_w, new_h)
                    force_recalc = False

                # Hardware OpenCV resize
                frame = cv2.resize(frame_data, target_size, interpolation=cv2.INTER_LINEAR)
                frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                frame = np.transpose(frame, (1, 0, 2))
                frame_surface = pygame.surfarray.make_surface(frame)

            elif current_mode == "IMG":
                img_w, img_h = frame_data.get_size()
                if force_recalc:
                    scale = min(current_screen_size[0] / img_w, current_screen_size[1] / img_h)
                    new_w, new_h = int(img_w * scale), int(img_h * scale)
                    vid_rect = ((current_screen_size[0] - new_w) // 2, (current_screen_size[1] - new_h) // 2, new_w, new_h)
                    target_size = (new_w, new_h)
                    force_recalc = False
                
                # Pygame resize
                frame_surface = pygame.transform.scale(frame_data, target_size)

        if frame_surface:
            screen.blit(frame_surface, (vid_rect[0], vid_rect[1]))
            pygame.display.update(vid_rect)

        # --- EVENT HANDLING ---
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            
            elif event.type == pygame.VIDEORESIZE:
                screen = pygame.display.set_mode((event.w, event.h), pygame.RESIZABLE)
                force_recalc = True
            
            # Key Events
            elif event.type == pygame.KEYDOWN:
                # Dynamic Switching Hotkey (F8)
                if event.key == pygame.K_F8:
                    video_stream.stop()
                    force_recalc = True
                    screen.fill((0,0,0))
                    
                    if current_mode == "RTSP":
                        current_mode = "IMG"
                        video_stream = IMGVideoStream(ip).start()
                    else:
                        current_mode = "RTSP"
                        video_stream = FastVideoStream(ip).start()
                    
                    update_caption()
                    continue # Skip sending F8 to the Xbox

                if event.key not in active_keys:
                    active_keys.add(event.key)
                    input_client.send_key(event.key, True)

            elif event.type == pygame.KEYUP:
                if event.key == pygame.K_F8: continue
                if event.key in active_keys:
                    active_keys.remove(event.key)
                input_client.send_key(event.key, False)
                
            # Mouse Events
            elif event.type in (pygame.MOUSEMOTION, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP, pygame.MOUSEWHEEL):
                mx, my = pygame.mouse.get_pos()
                xbox_x, xbox_y = get_xbox_mouse_coords(mx, my, vid_rect)

                if event.type == pygame.MOUSEMOTION:
                    input_client.update_mouse_pos(xbox_x, xbox_y)
                elif event.type == pygame.MOUSEBUTTONDOWN:
                    if event.button == 1: input_client.send_mouse_click(L_DOWN, xbox_x, xbox_y)
                    elif event.button == 2: input_client.send_mouse_click(M_DOWN, xbox_x, xbox_y)
                    elif event.button == 3: input_client.send_mouse_click(R_DOWN, xbox_x, xbox_y)
                elif event.type == pygame.MOUSEBUTTONUP:
                    if event.button == 1: input_client.send_mouse_click(L_UP, xbox_x, xbox_y)
                    elif event.button == 2: input_client.send_mouse_click(M_UP, xbox_x, xbox_y)
                    elif event.button == 3: input_client.send_mouse_click(R_UP, xbox_x, xbox_y)
                elif event.type == pygame.MOUSEWHEEL:
                    input_client.send_mouse_click(WHEEL_V, xbox_x, xbox_y, wheel_delta=(event.y * 120))

        clock.tick(FPS if current_mode == "RTSP" else 30)

    if video_stream:
        video_stream.stop()

# --- Main Execution ---
def main():
    pygame.init()
    screen = pygame.display.set_mode(WIN_SIZE, pygame.RESIZABLE)
    pygame.display.set_caption("Xbox Dev Launcher")
    clock = pygame.time.Clock()

    target_ip = run_menu(screen, clock)
    
    if target_ip:
        run_stream(screen, clock, target_ip)

    pygame.quit()

if __name__ == "__main__":
    main()