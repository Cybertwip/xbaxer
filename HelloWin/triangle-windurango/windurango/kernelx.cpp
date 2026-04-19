#include "kernelx.h"

extern "C" __declspec(dllexport) CONSOLE_TYPE WINAPI GetConsoleType(void)
{
    return CONSOLE_TYPE_XBOX_ONE;
}

extern "C" __declspec(dllexport) VOID WINAPI GetSystemOSVersion(SYSTEMOSVERSIONINFO *lpVersionInformation)
{
    if (!lpVersionInformation)
    {
        return;
    }

    OSVERSIONINFOW versionInfo = {};
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
    if (GetVersionExW(&versionInfo))
    {
        lpVersionInformation->MajorVersion = static_cast<BYTE>(versionInfo.dwMajorVersion);
        lpVersionInformation->MinorVersion = static_cast<BYTE>(versionInfo.dwMinorVersion);
        lpVersionInformation->BuildNumber = static_cast<WORD>(versionInfo.dwBuildNumber);
        lpVersionInformation->Revision = 0;
        return;
    }

    lpVersionInformation->MajorVersion = 10;
    lpVersionInformation->MinorVersion = 0;
    lpVersionInformation->BuildNumber = 19041;
    lpVersionInformation->Revision = 0;
}
