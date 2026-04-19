#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace {

constexpr wchar_t kWindowClassName[] = L"TriangleCppWindowClass";
constexpr wchar_t kWindowTitle[] = L"Triangle C++";
constexpr UINT kSuspendResumeMessage = WM_USER;

constexpr LONG kWindowWidth = 1920;
constexpr LONG kWindowHeight = 1080;

constexpr DWORD kEventModifyState = 0x0002;
constexpr DWORD kSynchronize = 0x00100000;

struct ProcCandidate {
  const wchar_t *dll;
  const char *proc;
};

void Tracef(const char *format, ...);

class CompatProc {
 public:
  CompatProc(const char *logical_name, std::initializer_list<ProcCandidate> candidates)
      : logical_name_(logical_name), candidates_(candidates) {}

  FARPROC Resolve() {
    if (resolved_ != nullptr) {
      return resolved_;
    }

    for (const auto &candidate : candidates_) {
      HMODULE module = LoadLibraryW(candidate.dll);
      if (module == nullptr) {
        continue;
      }
      FARPROC proc = GetProcAddress(module, candidate.proc);
      if (proc == nullptr) {
        continue;
      }
      resolved_ = proc;
      resolved_dll_ = candidate.dll;
      resolved_proc_ = candidate.proc;
      Tracef("[triangle][compat] %s resolved via %ls!%s\n", logical_name_, resolved_dll_, resolved_proc_);
      return resolved_;
    }

    Tracef("[triangle][compat] %s could not be resolved\n", logical_name_);
    return nullptr;
  }

  const wchar_t *resolved_dll() const { return resolved_dll_ != nullptr ? resolved_dll_ : L"(unresolved)"; }
  const char *resolved_proc() const { return resolved_proc_ != nullptr ? resolved_proc_ : "(unresolved)"; }

 private:
  const char *logical_name_;
  std::initializer_list<ProcCandidate> candidates_;
  FARPROC resolved_ = nullptr;
  const wchar_t *resolved_dll_ = nullptr;
  const char *resolved_proc_ = nullptr;
};

void Tracef(const char *format, ...) {
  char buffer[1024] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  OutputDebugStringA(buffer);
}

using CreateWindowExWProc = HWND(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using DefWindowProcWProc = LRESULT(WINAPI *)(HWND, UINT, WPARAM, LPARAM);
using ShowWindowProc = BOOL(WINAPI *)(HWND, int);
using PeekMessageWProc = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT);
using TranslateMessageProc = BOOL(WINAPI *)(const MSG *);
using DispatchMessageWProc = LRESULT(WINAPI *)(const MSG *);
using PostMessageWProc = BOOL(WINAPI *)(HWND, UINT, WPARAM, LPARAM);
using PostQuitMessageProc = void(WINAPI *)(int);
using RegisterClassExWProc = ATOM(WINAPI *)(const WNDCLASSEXW *);
using LoadCursorWProc = HCURSOR(WINAPI *)(HINSTANCE, LPCWSTR);
using GetClientRectProc = BOOL(WINAPI *)(HWND, LPRECT);
using SetWindowLongPtrWProc = LONG_PTR(WINAPI *)(HWND, int, LONG_PTR);
using GetWindowLongPtrWProc = LONG_PTR(WINAPI *)(HWND, int);
using CreateEventExWProc = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
using ResetEventProc = BOOL(WINAPI *)(HANDLE);
using SetEventProc = BOOL(WINAPI *)(HANDLE);
using WaitForSingleObjectProc = DWORD(WINAPI *)(HANDLE, DWORD);
using CloseHandleProc = BOOL(WINAPI *)(HANDLE);
using GetModuleHandleWProc = HMODULE(WINAPI *)(LPCWSTR);
using RegisterAppStateChangeNotificationProc = HRESULT(WINAPI *)(void(WINAPI *)(BOOLEAN, PVOID), PVOID, PVOID *);
using UnregisterAppStateChangeNotificationProc = HRESULT(WINAPI *)(PVOID);

CompatProc g_create_window_ex_w(
    "CreateWindowExW",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "CreateWindowExW"},
        {L"user32.dll", "CreateWindowExW"},
    });
CompatProc g_def_window_proc_w(
    "DefWindowProcW",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "DefWindowProcW"},
        {L"user32.dll", "DefWindowProcW"},
    });
CompatProc g_show_window(
    "ShowWindow",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "ShowWindow"},
        {L"user32.dll", "ShowWindow"},
    });
CompatProc g_peek_message_w(
    "PeekMessageW",
    {
        {L"ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PeekMessageW"},
        {L"user32.dll", "PeekMessageW"},
    });
