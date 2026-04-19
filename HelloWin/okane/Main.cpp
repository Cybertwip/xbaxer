//
// Main.cpp
//

#include "pch.h"
#include "Game.h"

#include <appnotify.h>

using namespace DirectX;

#ifdef __clang__
#    pragma clang diagnostic ignored "-Wcovered-switch-default"
#    pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

namespace
{
std::unique_ptr<Game> g_game;
#ifdef _GAMING_XBOX
HANDLE g_plmSuspendComplete = nullptr;
HANDLE g_plmSignalResume    = nullptr;
#endif

template <typename... TArgs>
void Tracef(const char* format, TArgs... args)
{
    char buffer[512] = {};
    sprintf_s(buffer, format, args...);
    OutputDebugStringA(buffer);
}
}  // namespace

LPCWSTR g_szAppName = L"Okaneizer Reference";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ExitGame() noexcept;

int SampleMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);

    Tracef("[okane][Main] SampleMain begin hInstance=%p nCmdShow=%d\n", hInstance, nCmdShow);

    if (!XMVerifyCPUSupport())
    {
#ifdef OKANE_DEBUG
        OutputDebugStringA("ERROR: This hardware does not support the required instruction set.\n");
#    if defined(_GAMING_XBOX) && defined(__AVX2__)
        OutputDebugStringA("This may indicate a Gaming.Xbox.Scarlett.x64 binary is being run on an Xbox One.\n");
#    endif
#endif
        return 1;
    }

    HRESULT hr = XGameRuntimeInitialize();
    if (FAILED(hr))
    {
        Tracef("[okane][Main] XGameRuntimeInitialize failed hr=0x%08X\n", static_cast<unsigned int>(hr));
        if (hr == E_GAMERUNTIME_DLL_NOT_FOUND || hr == E_GAMERUNTIME_VERSION_MISMATCH)
        {
#ifdef _GAMING_DESKTOP
            std::ignore = MessageBoxW(nullptr, L"Game Runtime is not installed on this system or needs updating.",
                                      g_szAppName, MB_ICONERROR | MB_OK);
#endif
        }
        return 1;
    }

    Tracef("[okane][Main] XGameRuntimeInitialize succeeded\n");

#ifdef _GAMING_XBOX
    Tracef("[okane][Main] Leaving main thread affinity unchanged (matching GEF startup path)\n");
#endif

    g_game = std::make_unique<Game>();
    Tracef("[okane][Main] Game allocated ptr=%p\n", g_game.get());

#ifdef _GAMING_XBOX
    PAPPSTATE_REGISTRATION hPLM = {};
#endif
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

#ifdef _GAMING_XBOX
        RECT rc = {0, 0, 1920, 1080};
        switch (XSystemGetDeviceType())
        {
        case XSystemDeviceType::XboxOne:
        case XSystemDeviceType::XboxOneS:
#    ifdef OKANE_DEBUG
            OutputDebugStringA("INFO: Swapchain using 1080p (1920 x 1080)\n");
#    endif
            break;

        case XSystemDeviceType::XboxScarlettLockhart:
            rc = {0, 0, 2560, 1440};
#    ifdef OKANE_DEBUG
            OutputDebugStringA("INFO: Swapchain using 1440p (2560 x 1440)\n");
#    endif
            break;

        case XSystemDeviceType::XboxScarlettAnaconda:
        case XSystemDeviceType::XboxOneXDevkit:
        case XSystemDeviceType::XboxScarlettDevkit:
        default:
            rc = {0, 0, 3840, 2160};
#    ifdef OKANE_DEBUG
            OutputDebugStringA("INFO: Swapchain using 4k (3840 x 2160)\n");
#    endif
            break;
        }
#else
        int width  = 0;
        int height = 0;
        g_game->GetDefaultSize(width, height);

        RECT rc = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
#endif

        HWND hwnd = CreateWindowExW(0, L"OkaneizerReferenceWindowClass", g_szAppName,
#ifdef _GAMING_XBOX
                                    WS_POPUP,
#else
                                    WS_OVERLAPPEDWINDOW,
#endif
                                    CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
                                    nullptr, hInstance, g_game.get());

        if (!hwnd)
        {
            Tracef("[okane][Main] CreateWindowExW failed gle=%lu\n", GetLastError());
            return 1;
        }

        Tracef("[okane][Main] CreateWindowExW hwnd=%p size=%ldx%ld\n", hwnd,
               rc.right - rc.left, rc.bottom - rc.top);

#ifdef _GAMING_XBOX
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        Tracef("[okane][Main] ShowWindow Xbox SW_SHOWDEFAULT\n");
#else
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
        Tracef("[okane][Main] ShowWindow desktop nCmdShow=%d + UpdateWindow\n", nCmdShow);
#endif

        GetClientRect(hwnd, &rc);
        Tracef("[okane][Main] Initialize begin client=%ldx%ld\n", rc.right - rc.left, rc.bottom - rc.top);
        g_game->Initialize(hwnd, rc.right - rc.left, rc.bottom - rc.top);
        Tracef("[okane][Main] Initialize finished\n");

