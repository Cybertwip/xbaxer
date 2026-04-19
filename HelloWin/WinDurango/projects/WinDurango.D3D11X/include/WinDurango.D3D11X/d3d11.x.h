#pragma once
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "version.lib")
#include "unknown.g.h"
#include "xcom/base.h"
#include <Windows.h>
#include <bitset>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <map>
#include <mutex>

// We use that to know the OS version.
abi_t g_ABI{};

// Immediate Context fence object.
BOOL m_Fence = TRUE;

// Multimap for placement update
std::multimap<void *, void *> g_ResourceMap;
std::mutex g_ResourceMapMutex;

#pragma comment(lib, "onecore.lib")
#pragma comment(lib, "kernel32.lib")

void GetCombaseVersion()
{
    DWORD FileVersionSize = GetFileVersionInfoSizeW(L".\\EmbeddedXvd\\Windows\\System32\\combase.dll", NULL);
    if (!FileVersionSize)
    {
        MessageBoxW(nullptr,
                    L"Couldn't get the combase version info size! Make sure you have EmbeddedXvd in the game "
                    L"root/Mount folder.",
                    L"D3D11.X Error!", MB_OK);
    }

    BYTE *Data = new BYTE[FileVersionSize];
    BOOL ret = GetFileVersionInfoW(L".\\EmbeddedXvd\\Windows\\System32\\combase.dll", NULL, FileVersionSize, Data);
    if (!ret)
    {
        MessageBoxW(
            nullptr,
            L"Couldn't get the combase version info! Make sure you have EmbeddedXvd in the game root/Mount folder.",
            L"D3D11.X Error!", MB_OK);
        delete[] Data;
    }

    VS_FIXEDFILEINFO *pFixedFileInfo{};
    UINT Length = 0;

    VerQueryValueW(Data, L"\\", (LPVOID *)&pFixedFileInfo, &Length);
    if (!pFixedFileInfo)
    {
        MessageBoxW(
            nullptr,
            L"Couldn't get the combase version value! Make sure you have EmbeddedXvd in the game root/Mount folder.",
            L"D3D11.X Error!", MB_OK);
        delete[] Data;
    }

    DWORD major = HIWORD(pFixedFileInfo->dwProductVersionMS);
    DWORD minor = LOWORD(pFixedFileInfo->dwProductVersionMS);
    DWORD build = HIWORD(pFixedFileInfo->dwProductVersionLS);
    DWORD revision = LOWORD(pFixedFileInfo->dwProductVersionLS);

    g_ABI.Major = major;
    g_ABI.Minor = minor;
    g_ABI.Build = build;
    g_ABI.Revision = revision;
}
