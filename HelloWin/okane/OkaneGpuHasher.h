#pragma once

#include <cstdint>
#include <array>
#include <memory>

#include <wrl/client.h>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12Fence;

/// Parameters for midstate-optimized GPU mining dispatch.
struct GpuMineParams
{
    uint32_t midstate[8];        // SHA-256 state after processing first 64 header bytes
    uint32_t secondBlockW[3];    // W[0]=merkle_tail, W[1]=ntime, W[2]=nbits (big-endian)
    uint32_t nonceStart;         // Starting nonce for this dispatch
    uint8_t  target[32];         // 32-byte target in LE storage (matching CPU Hash32)
};

struct GpuMineResult
{
    bool     found;
    uint32_t nonce;
};

/// D3D12 compute-shader GPU hasher for SHA-256 double-hash mining.
/// Midstate-optimized: CPU pre-computes first block SHA state, GPU only varies nonce.
class OkaneGpuHasher final
{
public:
    /// Construct with an external D3D12 device (shared with the game's renderer).
    /// On Xbox only one device is allowed, so this avoids the "multiple driver flavors" error.
    explicit OkaneGpuHasher(ID3D12Device* device);
    ~OkaneGpuHasher();

    OkaneGpuHasher(const OkaneGpuHasher &) = delete;
    OkaneGpuHasher &operator=(const OkaneGpuHasher &) = delete;

    /// Returns true if the GPU hasher initialised successfully and is usable.
    bool IsValid() const;

    /// Legacy full-header API (kept for backward compat).
    bool Process(const uint8_t header[80],
                 const uint8_t target[32],
                 uint8_t outHeader[80]);

    /// Midstate-optimized dispatch.  Searches nonces starting from nonceStart,
    /// covering NUM_THREADS * iterations_per_thread nonces.
    /// Returns true if a valid nonce was found.
    bool ProcessMidstate(const GpuMineParams& params, GpuMineResult& result);

    /// Total number of nonces searched per ProcessMidstate call.
    /// 4096 workgroups × 256 threads = 1,048,576 threads; full 2^32 range.
    static constexpr uint32_t kThreadsPerDispatch = 4096u * 256u;

private:
    bool CreateComputeResources();

    Microsoft::WRL::ComPtr<ID3D12Device>              m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>         m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>     m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>  m_commandList;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>        m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>        m_pipelineState;

    // GPU buffers
    Microsoft::WRL::ComPtr<ID3D12Resource> m_inputDataBuffer;     // SRV – 80 bytes (midstate+params)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_inputTargetBuffer;   // SRV – 32 bytes
    Microsoft::WRL::ComPtr<ID3D12Resource> m_outputBuffer;        // UAV – 8 bytes (found+nonce)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_readbackBuffer;      // CPU-readable – 8 bytes

    // Upload heaps
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadData;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadTarget;

    Microsoft::WRL::ComPtr<ID3D12Fence>    m_fence;
    HANDLE                                 m_fenceEvent = nullptr;
    uint64_t                               m_fenceValue = 0;

    bool m_valid = false;
};
