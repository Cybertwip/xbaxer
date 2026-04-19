package main

import (
	"fmt"
	"log"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	CS_VREDRAW = 0x0001
	CS_HREDRAW = 0x0002

	WS_POPUP   = 0x80000000
	WS_VISIBLE = 0x10000000

	CW_USEDEFAULT = ^uintptr(0x7fffffff)

	SW_SHOWDEFAULT = 10

	WM_CREATE      = 0x0001
	WM_DESTROY     = 0x0002
	WM_QUIT        = 0x0012
	WM_ACTIVATEAPP = 0x001C
	WM_KEYDOWN     = 0x0100
	WM_USER        = 0x0400

	PM_REMOVE = 0x0001

	IDC_ARROW = 32512

	COLOR_WINDOW = 5

	GWLP_USERDATA = ^uintptr(20) // -21 on 64-bit

	EVENT_MODIFY_STATE = 0x0002
	SYNCHRONIZE        = 0x00100000
	INFINITE           = 0xFFFFFFFF
	WAIT_FAILED        = 0xFFFFFFFF

	VK_ESCAPE = 0x1B

	windowWidth  = 1920
	windowHeight = 1080
)

const (
	windowClassName = "OkaneizerReferenceWindowClass"
	windowTitle     = "Okaneizer Reference"
)

type (
	HINSTANCE windows.Handle
	HWND      windows.Handle
	HCURSOR   windows.Handle
	HBRUSH    windows.Handle
)

type POINT struct {
	X int32
	Y int32
}

type MSG struct {
	HWnd     HWND
	Message  uint32
	WParam   uintptr
	LParam   uintptr
	Time     uint32
	Pt       POINT
	LPrivate uint32
}

type RECT struct {
	Left   int32
	Top    int32
	Right  int32
	Bottom int32
}

type WNDCLASSEX struct {
	CbSize        uint32
	Style         uint32
	LpfnWndProc   uintptr
	CbClsExtra    int32
	CbWndExtra    int32
	HInstance     HINSTANCE
	HIcon         windows.Handle
	HCursor       HCURSOR
	HbrBackground HBRUSH
	LpszMenuName  *uint16
	LpszClassName *uint16
	HIconSm       windows.Handle
}

type CREATESTRUCTW struct {
	LpCreateParams uintptr
	HInstance      HINSTANCE
	HMenu          windows.Handle
	HwndParent     HWND
	Cy             int32
	Cx             int32
	Y              int32
	X              int32
	Style          int32
	LpszName       *uint16
	LpszClass      *uint16
	ExStyle        uint32
}

type procCandidate struct {
	dll  string
	proc string
}

type compatProc struct {
	logicalName  string
	candidates   []procCandidate
	resolved     *windows.LazyProc
	resolvedDLL  string
	resolvedProc string
}

type Game struct {
	hwnd      HWND
	width     int32
	height    int32
	frame     uint64
	active    bool
	suspended bool
}

func candidate(dllName, procName string) procCandidate {
	return procCandidate{dll: dllName, proc: procName}
}

func newCompatProc(logicalName string, candidates ...procCandidate) *compatProc {
	return &compatProc{
		logicalName: logicalName,
		candidates:  candidates,
	}
}

func (p *compatProc) Find() error {
	if p.resolved != nil {
		return nil
	}

	var attempts []string
	for _, option := range p.candidates {
		dll := windows.NewLazySystemDLL(option.dll)
		proc := dll.NewProc(option.proc)

		if err := dll.Load(); err != nil {
			attempts = append(attempts, fmt.Sprintf("%s!%s load=%v", option.dll, option.proc, err))
			continue
		}
		if err := proc.Find(); err != nil {
			attempts = append(attempts, fmt.Sprintf("%s!%s find=%v", option.dll, option.proc, err))
			continue
		}

		p.resolved = proc
		p.resolvedDLL = option.dll
		p.resolvedProc = option.proc
		log.Printf("%s resolved via %s", p.logicalName, p.Source())
		return nil
	}

	return fmt.Errorf("%s: no compatible export found (%s)", p.logicalName, strings.Join(attempts, "; "))
}

