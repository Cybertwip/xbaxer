#include "CurrentApp.h"

HRESULT EraXboxUserLicenseInformationWrapper::QueryInterface(const IID &riid, void **ppvObject)
{
    if (riid == __uuidof(EraIXboxUserLicenseInformation))
    {
        *ppvObject = reinterpret_cast<EraICurrentApp*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG EraXboxUserLicenseInformationWrapper::AddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

ULONG EraXboxUserLicenseInformationWrapper::Release()
{
    ULONG refCount = InterlockedDecrement(&m_RefCount);
    if (!refCount) delete this;
    return refCount;
}

HRESULT EraXboxUserLicenseInformationWrapper::GetIids(ULONG *iidCount, IID **iids)
{
    return E_NOTIMPL;
}

HRESULT EraXboxUserLicenseInformationWrapper::GetRuntimeClassName(HSTRING *className)
{
    return E_NOTIMPL;
}

HRESULT EraXboxUserLicenseInformationWrapper::GetTrustLevel(TrustLevel *trustLevel)
{
    return E_NOTIMPL;
}

HRESULT EraXboxUserLicenseInformationWrapper::get_CurrentLicenseUserXuid(winrt::hstring *value)
{
    *value = L"0";
    return S_OK;
}

HRESULT EraLicenseInformationWrapper::QueryInterface(const IID &riid, void **ppvObject)
{
    if (riid == __uuidof(EraILicenseInformation))
    {
        *ppvObject = reinterpret_cast<EraICurrentApp*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(EraIXboxUserLicenseInformation))
    {
        *ppvObject = reinterpret_cast<EraIXboxUserLicenseInformation*>(new EraXboxUserLicenseInformationWrapper());
        AddRef();
        return S_OK;
    }

    HRESULT hr = m_realLicenseInformation->QueryInterface(riid, ppvObject);
    if (FAILED(hr))
    {
        char iidstr[sizeof("{AAAAAAAA-BBBB-CCCC-DDEE-FFGGHHIIJJKK}")];
        OLECHAR iidwstr[sizeof(iidstr)];
        StringFromGUID2(riid, iidwstr, ARRAYSIZE(iidwstr));
        WideCharToMultiByte(CP_UTF8, 0, iidwstr, -1, iidstr, sizeof(iidstr), nullptr, nullptr);
        printf("[LicenseInformationWrapperX] Interface Not Implemented: %s\n", iidstr);
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG EraLicenseInformationWrapper::AddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

ULONG EraLicenseInformationWrapper::Release()
{
    ULONG refCount = InterlockedDecrement(&m_RefCount);
    if (!refCount) delete this;
    return refCount;
}

HRESULT EraLicenseInformationWrapper::GetIids(ULONG *iidCount, IID **iids)
{
    return m_realLicenseInformation->GetIids(iidCount, iids);
}

HRESULT EraLicenseInformationWrapper::GetRuntimeClassName(HSTRING *className)
{
    return m_realLicenseInformation->GetRuntimeClassName(className);
}

HRESULT EraLicenseInformationWrapper::GetTrustLevel(TrustLevel *trustLevel)
{
    return m_realLicenseInformation->GetTrustLevel(trustLevel);
}

HRESULT EraLicenseInformationWrapper::get_ProductLicenses(ABI::Windows::Foundation::Collections::__FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense_t **value)
{
    return E_NOTIMPL;
}

HRESULT EraLicenseInformationWrapper::get_IsActive(boolean *value)
{
    *value = true;
    return S_OK;
}

HRESULT EraLicenseInformationWrapper::get_IsTrial(boolean *value)
{
    *value = false;
    return S_OK;
}

HRESULT EraLicenseInformationWrapper::get_ExpirationDate(ABI::Windows::Foundation::DateTime *value)
{
    ABI::Windows::Foundation::DateTime time;
    time.UniversalTime = UINT64_MAX;

    *value = time;
    return S_OK;
}

HRESULT EraLicenseInformationWrapper::add_LicenseChanged(ABI::Windows::ApplicationModel::Store::ILicenseChangedEventHandler *handler, EventRegistrationToken *cookie)
{
    return S_OK;
}

HRESULT EraLicenseInformationWrapper::remove_LicenseChanged(EventRegistrationToken cookie)
{
    return S_OK;
}

HRESULT __stdcall EraCurrentAppWrapper::QueryInterface(REFIID riid, void **ppvObject)
{
    if (riid == __uuidof(EraICurrentApp))
    {
        *ppvObject = reinterpret_cast<EraICurrentApp*>(this);
        AddRef();
        return S_OK;
    }

    HRESULT hr = m_realCurrentApp->QueryInterface(riid, ppvObject);
    if (FAILED(hr))
    {
        char iidstr[sizeof("{AAAAAAAA-BBBB-CCCC-DDEE-FFGGHHIIJJKK}")];
        OLECHAR iidwstr[sizeof(iidstr)];
        StringFromGUID2(riid, iidwstr, ARRAYSIZE(iidwstr));
        WideCharToMultiByte(CP_UTF8, 0, iidwstr, -1, iidstr, sizeof(iidstr), nullptr, nullptr);
        printf("[EraCurrentAppWrapper] Interface Not Implemented: %s\n", iidstr);
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG __stdcall EraCurrentAppWrapper::AddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

ULONG __stdcall EraCurrentAppWrapper::Release()
{
    ULONG refCount = InterlockedDecrement(&m_RefCount);
    if (!refCount) delete this;
    return refCount;
}

HRESULT EraCurrentAppWrapper::GetIids(ULONG *iidCount, IID **iids)
{
    return m_realCurrentApp->GetIids(iidCount, iids);
}

HRESULT EraCurrentAppWrapper::GetRuntimeClassName(HSTRING *className)
{
    return m_realCurrentApp->GetRuntimeClassName(className);
}

HRESULT EraCurrentAppWrapper::GetTrustLevel(TrustLevel *trustLevel)
{
    return m_realCurrentApp->GetTrustLevel(trustLevel);
}

HRESULT EraCurrentAppWrapper::get_LicenseInformation(ABI::Windows::ApplicationModel::Store::ILicenseInformation **value)
{
    return E_NOTIMPL;
}

HRESULT EraCurrentAppWrapper::get_LinkUri(ABI::Windows::Foundation::IUriRuntimeClass **value)
{
    return m_realCurrentApp->get_LinkUri(value);
}

HRESULT EraCurrentAppWrapper::get_AppId(GUID *value)
{
    return m_realCurrentApp->get_AppId(value);
}

HRESULT EraCurrentAppWrapper::RequestAppPurchaseAsync(boolean includeReceipt, ABI::Windows::Foundation::__FIAsyncOperation_1_HSTRING_t **requestAppPurchaseOperation)
{
    return m_realCurrentApp->RequestAppPurchaseAsync(includeReceipt, requestAppPurchaseOperation);
}

HRESULT EraCurrentAppWrapper::RequestProductPurchaseAsync(HSTRING productId, boolean includeReceipt, ABI::Windows::Foundation::__FIAsyncOperation_1_HSTRING_t **requestProductPurchaseOperation)
{
    return S_OK;
}

HRESULT EraCurrentAppWrapper::LoadListingInformationAsync(ABI::Windows::Foundation::__FIAsyncOperation_1_Windows__CApplicationModel__CStore__CListingInformation_t **loadListingOperation)
{
    return m_realCurrentApp->LoadListingInformationAsync(loadListingOperation);
}

HRESULT EraCurrentAppWrapper::GetAppReceiptAsync(
    ABI::Windows::Foundation::__FIAsyncOperation_1_HSTRING_t **appReceiptOperation)
{
    return m_realCurrentApp->GetAppReceiptAsync(appReceiptOperation);
}

HRESULT EraCurrentAppWrapper::GetProductReceiptAsync(
    HSTRING productId, ABI::Windows::Foundation::__FIAsyncOperation_1_HSTRING_t **getProductReceiptOperation)
{
    return m_realCurrentApp->GetProductReceiptAsync(productId, getProductReceiptOperation);
}