#ifdef _GAMING_XBOX
        // Pump startup messages once so the window is fully realized before first present.
        MSG startupMsg = {};
        uint32_t startupMessageCount = 0;
        while (PeekMessage(&startupMsg, nullptr, 0, 0, PM_REMOVE))
        {
            ++startupMessageCount;
            Tracef("[okane][Main] startup message=0x%04X wParam=0x%p lParam=0x%p\n",
                   startupMsg.message, reinterpret_cast<void*>(startupMsg.wParam),
                   reinterpret_cast<void*>(startupMsg.lParam));
            TranslateMessage(&startupMsg);
            DispatchMessage(&startupMsg);
        }

        Tracef("[okane][Main] startup message pump complete count=%u\n", startupMessageCount);

        // Submit one frame immediately so the shell splash screen is replaced
        // even if the steady-state loop has not started ticking yet.
        Tracef("[okane][Main] bootstrap Tick begin\n");
        g_game->Tick();
        Tracef("[okane][Main] bootstrap Tick end\n");
#endif

#ifdef _GAMING_XBOX
        g_plmSuspendComplete = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        g_plmSignalResume    = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!g_plmSuspendComplete || !g_plmSignalResume)
        {
            Tracef("[okane][Main] CreateEventEx failed suspend=%p resume=%p gle=%lu\n",
                   g_plmSuspendComplete, g_plmSignalResume, GetLastError());
            return 1;
        }

        Tracef("[okane][Main] PLM events created suspend=%p resume=%p\n",
               g_plmSuspendComplete, g_plmSignalResume);

        if (RegisterAppStateChangeNotification([](BOOLEAN quiesced, PVOID context) {
            Tracef("[okane][Main] AppStateChangeNotification quiesced=%d hwnd=%p\n",
                   quiesced ? 1 : 0, context);
            if (quiesced)
            {
                ResetEvent(g_plmSuspendComplete);
                ResetEvent(g_plmSignalResume);
                PostMessage(reinterpret_cast<HWND>(context), WM_USER, 0, 0);
                std::ignore = WaitForSingleObject(g_plmSuspendComplete, INFINITE);
            }
            else
            {
                SetEvent(g_plmSignalResume);
            }
        }, hwnd, &hPLM))
        {
            Tracef("[okane][Main] RegisterAppStateChangeNotification failed\n");
            return 1;
        }

        Tracef("[okane][Main] RegisterAppStateChangeNotification succeeded\n");
#endif
    }

    MSG msg = {};
    uint32_t idleTicks = 0;
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            Tracef("[okane][Main] loop message=0x%04X wParam=0x%p lParam=0x%p\n",
                   msg.message, reinterpret_cast<void*>(msg.wParam), reinterpret_cast<void*>(msg.lParam));
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (idleTicks < 5)
            {
                Tracef("[okane][Main] idle tick #%u\n", idleTicks);
            }
            ++idleTicks;
            g_game->Tick();
        }
    }

    Tracef("[okane][Main] message loop exiting wParam=%ld\n", static_cast<long>(msg.wParam));

    g_game.reset();
    Tracef("[okane][Main] Game destroyed\n");

#ifdef _GAMING_XBOX
    UnregisterAppStateChangeNotification(hPLM);

    CloseHandle(g_plmSuspendComplete);
    CloseHandle(g_plmSignalResume);
#endif

    XGameRuntimeUninitialize();
    Tracef("[okane][Main] XGameRuntimeUninitialize complete\n");

    return static_cast<int>(msg.wParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
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
#ifdef _GAMING_DESKTOP
    static bool s_in_sizemove = false;
    static bool s_in_suspend  = false;
    static bool s_minimized   = false;
    static bool s_fullscreen  = false;
#endif

    auto game = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
        if (lParam)
        {
            auto params = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(params->lpCreateParams));
            Tracef("[okane][Main] WM_CREATE hwnd=%p game=%p\n", hWnd, params->lpCreateParams);
        }
        break;

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
        break;

#ifdef _GAMING_XBOX
    case WM_USER:
        Tracef("[okane][Main] WM_USER suspend/resume handshake begin\n");
        if (game)
        {
            game->OnSuspending();
            SetEvent(g_plmSuspendComplete);
            std::ignore = WaitForSingleObject(g_plmSignalResume, INFINITE);
            game->OnResuming();
        }
        Tracef("[okane][Main] WM_USER suspend/resume handshake end\n");
        break;

    case WM_KEYDOWN:
        Tracef("[okane][Main] WM_KEYDOWN vk=0x%X\n", static_cast<unsigned int>(wParam));
        if (game)
        {
            game->OnKeyDown(static_cast<UINT>(wParam));
        }
        break;
#else
    case WM_PAINT:
        if (s_in_sizemove && game)
        {
            game->Tick();
        }
        else
        {
            PAINTSTRUCT ps;
            std::ignore = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_MOVE:
        if (game)
        {
            game->OnWindowMoved();
        }
        break;

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
        break;

    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        break;

    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (game)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            game->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
        }
        break;

    case WM_GETMINMAXINFO:
        if (lParam)
        {
            auto info              = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 200;
        }
        break;

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
                SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, 0);

                int width  = 800;
                int height = 600;
                if (game)
                {
                    game->GetDefaultSize(width, height);
                }

                ShowWindow(hWnd, SW_SHOWNORMAL);
                SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            else
            {
                SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);
                SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                ShowWindow(hWnd, SW_SHOWMAXIMIZED);
            }

            s_fullscreen = !s_fullscreen;
        }
        break;

    case WM_MENUCHAR:
        return MAKELRESULT(0, MNC_CLOSE);

    case WM_KEYDOWN:
        if (game)
        {
            game->OnKeyDown(static_cast<UINT>(wParam));
        }
        break;
#endif

    case WM_DESTROY:
        Tracef("[okane][Main] WM_DESTROY hwnd=%p\n", hWnd);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void ExitGame() noexcept
{
    PostQuitMessage(0);
}
