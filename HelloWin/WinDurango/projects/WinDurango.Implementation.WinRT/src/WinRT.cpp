#include "WinDurangoImplWinRT.h"
#include <iostream>
#include <windows.h>

/*
 * Found here
 * https://stackoverflow.com/questions/12168113/detect-application-running-in-winrt-mode-by-pid
 */
bool WDIMPL_API ImplementationSupported()
{
    return IsImmersiveProcess(GetCurrentProcess());
}