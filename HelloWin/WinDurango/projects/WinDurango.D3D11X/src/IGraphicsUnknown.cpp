#include "IGraphicsUnknown.h"

//
// IUnknown
//
template <abi_t ABI> HRESULT GraphicsUnknown<ABI>::QueryInterface(REFIID riid, void **ppvObject)
{
    IMPLEMENT_STUB();
    return E_NOTIMPL;
}

template <abi_t ABI> ULONG GraphicsUnknown<ABI>::AddRef()
{
    IMPLEMENT_STUB();
    return {};
}

template <abi_t ABI> ULONG GraphicsUnknown<ABI>::Release()
{
    IMPLEMENT_STUB();
    return {};
}

#undef ABI_INTERFACE
#define ABI_INTERFACE(ABI) GraphicsUnknown<ABI>
D3D11_DECLARE_ABI_TEMPLATES();