func (p *compatProc) Call(args ...uintptr) (uintptr, uintptr, error) {
	if err := p.Find(); err != nil {
		return 0, 0, err
	}
	return p.resolved.Call(args...)
}

func (p *compatProc) Source() string {
	if p.resolved == nil {
		return "unresolved"
	}
	return p.resolvedDLL + "!" + p.resolvedProc
}

var (
	procOutputDebugStringA = newCompatProc(
		"OutputDebugStringA",
		candidate("api-ms-win-core-debug-l1-1-0.dll", "OutputDebugStringA"),
		candidate("kernel32.dll", "OutputDebugStringA"),
	)
	procCreateWindowExW = newCompatProc(
		"CreateWindowExW",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "CreateWindowExW"),
		candidate("user32.dll", "CreateWindowExW"),
	)
	procDefWindowProcW = newCompatProc(
		"DefWindowProcW",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "DefWindowProcW"),
		candidate("user32.dll", "DefWindowProcW"),
	)
	procShowWindow = newCompatProc(
		"ShowWindow",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "ShowWindow"),
		candidate("user32.dll", "ShowWindow"),
	)
	procPeekMessageW = newCompatProc(
		"PeekMessageW",
		candidate("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PeekMessageW"),
		candidate("user32.dll", "PeekMessageW"),
	)
	procTranslateMessage = newCompatProc(
		"TranslateMessage",
		candidate("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "TranslateMessage"),
		candidate("user32.dll", "TranslateMessage"),
	)
	procDispatchMessageW = newCompatProc(
		"DispatchMessageW",
		candidate("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "DispatchMessageW"),
		candidate("user32.dll", "DispatchMessageW"),
	)
	procPostMessageW = newCompatProc(
		"PostMessageW",
		candidate("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PostMessageW"),
		candidate("user32.dll", "PostMessageW"),
	)
	procPostQuitMessage = newCompatProc(
		"PostQuitMessage",
		candidate("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PostQuitMessage"),
		candidate("user32.dll", "PostQuitMessage"),
	)
	procRegisterClassExW = newCompatProc(
		"RegisterClassExW",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "RegisterClassExW"),
		candidate("user32.dll", "RegisterClassExW"),
	)
	procLoadCursorW = newCompatProc(
		"LoadCursorW",
		candidate("ext-ms-win-rtcore-ntuser-cursor-l1-1-0.dll", "LoadCursorW"),
		candidate("user32.dll", "LoadCursorW"),
	)
	procGetClientRect = newCompatProc(
		"GetClientRect",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "GetClientRect"),
		candidate("user32.dll", "GetClientRect"),
	)
	procSetWindowLongPtrW = newCompatProc(
		"SetWindowLongPtrW",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "SetWindowLongPtrW"),
		candidate("user32.dll", "SetWindowLongPtrW"),
	)
	procGetWindowLongPtrW = newCompatProc(
		"GetWindowLongPtrW",
		candidate("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "GetWindowLongPtrW"),
		candidate("user32.dll", "GetWindowLongPtrW"),
	)
	procCreateEventExW = newCompatProc(
		"CreateEventExW",
		candidate("api-ms-win-core-synch-l1-1-0.dll", "CreateEventExW"),
		candidate("kernel32.dll", "CreateEventExW"),
	)
	procResetEvent = newCompatProc(
		"ResetEvent",
		candidate("api-ms-win-core-synch-l1-1-0.dll", "ResetEvent"),
		candidate("kernel32.dll", "ResetEvent"),
	)
	procSetEvent = newCompatProc(
		"SetEvent",
		candidate("api-ms-win-core-synch-l1-1-0.dll", "SetEvent"),
		candidate("kernel32.dll", "SetEvent"),
	)
	procWaitForSingleObject = newCompatProc(
		"WaitForSingleObject",
		candidate("api-ms-win-core-synch-l1-1-0.dll", "WaitForSingleObject"),
		candidate("kernel32.dll", "WaitForSingleObject"),
	)
	procCloseHandle = newCompatProc(
		"CloseHandle",
		candidate("api-ms-win-core-handle-l1-1-0.dll", "CloseHandle"),
		candidate("kernel32.dll", "CloseHandle"),
	)
	procGetModuleHandleW = newCompatProc(
		"GetModuleHandleW",
		candidate("api-ms-win-core-libraryloader-l1-2-0.dll", "GetModuleHandleW"),
		candidate("kernel32.dll", "GetModuleHandleW"),
	)
	procRegisterAppStateChangeNotification = newCompatProc(
		"RegisterAppStateChangeNotification",
		candidate("api-ms-win-core-psm-appnotify-l1-1-0.dll", "RegisterAppStateChangeNotification"),
	)
	procUnregisterAppStateChangeNotification = newCompatProc(
		"UnregisterAppStateChangeNotification",
		candidate("api-ms-win-core-psm-appnotify-l1-1-0.dll", "UnregisterAppStateChangeNotification"),
	)
)

