#include "pch.h"
#include "OkaneGpuHasher.h"

#include <chrono>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr uint32_t kNumWorkgroups = 4096;

    std::vector<uint8_t> ReadShaderBlob(const wchar_t *name)
    {
        std::ifstream inFile(name, std::ios::in | std::ios::binary | std::ios::ate);
#ifdef _GAMING_DESKTOP
        if (!inFile)
        {
            wchar_t moduleName[_MAX_PATH] = {};
            if (!GetModuleFileNameW(nullptr, moduleName, _MAX_PATH))
                return {};
            wchar_t drive[_MAX_DRIVE], path[_MAX_PATH], filename[_MAX_PATH];
            if (_wsplitpath_s(moduleName, drive, _MAX_DRIVE, path, _MAX_PATH, nullptr, 0, nullptr, 0))
                return {};
            if (_wmakepath_s(filename, _MAX_PATH, drive, path, name, nullptr))
                return {};
            inFile.open(filename, std::ios::in | std::ios::binary | std::ios::ate);
        }
#endif
        if (!inFile) return {};
        const auto len = static_cast<size_t>(inFile.tellg());
        std::vector<uint8_t> blob(len);
        inFile.seekg(0, std::ios::beg);
        inFile.read(reinterpret_cast<char *>(blob.data()), static_cast<std::streamsize>(len));
        return blob;
    }

    ComPtr<ID3D12Resource> MakeBuffer(ID3D12Device *dev, uint64_t size,
                                       D3D12_HEAP_TYPE heap,
                                       D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON)
    {
        const auto hp = CD3DX12_HEAP_PROPERTIES(heap);
        const auto rd = CD3DX12_RESOURCE_DESC::Buffer(size, flags);
        ComPtr<ID3D12Resource> r;
        DX::ThrowIfFailed(dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
            IID_GRAPHICS_PPV_ARGS(r.ReleaseAndGetAddressOf())));
        return r;
    }

    void Upload(ID3D12Resource *buf, const void *data, size_t sz)
    {
        void *p = nullptr;
        const CD3DX12_RANGE rr(0, 0);
        DX::ThrowIfFailed(buf->Map(0, &rr, &p));
        std::memcpy(p, data, sz);
        buf->Unmap(0, nullptr);
    }
}

// ---- lifetime ---------------------------------------------------------------

OkaneGpuHasher::OkaneGpuHasher(ID3D12Device* device)
{
    if (device)
        m_device = device;
    try { m_valid = CreateComputeResources(); }
    catch (...) { m_valid = false; }
}

OkaneGpuHasher::~OkaneGpuHasher()
{
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}

bool OkaneGpuHasher::IsValid() const { return m_valid; }

// ---- one-time setup ---------------------------------------------------------

bool OkaneGpuHasher::CreateComputeResources()
{
    // If no device was provided externally, create our own (desktop only).
    if (!m_device)
    {
#ifdef _GAMING_DESKTOP
        {
            ComPtr<IDXGIFactory4> factory;
            DX::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())));
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT i = 0;
                 factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;
                 ++i)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                IID_GRAPHICS_PPV_ARGS(m_device.ReleaseAndGetAddressOf()))))
                    break;
            }
            if (!m_device) return false;
        }
#else
        // On Xbox, a device MUST be provided externally.
        return false;