CompatProc g_translate_message(
    "TranslateMessage",
    {
        {L"ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "TranslateMessage"},
        {L"user32.dll", "TranslateMessage"},
    });
CompatProc g_dispatch_message_w(
    "DispatchMessageW",
    {
        {L"ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "DispatchMessageW"},
        {L"user32.dll", "DispatchMessageW"},
    });
CompatProc g_post_message_w(
    "PostMessageW",
    {
        {L"ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PostMessageW"},
        {L"user32.dll", "PostMessageW"},
    });
CompatProc g_post_quit_message(
    "PostQuitMessage",
    {
        {L"ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "PostQuitMessage"},
        {L"user32.dll", "PostQuitMessage"},
    });
CompatProc g_register_class_ex_w(
    "RegisterClassExW",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "RegisterClassExW"},
        {L"user32.dll", "RegisterClassExW"},
    });
CompatProc g_load_cursor_w(
    "LoadCursorW",
    {
        {L"ext-ms-win-rtcore-ntuser-cursor-l1-1-0.dll", "LoadCursorW"},
        {L"user32.dll", "LoadCursorW"},
    });
CompatProc g_get_client_rect(
    "GetClientRect",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "GetClientRect"},
        {L"user32.dll", "GetClientRect"},
    });
CompatProc g_set_window_long_ptr_w(
    "SetWindowLongPtrW",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "SetWindowLongPtrW"},
        {L"user32.dll", "SetWindowLongPtrW"},
    });
