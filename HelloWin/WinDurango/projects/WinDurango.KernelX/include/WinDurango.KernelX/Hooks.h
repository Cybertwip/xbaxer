#pragma once
#pragma comment(lib, "runtimeobject.lib")
#include <Shlwapi.h>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <string>
#include <windows.applicationmodel.core.h>
#include <windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/windows.storage.provider.h>
#include <wrl/client.h>
#include <detours.h>

typedef int32_t (__stdcall *GetActivationFactory_t)(HSTRING classId, IActivationFactory** factory);

// Provided by XWine1, all credits to them.
//(This is from the old impl but we can use it just fine).
HRESULT XWineGetImport(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ LPCSTR Import,
                       _Out_ PIMAGE_THUNK_DATA *pThunk);
HRESULT XWinePatchImport(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ PCSTR Import, _In_ PVOID Function);
HRESULT PatchNeededImports(_In_opt_ HMODULE Module, _In_ HMODULE ImportModule, _In_ PCSTR Import, _In_ PVOID Function);
HMODULE GetRuntimeModule();
inline HRESULT WINAPI EraRoGetActivationFactory(HSTRING classId, REFIID iid, void **factory);
HRESULT WINAPI GetActivationFactoryRedirect(PCWSTR str, REFIID riid, void **ppFactory);

HRESULT STDMETHODCALLTYPE EraAppActivateInstance(IActivationFactory *thisptr, IInspectable **instance);

template <typename T> inline T GetVTableMethod(void *table_base, std::uintptr_t index);

inline bool IsClassName(HSTRING classId, const char *classIdName)
{
    const wchar_t *classIdString = WindowsGetStringRawBuffer(classId, nullptr);
    std::wstring classIdWString(classIdString);
    const std::string classIdStringUTF8(classIdWString.begin(), classIdWString.end());

    return (classIdStringUTF8 == classIdName);
}

HRESULT(WINAPI *TrueActivateInstance)(IActivationFactory *thisptr, IInspectable **instance) = nullptr;