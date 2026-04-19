#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <tuple>

class Game
{
public:
    Game() noexcept;
    ~Game();

    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    Game(Game const&) = delete;
    Game& operator=(Game const&) = delete;

    void Initialize(HWND window, int width, int height);
    void Tick();

    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnWindowSizeChanged(int width, int height);
    void OnKeyDown(UINT vk);

    void GetDefaultSize(int& width, int& height) const noexcept;

private:
    HWND m_window;
    int m_width;
    int m_height;
};

Game::Game() noexcept
    : m_window(nullptr)
    , m_width(1280)
    , m_height(720)
{
}

Game::~Game() = default;

void Game::Initialize(HWND window, int width, int height)
{
    m_window = window;
    m_width = width;
    m_height = height;
}

void Game::Tick()
{
    // Blank boot test: no rendering, no shaders, no D3D.
    // Keep the loop alive and optionally invalidate once in a while if desired.
    (void)m_window;
}

void Game::OnActivated()
{
}

void Game::OnDeactivated()
{
}

void Game::OnSuspending()
{
}

void Game::OnResuming()
{
}

void Game::OnWindowMoved()
{
}

void Game::OnWindowSizeChanged(int width, int height)
{
    m_width = width;
    m_height = height;
}

void Game::OnKeyDown(UINT)
{
}

void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    width = 1280;
    height = 720;
}

#ifdef __clang__
#    pragma clang diagnostic ignored "-Wcovered-switch-default"
#    pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#ifdef _MSC_VER
#    pragma warning(disable : 4061)
#endif

// -----------------------------------------------------------------------------
// Minimal mocks for desktop/non-GDK boot
// -----------------------------------------------------------------------------

#ifndef E_GAMERUNTIME_DLL_NOT_FOUND
#    define E_GAMERUNTIME_DLL_NOT_FOUND static_cast<HRESULT>(0x89245100u)
#endif

#ifndef E_GAMERUNTIME_VERSION_MISMATCH
#    define E_GAMERUNTIME_VERSION_MISMATCH static_cast<HRESULT>(0x89245101u)
#endif

inline bool XMVerifyCPUSupport() noexcept
{
    return true;
}

inline HRESULT XGameRuntimeInitialize() noexcept
{
    return S_OK;
}

inline void XGameRuntimeUninitialize() noexcept
{
}

namespace
{
std::unique_ptr<Game> g_game;

template <typename... TArgs>
void Tracef(const char* format, TArgs... args)
{
    char buffer[512] = {};
#if defined(_MSC_VER)
    sprintf_s(buffer, format, args...);
#else
    std::snprintf(buffer, sizeof(buffer), format, args...);
#endif
    OutputDebugStringA(buffer);
}
} // namespace

LPCWSTR g_szAppName = L"Okaneizer Reference";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ExitGame() noexcept;

int SampleMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);

    Tracef("[okane][Main] SampleMain begin hInstance=%p nCmdShow=%d\n", hInstance, nCmdShow);

    if (!XMVerifyCPUSupport())
    {
        OutputDebugStringA("ERROR: This hardware does not support the required instruction set.\n");
        return 1;
    }

    HRESULT hr = XGameRuntimeInitialize();
    if (FAILED(hr))
    {
        Tracef("[okane][Main] XGameRuntimeInitialize failed hr=0x%08X\n", static_cast<unsigned int>(hr));
        if (hr == E_GAMERUNTIME_DLL_NOT_FOUND || hr == E_GAMERUNTIME_VERSION_MISMATCH)
        {
            MessageBoxW(
                nullptr,
                L"Game Runtime is not installed on this system or needs updating.",
                g_szAppName,
                MB_ICONERROR | MB_OK);
        }
        return 1;
    }

    Tracef("[okane][Main] XGameRuntimeInitialize succeeded\n");

    g_game = std::make_unique<Game>();
    Tracef("[okane][Main] Game allocated ptr=%p\n", g_game.get());

    {
        WNDCLASSEXW wcex   = {};
        wcex.cbSize        = sizeof(WNDCLASSEXW);
        wcex.style         = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc   = WndProc;
        wcex.hInstance     = hInstance;
        wcex.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"OkaneizerReferenceWindowClass";

        if (!RegisterClassExW(&wcex))
        {
            Tracef("[okane][Main] RegisterClassExW failed gle=%lu\n", GetLastError());
            return 1;
        }

        Tracef("[okane][Main] RegisterClassExW succeeded class=%ls\n", wcex.lpszClassName);

        int width  = 0;
        int height = 0;
        g_game->GetDefaultSize(width, height);

        RECT rc = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwnd = CreateWindowExW(
            0,
            L"OkaneizerReferenceWindowClass",
            g_szAppName,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInstance,
            g_game.get());

        if (!hwnd)
        {
            Tracef("[okane][Main] CreateWindowExW failed gle=%lu\n", GetLastError());
            return 1;
        }

        Tracef(
            "[okane][Main] CreateWindowExW hwnd=%p size=%ldx%ld\n",
            hwnd,
            rc.right - rc.left,
            rc.bottom - rc.top);

        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
        Tracef("[okane][Main] ShowWindow desktop nCmdShow=%d + UpdateWindow\n", nCmdShow);

        GetClientRect(hwnd, &rc);
        Tracef("[okane][Main] Initialize begin client=%ldx%ld\n", rc.right - rc.left, rc.bottom - rc.top);
        g_game->Initialize(hwnd, rc.right - rc.left, rc.bottom - rc.top);
        Tracef("[okane][Main] Initialize finished\n");
    }

    MSG msg = {};
    std::uint32_t idleTicks = 0;

    while (WM_QUIT != msg.message)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            Tracef(
                "[okane][Main] loop message=0x%04X wParam=0x%p lParam=0x%p\n",
                msg.message,
                reinterpret_cast<void*>(msg.wParam),
                reinterpret_cast<void*>(msg.lParam));

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            if (idleTicks < 5)
            {
                Tracef("[okane][Main] idle tick #%u\n", idleTicks);
            }
            ++idleTicks;
            g_game->Tick();
            Sleep(1);
        }
    }

    Tracef("[okane][Main] message loop exiting wParam=%ld\n", static_cast<long>(msg.wParam));

    g_game.reset();
    Tracef("[okane][Main] Game destroyed\n");

    XGameRuntimeUninitialize();
    Tracef("[okane][Main] XGameRuntimeUninitialize complete\n");

    return static_cast<int>(msg.wParam);
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    try
    {
        return SampleMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA("*** ERROR: Unhandled C++ exception thrown: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA(" *** \n");
        return 1;
    }
    catch (...)
    {
        OutputDebugStringA("*** ERROR: Unknown unhandled C++ exception thrown ***\n");
        return 1;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static bool s_in_sizemove = false;
    static bool s_in_suspend  = false;
    static bool s_minimized   = false;
    static bool s_fullscreen  = false;

    auto game = reinterpret_cast<Game*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
        if (lParam)
        {
            auto params = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(params->lpCreateParams));
            Tracef("[okane][Main] WM_CREATE hwnd=%p game=%p\n", hWnd, params->lpCreateParams);
        }
        return 0;

    case WM_ACTIVATEAPP:
        Tracef("[okane][Main] WM_ACTIVATEAPP active=%u\n", static_cast<unsigned int>(wParam));
        if (game)
        {
            if (wParam)
            {
                game->OnActivated();
            }
            else
            {
                game->OnDeactivated();
            }
        }
        return 0;

    case WM_PAINT:
        if (s_in_sizemove && game)
        {
            game->Tick();
        }
        else
        {
            PAINTSTRUCT ps = {};
            BeginPaint(hWnd, &ps);
            FillRect(ps.hdc, &ps.rcPaint, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            EndPaint(hWnd, &ps);
        }
        return 0;

    case WM_MOVE:
        if (game)
        {
            game->OnWindowMoved();
        }
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            if (!s_minimized)
            {
                s_minimized = true;
                if (!s_in_suspend && game)
                {
                    game->OnSuspending();
                }
                s_in_suspend = true;
            }
        }
        else if (s_minimized)
        {
            s_minimized = false;
            if (s_in_suspend && game)
            {
                game->OnResuming();
            }
            s_in_suspend = false;
        }
        else if (!s_in_sizemove && game)
        {
            game->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        return 0;

    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (game)
        {
            RECT rc = {};
            GetClientRect(hWnd, &rc);
            game->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
        }
        return 0;

    case WM_GETMINMAXINFO:
        if (lParam)
        {
            auto info              = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 200;
        }
        return 0;

    case WM_POWERBROADCAST:
        switch (wParam)
        {
        case PBT_APMQUERYSUSPEND:
            if (!s_in_suspend && game)
            {
                game->OnSuspending();
            }
            s_in_suspend = true;
            return TRUE;

        case PBT_APMRESUMESUSPEND:
            if (!s_minimized)
            {
                if (s_in_suspend && game)
                {
                    game->OnResuming();
                }
                s_in_suspend = false;
            }
            return TRUE;
        }
        break;

    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
        {
            if (s_fullscreen)
            {
                SetWindowLongPtrW(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowLongPtrW(hWnd, GWL_EXSTYLE, 0);

                int width  = 800;
                int height = 600;
                if (game)
                {
                    game->GetDefaultSize(width, height);
                }

                ShowWindow(hWnd, SW_SHOWNORMAL);
                SetWindowPos(
                    hWnd,
                    HWND_TOP,
                    0,
                    0,
                    width,
                    height,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            else
            {
                SetWindowLongPtrW(hWnd, GWL_STYLE, WS_POPUP);
                SetWindowLongPtrW(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);
                SetWindowPos(
                    hWnd,
                    HWND_TOP,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                ShowWindow(hWnd, SW_SHOWMAXIMIZED);
            }

            s_fullscreen = !s_fullscreen;
            return 0;
        }
        break;

    case WM_MENUCHAR:
        return MAKELRESULT(0, MNC_CLOSE);

    case WM_KEYDOWN:
        if (game)
        {
            game->OnKeyDown(static_cast<UINT>(wParam));
        }
        return 0;

    case WM_DESTROY:
        Tracef("[okane][Main] WM_DESTROY hwnd=%p\n", hWnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void ExitGame() noexcept
{
    PostQuitMessage(0);
}