var (
	mainWndProc             = syscall.NewCallback(wndProc)
	appStateChangeCallback  = syscall.NewCallback(appStateChangeHandler)
	nextGameID       uint64
	gameRegistry     sync.Map
	plmSuspendHandle windows.Handle
	plmResumeHandle  windows.Handle
	plmRegistration  uintptr
)

func main() {
	runtime.LockOSThread()

	if err := run(); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}

func tracef(format string, args ...any) {
	msg := fmt.Sprintf(format, args...)
	log.Print(strings.TrimRight(msg, "\r\n"))

	if err := procOutputDebugStringA.Find(); err == nil {
		if buffer, bufferErr := windows.BytePtrFromString(msg); bufferErr == nil {
			_, _, _ = procOutputDebugStringA.resolved.Call(uintptr(unsafe.Pointer(buffer)))
		}
	}
}

func traceCall(name string, p *compatProc, args ...uintptr) (uintptr, uintptr, error) {
	r1, r2, err := p.Call(args...)
	tracef("[okane][Main] %s via %s -> r1=0x%X r2=0x%X err=%v\n", name, p.Source(), r1, r2, err)
	return r1, r2, err
}

func run() error {
	instance, err := getModuleHandle()
	if err != nil {
		return err
	}

	tracef("[okane][Main] SampleMain begin hInstance=0x%X nCmdShow=%d\n", uintptr(instance), SW_SHOWDEFAULT)

	game := &Game{}
	tracef("[okane][Main] Game allocated ptr=%p\n", game)

	gameID := registerGame(game)
	defer unregisterGame(gameID)

	if err := registerWindowClass(instance); err != nil {
		return err
	}

	hwnd, err := createMainWindow(instance, gameID)
	if err != nil {
		return err
	}

	tracef("[okane][Main] CreateWindowExW hwnd=0x%X size=%dx%d\n", uintptr(hwnd), windowWidth, windowHeight)

	_, _, _ = traceCall("ShowWindow", procShowWindow, uintptr(hwnd), SW_SHOWDEFAULT)
	tracef("[okane][Main] ShowWindow Xbox SW_SHOWDEFAULT\n")

	clientWidth, clientHeight, err := getClientSize(hwnd)
	if err != nil {
		return err
	}

	tracef("[okane][Main] Initialize begin client=%dx%d\n", clientWidth, clientHeight)
	game.Initialize(hwnd, clientWidth, clientHeight)
	tracef("[okane][Main] Initialize finished\n")

	startupMessageCount := pumpStartupMessages()
	tracef("[okane][Main] startup message pump complete count=%d\n", startupMessageCount)

	tracef("[okane][Main] bootstrap Tick begin\n")
	game.Tick()
	tracef("[okane][Main] bootstrap Tick end\n")

	if err := initializePLM(hwnd); err != nil {
		return err
	}
	defer shutdownPLM()

	var msg MSG
	var idleTicks uint32
	for msg.Message != WM_QUIT {
		if peekMessage(&msg) {
			tracef(
				"[okane][Main] loop message=0x%04X wParam=0x%X lParam=0x%X\n",
				msg.Message,
				msg.WParam,
				msg.LParam,
			)
			_, _, _ = procTranslateMessage.Call(uintptr(unsafe.Pointer(&msg)))
			_, _, _ = procDispatchMessageW.Call(uintptr(unsafe.Pointer(&msg)))
			continue
		}

		if idleTicks < 5 {
			tracef("[okane][Main] idle tick #%d\n", idleTicks)
		}
		idleTicks++
		game.Tick()
	}

	tracef("[okane][Main] message loop exiting wParam=%d\n", msg.WParam)
	tracef("[okane][Main] Game destroyed\n")
	return nil
}

