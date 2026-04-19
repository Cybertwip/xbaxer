#pragma once
#include <windows.h>

void DisableBitstreamOut();
HRESULT EnableSpatialAudio();
void RestoreBitstreamOut();
DWORD_PTR SetWasapiThreadAffinityMask(DWORD_PTR dwThreadAffinityMask);
void RefreshWasapiDeviceList();