CompatProc g_get_window_long_ptr_w(
    "GetWindowLongPtrW",
    {
        {L"ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "GetWindowLongPtrW"},
        {L"user32.dll", "GetWindowLongPtrW"},
    });
CompatProc g_create_event_ex_w(
    "CreateEventExW",
    {
        {L"api-ms-win-core-synch-l1-1-0.dll", "CreateEventExW"},
        {L"kernel32.dll", "CreateEventExW"},
    });
CompatProc g_reset_event(
    "ResetEvent",
    {
        {L"api-ms-win-core-synch-l1-1-0.dll", "ResetEvent"},
        {L"kernel32.dll", "ResetEvent"},
    });
CompatProc g_set_event(
    "SetEvent",
    {
        {L"api-ms-win-core-synch-l1-1-0.dll", "SetEvent"},
        {L"kernel32.dll", "SetEvent"},
    });
CompatProc g_wait_for_single_object(
    "WaitForSingleObject",
    {
        {L"api-ms-win-core-synch-l1-1-0.dll", "WaitForSingleObject"},
        {L"kernel32.dll", "WaitForSingleObject"},
    });
CompatProc g_close_handle(
    "CloseHandle",
    {
        {L"api-ms-win-core-handle-l1-1-0.dll", "CloseHandle"},
        {L"kernel32.dll", "CloseHandle"},
    });
CompatProc g_get_module_handle_w(
    "GetModuleHandleW",
    {
        {L"api-ms-win-core-libraryloader-l1-2-0.dll", "GetModuleHandleW"},
        {L"kernel32.dll", "GetModuleHandleW"},
    });
CompatProc g_register_app_state_change_notification(
    "RegisterAppStateChangeNotification",
    {
        {L"api-ms-win-core-psm-appnotify-l1-1-0.dll", "RegisterAppStateChangeNotification"},
    });
CompatProc g_unregister_app_state_change_notification(
    "UnregisterAppStateChangeNotification",
    {
        {L"api-ms-win-core-psm-appnotify-l1-1-0.dll", "UnregisterAppStateChangeNotification"},
    });

template <typename ProcType>
ProcType Resolve(CompatProc &proc) {
  return reinterpret_cast<ProcType>(proc.Resolve());
}

class Game {
 public:
  void Initialize(HWND hwnd, LONG width, LONG height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;
    active_ = true;
    Tracef("[triangle][game] Initialize hwnd=0x%p size=%ldx%ld\n", hwnd_, width_, height_);
  }

  void Tick() {
    if (suspended_) {
      return;
    }
    ++frame_;
    if (frame_ == 1 || frame_ % 600 == 0) {
      Tracef("[triangle][game] Tick frame=%llu\n", static_cast<unsigned long long>(frame_));
    }
  }

  void OnActivated() {
    active_ = true;
    Tracef("[triangle][game] OnActivated\n");
  }

  void OnDeactivated() {
    active_ = false;
    Tracef("[triangle][game] OnDeactivated\n");
  }

  void OnSuspending() {
    suspended_ = true;
    Tracef("[triangle][game] OnSuspending\n");
  }

  void OnResuming() {
    suspended_ = false;
    Tracef("[triangle][game] OnResuming\n");
  }

  void OnKeyDown(UINT key) {
    Tracef("[triangle][game] OnKeyDown vk=0x%X\n", key);
    if (key == VK_ESCAPE) {
      auto post_quit = Resolve<PostQuitMessageProc>(g_post_quit_message);
      if (post_quit != nullptr) {
        post_quit(0);
      }
    }
  }

 private:
  HWND hwnd_ = nullptr;
  LONG width_ = 0;
  LONG height_ = 0;
  std::uint64_t frame_ = 0;
  bool active_ = false;
  bool suspended_ = false;
};

Game g_game;
HANDLE g_suspend_event = nullptr;
HANDLE g_resume_event = nullptr;
PVOID g_plm_registration = nullptr;

void ShutdownPLM() {
  auto unregister_notification =
      Resolve<UnregisterAppStateChangeNotificationProc>(g_unregister_app_state_change_notification);
  auto close_handle = Resolve<CloseHandleProc>(g_close_handle);

  if (g_plm_registration != nullptr && unregister_notification != nullptr) {
    unregister_notification(g_plm_registration);
    g_plm_registration = nullptr;
  }
  if (g_suspend_event != nullptr && close_handle != nullptr) {
    close_handle(g_suspend_event);
    g_suspend_event = nullptr;
  }
  if (g_resume_event != nullptr && close_handle != nullptr) {
    close_handle(g_resume_event);
    g_resume_event = nullptr;
  }
}

void WINAPI AppStateChangeHandler(BOOLEAN quiesced, PVOID context) {
  Tracef("[triangle][main] AppStateChangeNotification quiesced=%d hwnd=0x%p\n", quiesced ? 1 : 0, context);

  auto reset_event = Resolve<ResetEventProc>(g_reset_event);
  auto set_event = Resolve<SetEventProc>(g_set_event);
  auto post_message = Resolve<PostMessageWProc>(g_post_message_w);
  auto wait_for_single_object = Resolve<WaitForSingleObjectProc>(g_wait_for_single_object);

  if (quiesced) {
    if (reset_event != nullptr) {
      reset_event(g_suspend_event);
      reset_event(g_resume_event);
    }
    if (post_message != nullptr) {
      post_message(static_cast<HWND>(context), kSuspendResumeMessage, 0, 0);
    }
    if (wait_for_single_object != nullptr) {
      wait_for_single_object(g_suspend_event, INFINITE);
    }
  } else if (set_event != nullptr) {
    set_event(g_resume_event);
  }
}

bool InitializePLM(HWND hwnd) {
  auto register_notification =
      Resolve<RegisterAppStateChangeNotificationProc>(g_register_app_state_change_notification);
  auto create_event = Resolve<CreateEventExWProc>(g_create_event_ex_w);

  if (register_notification == nullptr || create_event == nullptr) {
    Tracef("[triangle][main] PLM notifications unavailable\n");
    return true;
  }

  g_suspend_event = create_event(nullptr, nullptr, 0, kEventModifyState | kSynchronize);
  if (g_suspend_event == nullptr) {
    Tracef("[triangle][main] CreateEventExW(suspend) failed gle=%lu\n", GetLastError());
    return false;
  }

  g_resume_event = create_event(nullptr, nullptr, 0, kEventModifyState | kSynchronize);
  if (g_resume_event == nullptr) {
    Tracef("[triangle][main] CreateEventExW(resume) failed gle=%lu\n", GetLastError());
    ShutdownPLM();
    return false;
  }

  HRESULT hr = register_notification(&AppStateChangeHandler, hwnd, &g_plm_registration);
  if (FAILED(hr)) {
    Tracef("[triangle][main] RegisterAppStateChangeNotification failed hr=0x%08lX\n", static_cast<unsigned long>(hr));
    ShutdownPLM();
    return false;
  }

  Tracef("[triangle][main] RegisterAppStateChangeNotification succeeded\n");
  return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  auto set_window_user_data = Resolve<SetWindowLongPtrWProc>(g_set_window_long_ptr_w);
  auto get_window_user_data = Resolve<GetWindowLongPtrWProc>(g_get_window_long_ptr_w);
  auto def_window_proc = Resolve<DefWindowProcWProc>(g_def_window_proc_w);
  auto post_quit = Resolve<PostQuitMessageProc>(g_post_quit_message);
  auto set_event = Resolve<SetEventProc>(g_set_event);
  auto wait_for_single_object = Resolve<WaitForSingleObjectProc>(g_wait_for_single_object);

  auto *game = reinterpret_cast<Game *>(get_window_user_data != nullptr ? get_window_user_data(hwnd, GWLP_USERDATA) : 0);

  switch (message) {
    case WM_CREATE: {
      auto *create = reinterpret_cast<LPCREATESTRUCTW>(l_param);
      if (set_window_user_data != nullptr && create != nullptr) {
        set_window_user_data(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      }
      Tracef("[triangle][main] WM_CREATE hwnd=0x%p game=0x%p\n", hwnd, create != nullptr ? create->lpCreateParams : nullptr);
      return 0;
    }
    case WM_ACTIVATEAPP:
      Tracef("[triangle][main] WM_ACTIVATEAPP active=%d\n", w_param != 0 ? 1 : 0);
      if (game != nullptr) {
        if (w_param != 0) {
          game->OnActivated();
        } else {
          game->OnDeactivated();
        }
      }
      return 0;
    case kSuspendResumeMessage:
      Tracef("[triangle][main] WM_USER suspend/resume handshake begin\n");
      if (game != nullptr) {
        game->OnSuspending();
        if (set_event != nullptr) {
          set_event(g_suspend_event);
        }
        if (wait_for_single_object != nullptr) {
          wait_for_single_object(g_resume_event, INFINITE);
        }
        game->OnResuming();
      }
      Tracef("[triangle][main] WM_USER suspend/resume handshake end\n");
      return 0;
    case WM_KEYDOWN:
      if (game != nullptr) {
        game->OnKeyDown(static_cast<UINT>(w_param));
      }
      return 0;
    case WM_DESTROY:
      Tracef("[triangle][main] WM_DESTROY hwnd=0x%p\n", hwnd);
      if (post_quit != nullptr) {
        post_quit(0);
      }
      return 0;
    default:
      break;
  }

  if (def_window_proc != nullptr) {
    return def_window_proc(hwnd, message, w_param, l_param);
  }
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  auto get_module_handle = Resolve<GetModuleHandleWProc>(g_get_module_handle_w);
  auto register_class = Resolve<RegisterClassExWProc>(g_register_class_ex_w);
  auto load_cursor = Resolve<LoadCursorWProc>(g_load_cursor_w);
  auto create_window = Resolve<CreateWindowExWProc>(g_create_window_ex_w);
  auto show_window = Resolve<ShowWindowProc>(g_show_window);
  auto get_client_rect = Resolve<GetClientRectProc>(g_get_client_rect);
  auto peek_message = Resolve<PeekMessageWProc>(g_peek_message_w);
  auto translate_message = Resolve<TranslateMessageProc>(g_translate_message);
  auto dispatch_message = Resolve<DispatchMessageWProc>(g_dispatch_message_w);

  if (get_module_handle == nullptr || register_class == nullptr || load_cursor == nullptr ||
      create_window == nullptr || show_window == nullptr || get_client_rect == nullptr ||
      peek_message == nullptr || translate_message == nullptr || dispatch_message == nullptr) {
    Tracef("[triangle][main] missing one or more required Win32 exports\n");
    return 1;
  }

  instance = get_module_handle(nullptr);
  if (instance == nullptr) {
    Tracef("[triangle][main] GetModuleHandleW failed gle=%lu\n", GetLastError());
    return 1;
  }

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = &WndProc;
  window_class.hInstance = instance;
  window_class.hCursor = load_cursor(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kWindowClassName;

  if (register_class(&window_class) == 0) {
    Tracef("[triangle][main] RegisterClassExW failed gle=%lu\n", GetLastError());
    return 1;
  }

  HWND hwnd = create_window(
      0,
      kWindowClassName,
      kWindowTitle,
      WS_POPUP | WS_VISIBLE,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      kWindowWidth,
      kWindowHeight,
      nullptr,
      nullptr,
      instance,
      &g_game);
  if (hwnd == nullptr) {
    Tracef("[triangle][main] CreateWindowExW failed gle=%lu\n", GetLastError());
    return 1;
  }

  show_window(hwnd, SW_SHOWDEFAULT);

  RECT client = {};
  if (!get_client_rect(hwnd, &client)) {
    Tracef("[triangle][main] GetClientRect failed gle=%lu\n", GetLastError());
    return 1;
  }

  g_game.Initialize(hwnd, client.right - client.left, client.bottom - client.top);
  if (!InitializePLM(hwnd)) {
    return 1;
  }

  MSG message = {};
  while (message.message != WM_QUIT) {
    if (peek_message(&message, nullptr, 0, 0, PM_REMOVE)) {
      translate_message(&message);
      dispatch_message(&message);
      continue;
    }

    g_game.Tick();
    Sleep(1);
  }

  ShutdownPLM();
  return static_cast<int>(message.wParam);
}
