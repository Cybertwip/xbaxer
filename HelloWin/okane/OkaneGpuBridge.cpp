#include "pch.h"
#include "include/OkaneBridge.h"
#include "OkaneGpuHasher.h"

extern "C"
{
    OkaneGpuHandle OkaneGpu_Create(void)
    {
        auto *gpu = new (std::nothrow) OkaneGpuHasher(nullptr);
        if (!gpu)
            return nullptr;
        if (!gpu->IsValid())
        {
            delete gpu;
            return nullptr;
        }
        return static_cast<OkaneGpuHandle>(gpu);
    }

    void OkaneGpu_Destroy(OkaneGpuHandle handle)
    {
        delete static_cast<OkaneGpuHasher *>(handle);
    }

    int OkaneGpu_Process(OkaneGpuHandle handle,
                         const uint8_t *header,
                         const uint8_t *target,
                         uint8_t *outHeader)
    {
        if (!handle || !header || !target || !outHeader)
            return 0;
        auto *gpu = static_cast<OkaneGpuHasher *>(handle);
        return gpu->Process(header, target, outHeader) ? 1 : 0;
    }
}