func registerWindowClass(instance HINSTANCE) error {
	className := syscall.StringToUTF16Ptr(windowClassName)

	cursor, _, err := traceCall("LoadCursorW", procLoadCursorW, 0, uintptr(IDC_ARROW))
	if cursor == 0 {
		return fmt.Errorf("LoadCursorW failed: %w", err)
	}

	wcex := WNDCLASSEX{
		CbSize:        uint32(unsafe.Sizeof(WNDCLASSEX{})),
		Style:         CS_HREDRAW | CS_VREDRAW,
		LpfnWndProc:   mainWndProc,
		HInstance:     instance,
		HCursor:       HCURSOR(cursor),
		HbrBackground: HBRUSH(COLOR_WINDOW + 1),
		LpszClassName: className,
	}

	r1, _, err := traceCall("RegisterClassExW", procRegisterClassExW, uintptr(unsafe.Pointer(&wcex)))
	if r1 == 0 {
		return fmt.Errorf("RegisterClassExW failed: %w", err)
	}

	tracef("[okane][Main] RegisterClassExW succeeded class=%s\n", windowClassName)
	return nil
}

func createMainWindow(instance HINSTANCE, gameID uintptr) (HWND, error) {
	className := syscall.StringToUTF16Ptr(windowClassName)
	title := syscall.StringToUTF16Ptr(windowTitle)

	r1, _, err := traceCall(
		"CreateWindowExW",
		procCreateWindowExW,
		0,
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(title)),
		uintptr(WS_POPUP|WS_VISIBLE),
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowWidth,
		windowHeight,
		0,
		0,
		uintptr(instance),
		gameID,
	)
	if r1 == 0 {
		return 0, fmt.Errorf("CreateWindowExW failed: %w", err)
	}

	return HWND(r1), nil
}

func getClientSize(hwnd HWND) (int32, int32, error) {
	var rc RECT
	r1, _, err := traceCall("GetClientRect", procGetClientRect, uintptr(hwnd), uintptr(unsafe.Pointer(&rc)))
	if r1 == 0 {
		return 0, 0, fmt.Errorf("GetClientRect failed: %w", err)
	}
	return rc.Right - rc.Left, rc.Bottom - rc.Top, nil
}

func pumpStartupMessages() uint32 {
	var msg MSG
	var count uint32
	for peekMessage(&msg) {
		count++
		tracef(
			"[okane][Main] startup message=0x%04X wParam=0x%X lParam=0x%X\n",
			msg.Message,
			msg.WParam,
			msg.LParam,
		)
		_, _, _ = procTranslateMessage.Call(uintptr(unsafe.Pointer(&msg)))
		_, _, _ = procDispatchMessageW.Call(uintptr(unsafe.Pointer(&msg)))
	}
	return count
}

