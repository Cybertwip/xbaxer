#include "Hooks.h"
#include "WinDurango.Common/Interfaces/Storage/Directory.h"
#include "WinDurango.Common/WinDurango.h"
#include "WinDurango.Implementation.WinRT/Interfaces/Storage/Directory.h"
#include "CurrentApp.h"

using namespace ABI::Windows::ApplicationModel::Store;

std::shared_ptr<wd::common::WinDurango> winDurango;
GetActivationFactory_t p_GetActivationFactory;

HRESULT XWineGetImport(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ LPCSTR Import,
                       _Out_ PIMAGE_THUNK_DATA *pThunk)
{
    if (ImportModule == nullptr)
        return E_INVALIDARG;

    if (pThunk == nullptr)
        return E_POINTER;

    if (Module == nullptr)
        Module = GetModuleHandleW(nullptr);

    auto dosHeader = (PIMAGE_DOS_HEADER)Module;
    auto ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)Module + dosHeader->e_lfanew);
    auto directory = &ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (directory->VirtualAddress == 0)
        return E_FAIL;

    auto peImports = (PIMAGE_IMPORT_DESCRIPTOR)((PBYTE)Module + directory->VirtualAddress);

    for (size_t i = 0; peImports[i].Name; i++)
    {
        if (GetModuleHandleA((LPCSTR)((PBYTE)Module + peImports[i].Name)) != ImportModule)
            continue;

        auto iatThunks = (PIMAGE_THUNK_DATA)((PBYTE)Module + peImports[i].FirstThunk);
        auto intThunks = (PIMAGE_THUNK_DATA)((PBYTE)Module + peImports[i].OriginalFirstThunk);

        for (size_t j = 0; intThunks[j].u1.AddressOfData; j++)
        {
            if ((intThunks[j].u1.AddressOfData & IMAGE_ORDINAL_FLAG) != 0)
            {
                if (!IS_INTRESOURCE(Import))
                    continue;

                if (((intThunks[j].u1.Ordinal & ~IMAGE_ORDINAL_FLAG) == (ULONG_PTR)Import))
                {
                    *pThunk = &iatThunks[j];
                    return S_OK;
                }

                continue;
            }

            if (strcmp(((PIMAGE_IMPORT_BY_NAME)((PBYTE)Module + intThunks[j].u1.AddressOfData))->Name, Import))
                continue;

            *pThunk = &iatThunks[j];
            return S_OK;
        }
    }

    *pThunk = nullptr;
    return E_FAIL;
}

HRESULT XWinePatchImport(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ PCSTR Import, _In_ PVOID Function)
{
    DWORD protect;
    PIMAGE_THUNK_DATA pThunk;
    HRESULT hr = XWineGetImport(Module, ImportModule, Import, &pThunk);
    if (FAILED(hr))
        return hr;

    if (!VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &protect))
        return GetLastError();

    pThunk->u1.Function = (ULONG_PTR)Function;

    if (!VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), protect, &protect))
        return GetLastError();

    return S_OK;
}

HRESULT PatchNeededImports(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ PCSTR Import, _In_ PVOID Function)
{
    PIMAGE_THUNK_DATA pThunk;
    HRESULT hr = XWineGetImport(Module, ImportModule, Import, &pThunk);
    if (FAILED(hr))
        return hr;

    return XWinePatchImport(Module, ImportModule, Import, Function);
}

HMODULE GetRuntimeModule()
{
    std::array<const wchar_t *, 3> modules = {L"vccorlib140.dll", L"vccorlib110.dll", L"vccorlib120.dll"};
    static HMODULE hModule = nullptr;
    if (hModule != nullptr)
    {
        return hModule;
    }

    for (auto &module : modules)
    {
        hModule = GetModuleHandleW(module);
        if (hModule != nullptr)
        {
            break;
        }
    }

    return hModule;
}

template <typename T> inline T GetVTableMethod(void *table_base, std::uintptr_t index)
{
    return (T)((*reinterpret_cast<std::uintptr_t **>(table_base))[index]);
}

HRESULT __stdcall EraAppActivateInstance(IActivationFactory *thisptr, IInspectable **instance)
{
    *instance = reinterpret_cast<ILicenseInformation *>(
        new EraLicenseInformationWrapper(reinterpret_cast<ILicenseInformation *>(*instance)));
    return S_OK;
}


inline HRESULT WINAPI EraRoGetActivationFactory(HSTRING classId, REFIID iid, void **factory)
{
    const wchar_t *rawString = WindowsGetStringRawBuffer(classId, nullptr);

    MessageBoxW(nullptr, rawString, L"EraRoGetActivationFactory", MB_OK | MB_ICONERROR);

    std::wstring rsws(rawString);
    std::string rss(rsws.begin(), rsws.end());

    winDurango->log.Log("WinDurango::KernelX", "EraRoGetActivationFactory: {}", rss);

    if (IsClassName(classId, "Windows.ApplicationModel.Store.CurrentApp"))
    {
        HRESULT hr = RoGetActivationFactory(classId, iid, factory);

        if (FAILED(hr))
            return hr;

        TrueActivateInstance = GetVTableMethod<decltype(TrueActivateInstance)>(*factory, 6);
        
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID *>(&TrueActivateInstance), reinterpret_cast<PVOID>(EraAppActivateInstance));
        DetourTransactionCommit();
    }

    if (!p_GetActivationFactory) 
    {
        HINSTANCE hGetActivationFactoryDLL = LoadLibrary("winrt_x.dll");

        if (!hGetActivationFactoryDLL) 
        {
            winDurango->log.Error("WinDurango::KernelX", "Failed to load winrt_x.dll");
            return EXIT_FAILURE;
        }

        p_GetActivationFactory = (GetActivationFactory_t)GetProcAddress(hGetActivationFactoryDLL, "GetActivationFactory");

        if (!p_GetActivationFactory) 
        {
            winDurango->log.Error("WinDurango::KernelX", "Failed to load GetActivationFactory");
            return EXIT_FAILURE;
        }
    }

    Microsoft::WRL::ComPtr<IActivationFactory> i_factory;

    HRESULT hr = p_GetActivationFactory(classId, i_factory.GetAddressOf());

    if (SUCCEEDED(hr))
    {
        return i_factory.CopyTo(iid, factory);
    }

    return E_NOINTERFACE;
}

HRESULT WINAPI GetActivationFactoryRedirect(PCWSTR str, REFIID riid, void **ppFactory)
{
    winDurango = wd::common::WinDurango::GetInstance();
    if (!winDurango->inited())
    {
#ifndef CROSS_PLATFORM
        std::shared_ptr<wd::common::interfaces::storage::Directory> rootDir =
            std::make_shared<wd::impl::winrt::interfaces::storage::WinRTDirectory>("");
        winDurango->Init(rootDir);
#endif
    }

    HRESULT hr = 0;
    HSTRING className;
    HSTRING_HEADER classNameHeader;

    hr = WindowsCreateStringReference(str, wcslen(str), &classNameHeader, &className);
    if (FAILED(hr))
        return hr;

    hr = EraRoGetActivationFactory(className, riid, ppFactory);

    if (FAILED(hr))
        DebugBreak();

    WindowsDeleteString(className);
    return hr;
}
