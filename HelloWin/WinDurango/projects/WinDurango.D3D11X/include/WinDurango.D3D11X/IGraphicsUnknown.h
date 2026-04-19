#pragma once
#include "d3d11.x.h"

template <abi_t ABI> class GraphicsUnknown : public xbox::IGraphicsUnknown<ABI>
{
  public:
    //
    // IUnknown
    //
    HRESULT QueryInterface(REFIID riid, void **ppvObject);
    ULONG AddRef();
    ULONG Release();
};

#undef ABI_INTERFACE
#define ABI_INTERFACE(ABI) GraphicsUnknown<ABI>
D3D11_DECLARE_ABI_TEMPLATES(extern);