func peekMessage(msg *MSG) bool {
	r1, _, _ := procPeekMessageW.Call(
		uintptr(unsafe.Pointer(msg)),
		0,
		0,
		0,
		PM_REMOVE,
	)
	return r1 != 0
}

func initializePLM(hwnd HWND) error {
	if err := procRegisterAppStateChangeNotification.Find(); err != nil {
		tracef("[okane][Main] RegisterAppStateChangeNotification unavailable: %v\n", err)
		return nil
	}

	suspendHandle, _, err := traceCall(
		"CreateEventExW",
		procCreateEventExW,
		0,
		0,
		0,
		EVENT_MODIFY_STATE|SYNCHRONIZE,
	)
	if suspendHandle == 0 {
		return fmt.Errorf("CreateEventExW(suspend) failed: %w", err)
	}

	resumeHandle, _, err := traceCall(
		"CreateEventExW",
		procCreateEventExW,
		0,
		0,
		0,
		EVENT_MODIFY_STATE|SYNCHRONIZE,
	)
	if resumeHandle == 0 {
		_, _, _ = procCloseHandle.Call(suspendHandle)
		return fmt.Errorf("CreateEventExW(resume) failed: %w", err)
	}

	plmSuspendHandle = windows.Handle(suspendHandle)
	plmResumeHandle = windows.Handle(resumeHandle)
	tracef(
		"[okane][Main] PLM events created suspend=0x%X resume=0x%X\n",
		uintptr(plmSuspendHandle),
		uintptr(plmResumeHandle),
	)

	var registration uintptr
	r1, _, err := traceCall(
		"RegisterAppStateChangeNotification",
		procRegisterAppStateChangeNotification,
		appStateChangeCallback,
		uintptr(hwnd),
		uintptr(unsafe.Pointer(&registration)),
	)
	if r1 != 0 {
		shutdownPLM()
		return fmt.Errorf("RegisterAppStateChangeNotification failed: %w", err)
	}

	plmRegistration = registration
	tracef("[okane][Main] RegisterAppStateChangeNotification succeeded\n")
	return nil
}

func shutdownPLM() {
	if plmRegistration != 0 {
		_, _, _ = traceCall(
			"UnregisterAppStateChangeNotification",
			procUnregisterAppStateChangeNotification,
			plmRegistration,
		)
		plmRegistration = 0
	}

	if plmSuspendHandle != 0 {
		_, _, _ = traceCall("CloseHandle", procCloseHandle, uintptr(plmSuspendHandle))
		plmSuspendHandle = 0
	}

	if plmResumeHandle != 0 {
		_, _, _ = traceCall("CloseHandle", procCloseHandle, uintptr(plmResumeHandle))
		plmResumeHandle = 0
	}
}

func appStateChangeHandler(quiesced uintptr, context uintptr) uintptr {
	tracef(
		"[okane][Main] AppStateChangeNotification quiesced=%d hwnd=0x%X\n",
		boolToInt(quiesced != 0),
		context,
	)

	if quiesced != 0 {
		_, _, _ = procResetEvent.Call(uintptr(plmSuspendHandle))
		_, _, _ = procResetEvent.Call(uintptr(plmResumeHandle))
		_, _, _ = procPostMessageW.Call(context, WM_USER, 0, 0)
		_, _, _ = procWaitForSingleObject.Call(uintptr(plmSuspendHandle), INFINITE)
	} else {
		_, _, _ = procSetEvent.Call(uintptr(plmResumeHandle))
	}

	return 0
}

