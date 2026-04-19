// XboxConsoleStubs.cpp
//
// Xbox Game Core does not expose Win32 console APIs or WER (Windows Error
// Reporting) APIs. The Go runtime (compiled via CGO) references several of
// these symbols unconditionally in its platform layer. This file provides
// no-op stub implementations so the linker is satisfied.
//
// These stubs are only compiled when targeting an Xbox platform
// (_GAMING_XBOX is defined by the GDK toolchain).

#ifdef _GAMING_XBOX

// Avoid redefining macros already set on the command line
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ---------------------------------------------------------------------------
// Console API stubs
// The Go runtime probes and uses these for its stdio/signal machinery. On
// Xbox they are not available; the stubs return safe failure values.
// ---------------------------------------------------------------------------

extern "C"
{

BOOL WINAPI WriteConsoleW(
    HANDLE   /*hConsoleOutput*/,
    const VOID* /*lpBuffer*/,
    DWORD    /*nNumberOfCharsToWrite*/,
    LPDWORD  lpNumberOfCharsWritten,
    LPVOID   /*lpReserved*/)
{
    if (lpNumberOfCharsWritten)
        *lpNumberOfCharsWritten = 0;
    return FALSE;
}

BOOL WINAPI GetConsoleMode(
    HANDLE   /*hConsoleHandle*/,
    LPDWORD  lpMode)
{
    if (lpMode)
        *lpMode = 0;
    return FALSE;
}

// SetConsoleCtrlHandler: the handler type is not available in the GDK
// WINAPI_FAMILY_GAMES partition, so we use the raw function-pointer type.
typedef BOOL (WINAPI *CONSOLE_CTRL_HANDLER)(DWORD dwCtrlType);

BOOL WINAPI SetConsoleCtrlHandler(
    CONSOLE_CTRL_HANDLER /*HandlerRoutine*/,
    BOOL                 /*Add*/)
{
    return FALSE;
}

BOOL WINAPI ReadConsoleW(
    HANDLE   /*hConsoleInput*/,
    LPVOID   /*lpBuffer*/,
    DWORD    /*nNumberOfCharsToRead*/,
    LPDWORD  lpNumberOfCharsRead,
    LPVOID   /*pInputControl*/)
{
    if (lpNumberOfCharsRead)
        *lpNumberOfCharsRead = 0;
    return FALSE;
}

UINT WINAPI GetConsoleOutputCP(void)
{
    return 0;
}

// ---------------------------------------------------------------------------
// Windows Error Reporting (WER) stubs
// The Go runtime calls WerSetFlags / WerGetFlags for crash reporting. These
// APIs do not exist on Xbox; stub them out as no-ops.
// ---------------------------------------------------------------------------

typedef DWORD WER_FAULT_REPORTING_FLAGS;

HRESULT WINAPI WerSetFlags(WER_FAULT_REPORTING_FLAGS /*dwFlags*/)
{
    return S_OK;
}

HRESULT WINAPI WerGetFlags(
    HANDLE                     /*hProcess*/,
    WER_FAULT_REPORTING_FLAGS* pdwFlags)
{
    if (pdwFlags)
        *pdwFlags = 0;
    return S_OK;
}

// ---------------------------------------------------------------------------
// NLS/Locale API stubs
// libucrt references IsValidLocale, GetUserDefaultLCID, and GetOEMCP via
// winapi_thunks. Stub them so the locale subsystem degrades gracefully on
// Xbox Game Core.
// ---------------------------------------------------------------------------

BOOL WINAPI IsValidLocale(LCID /*Locale*/, DWORD /*dwFlags*/)
{
    return FALSE;
}

LCID WINAPI GetUserDefaultLCID(void)
{
    // Return the LCID for English (United States) as a safe fallback.
    return MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), SORT_DEFAULT);
}

UINT WINAPI GetOEMCP(void)
{
    return 437; // OEM US code page – safe fallback
}

} // extern "C"

#endif // _GAMING_XBOX
