#include "Hooks.h"
#include "kernelx.h"

static DWORD ReasonForCall = 0;

void KernelxInitialize(HINSTANCE hinstDLL)
{
    if (ReasonForCall == DLL_PROCESS_ATTACH || ReasonForCall == DLL_THREAD_ATTACH)
    {
        XWinePatchImport(GetModuleHandleW(nullptr), GetRuntimeModule(),
                         "?GetActivationFactoryByPCWSTR@@YAJPEAXAEAVGuid@Platform@@PEAPEAX@Z",
                         GetActivationFactoryRedirect);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    InitializeCriticalSection(&XmpAllocationHookLock);

    ReasonForCall = ul_reason_for_call;

    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
    {
        ReasonForCall = ul_reason_for_call;
        return TRUE;
    }

    return QueueUserAPC([](ULONG_PTR dwParam) { KernelxInitialize(reinterpret_cast<HINSTANCE>(dwParam)); },
                        GetCurrentThread(), reinterpret_cast<ULONG_PTR>(hModule)) != 0;
}