//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "include/OkaneBridge.h"
#include "OkaneGpuHasher.h"
#include "OkaneMiner.h"
#include "StepTimer.h"

// A basic game implementation that creates a D3D12 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:
    Game() noexcept(false);
    ~Game();

    Game(Game &&) = delete;
    Game &operator=(Game &&) = delete;

    Game(Game const &) = delete;
    Game &operator=(Game const &) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated() {}
    void OnDeactivated() {}
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnWindowSizeChanged(int width, int height);
    void OnKeyDown(UINT vk);

    // Properties
    void GetDefaultSize(int &width, int &height) const noexcept;

private:
    void Update(DX::StepTimer const &timer);
    void Render();

    void Clear();
    void RefreshDynamicGeometry();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    // Device resources.
    std::unique_ptr<DX::DeviceResources> m_deviceResources;

    // Rendering loop timer.
    uint64_t m_frame;
    DX::StepTimer m_timer;

    // Local render/menu state.
    OkaneFrameState m_goFrameState{};

    // Direct3D 12 objects.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    // Menu overlay includes selection bars plus a lightweight text system.
    static constexpr uint32_t kMaxMenuQuadVerts = 65536;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_menuVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_menuVertexBufferView;
    uint32_t m_menuVertexCount = 0;

    bool m_bootstrapPresented          = false;
    char m_pendingConfigPath[MAX_PATH] = {};

    // Native C++ miner (ported from Go bridge).
    OkaneConfig                            m_config{};
    std::unique_ptr<OkaneRpcClient>        m_rpcClient;
    std::unique_ptr<OkaneStratumClient>    m_stratumClient;
    std::unique_ptr<OkaneGpuHasher>        m_gpuHasher;
    std::unique_ptr<OkaneMiner>            m_miner;
    void EnsureMiner();
};
