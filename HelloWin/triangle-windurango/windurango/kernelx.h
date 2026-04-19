#pragma once

#include <windows.h>

typedef enum CONSOLE_TYPE
{
    CONSOLE_TYPE_UNKNOWN = 0,
    CONSOLE_TYPE_XBOX_ONE = 1,
    CONSOLE_TYPE_XBOX_ONE_S = 2,
    CONSOLE_TYPE_XBOX_ONE_X = 3,
    CONSOLE_TYPE_XBOX_ONE_X_DEVKIT = 4,
} CONSOLE_TYPE;

typedef struct SYSTEMOSVERSIONINFO
{
    BYTE MajorVersion;
    BYTE MinorVersion;
    WORD BuildNumber;
    WORD Revision;
} SYSTEMOSVERSIONINFO;

extern "C" __declspec(dllimport) CONSOLE_TYPE WINAPI GetConsoleType(void);
extern "C" __declspec(dllimport) VOID WINAPI GetSystemOSVersion(SYSTEMOSVERSIONINFO *lpVersionInformation);
