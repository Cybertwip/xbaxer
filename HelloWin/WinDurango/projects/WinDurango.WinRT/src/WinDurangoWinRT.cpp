#include "WinDurangoWinRT.h"

std::shared_ptr<wd::common::WinDurango> p_wd;

/*
 * https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, // handle to DLL module
                    DWORD fdwReason,    // reason for calling function
                    LPVOID lpvReserved) // reserved
{
    p_wd = wd::common::WinDurango::GetInstance();
    p_wd->log.Log("WinDurango::WinRT", "Initialized");
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}

HRESULT WINAPI ActivationFactory(HSTRING classID, void* factory) {
    return 0;
}