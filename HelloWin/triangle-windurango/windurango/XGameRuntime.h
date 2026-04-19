#pragma once

#include <windows.h>


extern "C" __declspec(dllimport) HRESULT WINAPI XGameRuntimeInitialize(void);
extern "C" __declspec(dllimport) void WINAPI XGameRuntimeUninitialize(void);

