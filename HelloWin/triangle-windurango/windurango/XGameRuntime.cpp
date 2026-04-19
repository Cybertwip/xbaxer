#include "XGameRuntime.h"

extern "C" __declspec(dllexport) HRESULT WINAPI XGameRuntimeInitialize(void)
{
    return S_OK;
}

extern "C" __declspec(dllexport) VOID WINAPI XGameRuntimeUninitialize()
{

}