func wndProc(hwnd uintptr, message uint32, wParam, lParam uintptr) uintptr {
	game := gameForWindow(hwnd)

	switch message {
	case WM_CREATE:
		if lParam != 0 {
			params := (*CREATESTRUCTW)(unsafe.Pointer(lParam))
			_, _, _ = traceCall("SetWindowLongPtrW", procSetWindowLongPtrW, hwnd, GWLP_USERDATA, params.LpCreateParams)
			tracef("[okane][Main] WM_CREATE hwnd=0x%X game=0x%X\n", hwnd, params.LpCreateParams)
		}
		return 0

	case WM_ACTIVATEAPP:
		tracef("[okane][Main] WM_ACTIVATEAPP active=%d\n", boolToInt(wParam != 0))
		if game != nil {
			if wParam != 0 {
				game.OnActivated()
			} else {
				game.OnDeactivated()
			}
		}

	case WM_USER:
		tracef("[okane][Main] WM_USER suspend/resume handshake begin\n")
		if game != nil {
			game.OnSuspending()
			_, _, _ = procSetEvent.Call(uintptr(plmSuspendHandle))
			_, _, _ = procWaitForSingleObject.Call(uintptr(plmResumeHandle), INFINITE)
			game.OnResuming()
		}
		tracef("[okane][Main] WM_USER suspend/resume handshake end\n")
		return 0

	case WM_KEYDOWN:
		tracef("[okane][Main] WM_KEYDOWN vk=0x%X\n", wParam)
		if game != nil {
			game.OnKeyDown(uint32(wParam))
		}

	case WM_DESTROY:
		tracef("[okane][Main] WM_DESTROY hwnd=0x%X\n", hwnd)
		_, _, _ = procPostQuitMessage.Call(0)
		return 0
	}

	r1, _, _ := procDefWindowProcW.Call(hwnd, uintptr(message), wParam, lParam)
	return r1
}

func registerGame(game *Game) uintptr {
	id := uintptr(atomic.AddUint64(&nextGameID, 1))
	gameRegistry.Store(id, game)
	return id
}

func unregisterGame(id uintptr) {
	gameRegistry.Delete(id)
}

func gameForWindow(hwnd uintptr) *Game {
	gameID := getWindowUserData(hwnd)
	if gameID == 0 {
		return nil
	}

	value, ok := gameRegistry.Load(gameID)
	if !ok {
		return nil
	}

	game, _ := value.(*Game)
	return game
}

func getWindowUserData(hwnd uintptr) uintptr {
	r1, _, _ := procGetWindowLongPtrW.Call(hwnd, GWLP_USERDATA)
	return r1
}

func boolToInt(value bool) int {
	if value {
		return 1
	}
	return 0
}

func (g *Game) Initialize(hwnd HWND, width, height int32) {
	g.hwnd = hwnd
	g.width = width
	g.height = height
	g.active = true
	tracef("[okane][Main] Game.Initialize hwnd=0x%X size=%dx%d\n", uintptr(hwnd), width, height)
}

func (g *Game) Tick() {
	if g.suspended {
		return
	}

	g.frame++
	if g.frame == 1 || g.frame%600 == 0 {
		tracef("[okane][Main] Tick frame=%d\n", g.frame)
	}
}

func (g *Game) OnActivated() {
	g.active = true
	tracef("[okane][Main] Game.OnActivated\n")
}

func (g *Game) OnDeactivated() {
	g.active = false
	tracef("[okane][Main] Game.OnDeactivated\n")
}

func (g *Game) OnSuspending() {
	g.suspended = true
	tracef("[okane][Main] Game.OnSuspending\n")
}

func (g *Game) OnResuming() {
	g.suspended = false
	tracef("[okane][Main] Game.OnResuming\n")
}

func (g *Game) OnKeyDown(key uint32) {
	tracef("[okane][Main] Game.OnKeyDown vk=0x%X\n", key)
	if key == VK_ESCAPE {
		ExitGame()
	}
}

func ExitGame() {
	_, _, _ = procPostQuitMessage.Call(0)
}

func getModuleHandle() (HINSTANCE, error) {
	r1, _, err := traceCall("GetModuleHandleW", procGetModuleHandleW, 0)
	if r1 == 0 {
		return 0, fmt.Errorf("GetModuleHandleW failed: %w", err)
	}
	return HINSTANCE(r1), nil
}
