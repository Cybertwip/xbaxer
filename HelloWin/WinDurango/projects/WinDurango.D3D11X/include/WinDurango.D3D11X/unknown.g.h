#pragma once
#ifndef __unknown_g_h__
#define __unknown_g_h__

#include "xcom/base.h"

namespace xbox
{
    template <abi_t ABI> struct IGraphicsUnknown;

    template <abi_t ABI> struct IGraphicsUnknownVtbl;

    template <abi_t ABI> struct __declspec(uuid("bcfaae29-e1a2-4b9a-aafc-55b9ff21fa54")) IGraphicsUnwrap;

    template <abi_t ABI> struct IGraphicsUnwrapVtbl;

    namespace details
    {
        template <abi_t ABI> struct IGraphicsUnknownData
        {
            ULONG m_RefCount;
        };

        template <abi_t ABI>
            requires(ABI >= abi_t{10, 0, 14393, 2152} && ABI < abi_t{10, 0, 19041, 3453})
        struct IGraphicsUnknownData<ABI>
        {
            ULONG m_DeviceIndex : 3;
            ULONG m_PrivateDataPresent : 1;
            ULONG m_Reserved : 28;
            ULONG m_RefCount;
        };

        template <abi_t ABI>
            requires(ABI >= abi_t{10, 0, 19041, 3453})
        struct IGraphicsUnknownData<ABI>
        {
            ULONG m_DeviceIndex : 3;
            ULONG m_PrivateDataPresent : 1;
            ULONG m_DisableCollectiveRefCount : 1;
            ULONG m_Reserved : 27;
            ULONG m_RefCount;
        };
    } // namespace details

    template <abi_t ABI> struct IGraphicsUnknown : details::IGraphicsUnknownData<ABI>
    {
        virtual HRESULT QueryInterface(REFIID riid, void **ppvObject) = 0;
        virtual ULONG AddRef() = 0;
        virtual ULONG Release() = 0;
    };

    template <abi_t ABI> struct IGraphicsUnknownVtbl
    {
        HRESULT (*QueryInterface)(void *, REFIID riid, void **ppvObject);
        ULONG (*AddRef)(void *);
        ULONG (*Release)(void *);
    };

    template <abi_t ABI> struct IGraphicsUnwrap
    {
    };

    template <abi_t ABI> struct IGraphicsUnwrapVtbl
    {
    };
} // namespace xbox

DECLARE_ABI_UUIDOF_HELPER(xbox::IGraphicsUnknown, 0xACEEEA63, 0xE0A9, 0x4A1C, 0xBB, 0xEC, 0x71, 0xB2, 0xF4, 0x85, 0xF7,
                          0x58)
DECLARE_ABI_UUIDOF_HELPER(xbox::IGraphicsUnwrap, 0xBCFAAE29, 0xE1A2, 0x4B9A, 0xAA, 0xFC, 0x55, 0xB9, 0xFF, 0x21, 0xFA,
                          0x54)

#define D3D11_DECLARE_ABI_TEMPLATES(prefix)                                                                            \
    prefix template class ABI_INTERFACE((abi_t{0, 0, 0, 0}));                                                          \
    prefix template class ABI_INTERFACE((abi_t{10, 0, 14393, 2152}));                                                  \
    prefix template class ABI_INTERFACE((abi_t{10, 0, 19041, 3453}))

#endif // __unknown_g_h__
