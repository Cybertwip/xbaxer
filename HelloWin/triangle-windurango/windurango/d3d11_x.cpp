#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace
{
HMODULE load_d3d11()
{
    static HMODULE module = LoadLibraryW(L"d3d11.dll");
    return module;
}

template <typename ProcType>
ProcType load_proc(const char *name)
{
    HMODULE module = load_d3d11();
    if (!module)
    {
        return nullptr;
    }
    return reinterpret_cast<ProcType>(GetProcAddress(module, name));
}
} // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI D3D10CreateBlob(SIZE_T numBytes, ID3DBlob **blob)
{
    using ProcType = HRESULT(WINAPI *)(SIZE_T, ID3DBlob **);
    auto *proc = load_proc<ProcType>("D3D10CreateBlob");
    if (!proc)
    {
        return E_NOTIMPL;
    }
    return proc(numBytes, blob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *adapter,
                                                                  D3D_DRIVER_TYPE driverType,
                                                                  HMODULE software,
                                                                  UINT flags,
                                                                  const D3D_FEATURE_LEVEL *featureLevels,
                                                                  UINT featureLevelCount,
                                                                  UINT sdkVersion,
                                                                  ID3D11Device **device,
                                                                  D3D_FEATURE_LEVEL *featureLevel,
                                                                  ID3D11DeviceContext **immediateContext)
{
    using ProcType = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                       UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    auto *proc = load_proc<ProcType>("D3D11CreateDevice");
    if (!proc)
    {
        return E_NOTIMPL;
    }
    return proc(adapter, driverType, software, flags, featureLevels, featureLevelCount, sdkVersion, device,
                featureLevel, immediateContext);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *featureLevels, UINT featureLevelCount, UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC *swapChainDesc, IDXGISwapChain **swapChain, ID3D11Device **device,
    D3D_FEATURE_LEVEL *featureLevel, ID3D11DeviceContext **immediateContext)
{
    using ProcType = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                       UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                       D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    auto *proc = load_proc<ProcType>("D3D11CreateDeviceAndSwapChain");
    if (!proc)
    {
        return E_NOTIMPL;
    }
    return proc(adapter, driverType, software, flags, featureLevels, featureLevelCount, sdkVersion, swapChainDesc,
                swapChain, device, featureLevel, immediateContext);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3D11XCreateDeviceX(const void *, void **, void **)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3D11XCreateDeviceXAndSwapChain1(const void *,
                                                                                  const DXGI_SWAP_CHAIN_DESC1 *,
                                                                                  void **, void **, void **)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DAllocateGraphicsMemory(SIZE_T, UINT64, void *, int, void **)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DConfigureVirtualMemory(UINT64)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DFreeGraphicsMemory(void *)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DMapEsramMemory(UINT, void *, UINT, const UINT *)
{
    return E_NOTIMPL;
}

struct DXGIX_FRAME_STATISTICS
{
    UINT64 CPUTimePresentCalled;
    UINT64 CPUTimeAddedToQueue;
    UINT QueueLengthAddedToQueue;
    UINT64 CPUTimeFrameComplete;
    UINT64 GPUTimeFrameComplete;
    UINT64 GPUCountTitleUsed;
    UINT64 GPUCountSystemUsed;
    UINT64 CPUTimeVSync;
    UINT64 GPUTimeVSync;
    UINT64 CPUTimeFlip;
    UINT64 GPUTimeFlip;
    UINT64 VSyncCount;
    float PercentScanned;
    void *Cookie[2];
};

extern "C" __declspec(dllexport) HRESULT WINAPI DXGIXGetFrameStatistics(UINT, DXGIX_FRAME_STATISTICS *statistics)
{
    if (!statistics)
    {
        return E_INVALIDARG;
    }
    ZeroMemory(statistics, sizeof(*statistics));
    statistics->PercentScanned = 100.0f;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DXGIXPresentArray(UINT, UINT, UINT, UINT, void **, const void *)
{
    return E_NOTIMPL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DXGIXSetVLineNotification(UINT, UINT, HANDLE)
{
    return E_NOTIMPL;
}