#endif
    }

    // Compute queue
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    DX::ThrowIfFailed(m_device->CreateCommandQueue(&qd,
        IID_GRAPHICS_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf())));
    DX::ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
        IID_GRAPHICS_PPV_ARGS(m_commandAllocator.ReleaseAndGetAddressOf())));
    DX::ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
        m_commandAllocator.Get(), nullptr,
        IID_GRAPHICS_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));
    m_commandList->Close();

    // Fence
    DX::ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_GRAPHICS_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
    m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (!m_fenceEvent) return false;

    // Root signature: 3 root descriptors — SRV(t0), SRV(t1), UAV(u0)
    CD3DX12_ROOT_PARAMETER1 rp[3] = {};
    rp[0].InitAsShaderResourceView(0);   // t0 inputData (midstate+params)
    rp[1].InitAsShaderResourceView(1);   // t1 inputTarget
    rp[2].InitAsUnorderedAccessView(0);  // u0 outputData (found+nonce)

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
    rsd.Init_1_1(3, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3DX12SerializeVersionedRootSignature(&rsd,
            D3D_ROOT_SIGNATURE_VERSION_1_1, sig.GetAddressOf(), err.GetAddressOf())))
        return false;

    DX::ThrowIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(),
        sig->GetBufferSize(),
        IID_GRAPHICS_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));

    // Pipeline state
    auto cs = ReadShaderBlob(L"SHA256Compute.cso");
    if (cs.empty()) return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = m_rootSignature.Get();
    pd.CS = {cs.data(), cs.size()};
    DX::ThrowIfFailed(m_device->CreateComputePipelineState(&pd,
        IID_GRAPHICS_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));

    // Buffers — sized for new midstate API
    auto *d = m_device.Get();
    m_uploadData         = MakeBuffer(d, 80, D3D12_HEAP_TYPE_UPLOAD,
                                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    m_uploadTarget       = MakeBuffer(d, 32, D3D12_HEAP_TYPE_UPLOAD,
                                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    m_inputDataBuffer    = MakeBuffer(d, 80, D3D12_HEAP_TYPE_DEFAULT);
    m_inputTargetBuffer  = MakeBuffer(d, 32, D3D12_HEAP_TYPE_DEFAULT);
    m_outputBuffer       = MakeBuffer(d, 8, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_readbackBuffer     = MakeBuffer(d, 8, D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    return true;
}

// ---- legacy dispatch (not used for stratum, kept for compat) ----------------

bool OkaneGpuHasher::Process(const uint8_t header[80],
                              const uint8_t target[32],
                              uint8_t outHeader[80])
{
    // Stub: not used with new midstate API.
    (void)header; (void)target; (void)outHeader;
    return false;
}

// ---- midstate dispatch ------------------------------------------------------

bool OkaneGpuHasher::ProcessMidstate(const GpuMineParams& params, GpuMineResult& result)
{
    if (!m_valid) return false;

    result.found = false;
    result.nonce = 0;

    // Pack input data: 80 bytes total
    // [0..31]  midstate (8 × uint32)
    // [32..43] secondBlockW[3] (3 × uint32)
    // [44..47] nonceStart (uint32)
    // [48..79] padding (zeros)
    uint8_t inputData[80] = {};
    std::memcpy(inputData + 0, params.midstate, 32);
    std::memcpy(inputData + 32, params.secondBlockW, 12);
    std::memcpy(inputData + 44, &params.nonceStart, 4);

    Upload(m_uploadData.Get(), inputData, 80);
    Upload(m_uploadTarget.Get(), params.target, 32);

    // Zero the output (found flag + nonce).
    {
        void *p = nullptr;
        const CD3DX12_RANGE rr(0, 0);
        DX::ThrowIfFailed(m_readbackBuffer->Map(0, &rr, &p));
        std::memset(p, 0, 8);
        m_readbackBuffer->Unmap(0, nullptr);
    }

    // Record command list.
    DX::ThrowIfFailed(m_commandAllocator->Reset());
    DX::ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Transition default buffers to COPY_DEST.
    {
        D3D12_RESOURCE_BARRIER b[3];
        b[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputDataBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        b[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputTargetBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        b[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        m_commandList->ResourceBarrier(3, b);
    }

    // Copy uploads into default heap.
    m_commandList->CopyResource(m_inputDataBuffer.Get(), m_uploadData.Get());
    m_commandList->CopyResource(m_inputTargetBuffer.Get(), m_uploadTarget.Get());

    // Zero the output buffer by copying 8 zero bytes from upload buffer offset 72 (the padding area).
    // Actually we need a clean approach: copy from readback (already zeroed).
    // Simpler: just use ClearUnorderedAccessViewUint.
    // But that requires a descriptor heap. Let's use a small upload copy instead.
    {
        // Upload 8 zero bytes via the tail of the uploadData buffer (bytes 72-79 are padding/zero).
        D3D12_BOX srcBox = { 72, 0, 0, 80, 1, 1 };
        m_commandList->CopyBufferRegion(m_outputBuffer.Get(), 0, m_uploadData.Get(), 72, 8);
    }

    // Transition for dispatch.
    {
        D3D12_RESOURCE_BARRIER b[3];
        b[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputDataBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        b[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputTargetBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        b[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(3, b);
    }

    // Bind and dispatch.
    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->SetComputeRootSignature(m_rootSignature.Get());
    m_commandList->SetComputeRootShaderResourceView(0,
        m_inputDataBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootShaderResourceView(1,
        m_inputTargetBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootUnorderedAccessView(2,
        m_outputBuffer->GetGPUVirtualAddress());
    m_commandList->Dispatch(kNumWorkgroups, 1, 1);

    // Copy output -> readback.
    {
        auto b = CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_commandList->ResourceBarrier(1, &b);
    }
    m_commandList->CopyResource(m_readbackBuffer.Get(), m_outputBuffer.Get());

    // Transition back to COMMON for the next call.
    {
        D3D12_RESOURCE_BARRIER b[3];
        b[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputDataBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        b[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_inputTargetBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        b[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        m_commandList->ResourceBarrier(3, b);
    }

    DX::ThrowIfFailed(m_commandList->Close());

    // Execute and fence-wait (polling approach — more reliable on Xbox shared device).
    ID3D12CommandList *lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);

    ++m_fenceValue;
    HRESULT sigHr = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    if (FAILED(sigHr))
    {
        char msg[128];
        sprintf_s(msg, "[okane][gpu] Signal failed hr=0x%08x\n", static_cast<unsigned>(sigHr));
        OutputDebugStringA(msg);
        return false;
    }

    // Spin-poll the fence with a timeout (avoids event issues on Xbox).
    {
        auto start = std::chrono::steady_clock::now();
        while (m_fence->GetCompletedValue() < m_fenceValue)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > 5000)
            {
                OutputDebugStringA("[okane][gpu] fence poll timed out after 5s!\n");
                return false;
            }
            SwitchToThread();
        }
    }

    // Read back result: [0..3] found flag, [4..7] nonce.
    void *mapped = nullptr;
    DX::ThrowIfFailed(m_readbackBuffer->Map(0, nullptr, &mapped));

    uint32_t foundFlag = 0;
    uint32_t foundNonce = 0;
    std::memcpy(&foundFlag, mapped, 4);
    std::memcpy(&foundNonce, static_cast<const uint8_t*>(mapped) + 4, 4);

    m_readbackBuffer->Unmap(0, nullptr);

    if (foundFlag != 0)
    {
        result.found = true;
        result.nonce = foundNonce;
    }

    return true;
}
