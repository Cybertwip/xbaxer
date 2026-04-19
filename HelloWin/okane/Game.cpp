//
// Game.cpp
//

#include "pch.h"
#include "Game.h"

#include <array>

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;

namespace
{
const DirectX::XMVECTORF32 c_Background = {{{0.08f, 0.09f, 0.12f, 1.0f}}};

template <typename... TArgs>
void Tracef(const char* format, TArgs... args)
{
    char buffer[512] = {};
    sprintf_s(buffer, format, args...);
    OutputDebugStringA(buffer);
}

bool ShouldTraceFrame(uint64_t frame) noexcept
{
    return frame < 8 || frame == 15 || frame == 30 || frame == 60;
}

struct Vertex
{
    XMFLOAT4 position;
    XMFLOAT4 color;
};

void AppendQuad(std::vector<Vertex>& vertices, float left, float top, float right, float bottom,
                const XMFLOAT4& color, float z = 0.40f)
{
    vertices.push_back({{left, top, z, 1.0f}, color});
    vertices.push_back({{right, top, z, 1.0f}, color});
    vertices.push_back({{left, bottom, z, 1.0f}, color});
    vertices.push_back({{right, top, z, 1.0f}, color});
    vertices.push_back({{right, bottom, z, 1.0f}, color});
    vertices.push_back({{left, bottom, z, 1.0f}, color});
}

std::string ReadFixedString(const char* text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length] != '\0')
    {
        ++length;
    }

    return std::string(text, length);
}

std::string ToUpperAscii(std::string text)
{
    for (char& ch : text)
    {
        if (ch >= 'a' && ch <= 'z')
        {
            ch = static_cast<char>(ch - ('a' - 'A'));
        }
    }

    return text;
}

std::string ShortenForUi(std::string text, size_t maxChars)
{
    if (text.size() <= maxChars)
    {
        return text;
    }

    if (maxChars <= 3)
    {
        return text.substr(0, maxChars);
    }

    return text.substr(0, maxChars - 3) + "...";
}

char NormalizeGlyph(char ch)
{
    if (ch >= 'a' && ch <= 'z')
    {
        ch = static_cast<char>(ch - ('a' - 'A'));
    }

    switch (ch)
    {
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
    case ' ': case ':': case '.': case '-': case '+': case '/': case '(': case ')': case '?': case '#':
        return ch;
    default:
        return '?';
    }
}

std::array<uint8_t, 5> GlyphRows(char input)
{
    switch (NormalizeGlyph(input))
    {
    case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
    case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
    case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
    case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
    case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
    case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
    case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
    case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
    case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
    case 'J': return {0b111, 0b001, 0b001, 0b101, 0b010};
    case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
    case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
    case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
    case 'N': return {0b101, 0b111, 0b111, 0b111, 0b101};
    case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
    case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
    case 'Q': return {0b010, 0b101, 0b101, 0b111, 0b011};
    case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
    case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
    case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
    case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
    case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
    case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
    case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
    case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
    case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
    case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
    case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
    case '2': return {0b110, 0b001, 0b111, 0b100, 0b111};
    case '3': return {0b110, 0b001, 0b111, 0b001, 0b110};
    case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
    case '5': return {0b111, 0b100, 0b111, 0b001, 0b110};
    case '6': return {0b011, 0b100, 0b111, 0b101, 0b111};
    case '7': return {0b111, 0b001, 0b010, 0b010, 0b010};
    case '8': return {0b111, 0b101, 0b111, 0b101, 0b111};
    case '9': return {0b111, 0b101, 0b111, 0b001, 0b110};
    case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
    case '.': return {0b000, 0b000, 0b000, 0b000, 0b010};
    case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
    case '+': return {0b000, 0b010, 0b111, 0b010, 0b000};
    case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
    case '(': return {0b001, 0b010, 0b010, 0b010, 0b001};
    case ')': return {0b100, 0b010, 0b010, 0b010, 0b100};
    case '#': return {0b101, 0b111, 0b101, 0b111, 0b101};
    case ' ': return {0b000, 0b000, 0b000, 0b000, 0b000};
    default: return {0b111, 0b001, 0b011, 0b000, 0b010};
    }
}

void AppendGlyph(std::vector<Vertex>& vertices, char ch, float left, float top, float glyphWidth, float glyphHeight,
                 const XMFLOAT4& color)
{
    const auto rows = GlyphRows(ch);
    const float pixelWidth = glyphWidth / 3.0f;
    const float pixelHeight = glyphHeight / 5.0f;

    for (size_t row = 0; row < rows.size(); ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            if ((rows[row] & (1 << (2 - col))) == 0)
            {
                continue;
            }

            const float x0 = left + static_cast<float>(col) * pixelWidth;
            const float x1 = x0 + pixelWidth * 0.82f;
            const float y1 = top - static_cast<float>(row) * pixelHeight;
            const float y0 = y1 - pixelHeight * 0.82f;
            AppendQuad(vertices, x0, y1, x1, y0, color, 0.35f);
        }
    }
}

void AppendText(std::vector<Vertex>& vertices, const std::string& text, float left, float top,
                float glyphWidth, float glyphHeight, float advance, const XMFLOAT4& color)
{
    float penX = left;
    for (char ch : text)
    {
        AppendGlyph(vertices, ch, penX, top, glyphWidth, glyphHeight, color);
        penX += advance;
    }
}

bool StartsWith(const std::string& value, const char* prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string ExtractMenuValue(const OkaneFrameState& state, const char* prefix)
{
    for (uint32_t index = 0; index < state.menuItemCount && index < OKANE_MAX_MENU_ITEMS; ++index)
    {
        const std::string item = ReadFixedString(state.menuItems[index].label, OKANE_MAX_LABEL_LEN);
        if (StartsWith(item, prefix))
        {
            return item.substr(std::strlen(prefix));
        }
    }

    return {};
}

std::vector<uint8_t> ReadData(_In_z_ const wchar_t* name)
{
    std::ifstream inFile(name, std::ios::in | std::ios::binary | std::ios::ate);

#if defined(_GAMING_DESKTOP) || defined(_GAMING_XBOX)
    if (!inFile)
    {
        wchar_t moduleName[_MAX_PATH];
        if (!GetModuleFileNameW(nullptr, moduleName, _MAX_PATH))
        {
            throw std::exception("GetModuleFileNameW");
        }

        wchar_t drive[_MAX_DRIVE];
        wchar_t path[_MAX_PATH];

        if (_wsplitpath_s(moduleName, drive, _MAX_DRIVE, path, _MAX_PATH, nullptr, 0, nullptr, 0))
        {
            throw std::exception("_wsplitpath_s");
        }

        wchar_t filename[_MAX_PATH];
        if (_wmakepath_s(filename, _MAX_PATH, drive, path, name, nullptr))
        {
            throw std::exception("_wmakepath_s");
        }

        inFile.open(filename, std::ios::in | std::ios::binary | std::ios::ate);
    }
#endif

    if (!inFile)
    {
        throw std::exception("ReadData");
    }

    const std::streampos len = inFile.tellg();
    if (!inFile)
    {
        throw std::exception("ReadData");
    }

    std::vector<uint8_t> blob;
    blob.resize(size_t(len));

    inFile.seekg(0, std::ios::beg);
    if (!inFile)
    {
        throw std::exception("ReadData");
    }

    inFile.read(reinterpret_cast<char*>(blob.data()), len);
    if (!inFile)
    {
        throw std::exception("ReadData");
    }

    inFile.close();

    return blob;
}

static float Clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

XMFLOAT2 RotateAndOffset(const XMFLOAT2& point, float scale, float radians, float offsetX, float offsetY)
{
    const float scaledX = point.x * scale;
    const float scaledY = point.y * scale;
    const float s       = std::sin(radians);
    const float c       = std::cos(radians);

    return {
        (scaledX * c) - (scaledY * s) + offsetX,
        (scaledX * s) + (scaledY * c) + offsetY,
    };
}

std::array<Vertex, 3> BuildTriangleVertices(const OkaneFrameState& state)
{
    constexpr std::array<XMFLOAT2, 3> kBaseTriangle = {{
        {0.0f, 0.70f},
        {0.62f, -0.45f},
        {-0.62f, -0.45f},
    }};

    const float pulse = Clamp01(state.trianglePulse);
    const float scale = std::max(0.15f, state.triangleScale);
    const auto top    = RotateAndOffset(kBaseTriangle[0], scale, state.triangleRotationRadians, state.triangleOffsetX,
                                        state.triangleOffsetY);

    const auto right  = RotateAndOffset(kBaseTriangle[1], scale, state.triangleRotationRadians, state.triangleOffsetX,
                                        state.triangleOffsetY);
    const auto left   = RotateAndOffset(kBaseTriangle[2], scale, state.triangleRotationRadians, state.triangleOffsetX,
                                        state.triangleOffsetY);

    const XMFLOAT4 topColor = {
        Clamp01(state.triangleColor[0] + 0.20f * pulse),
        Clamp01(state.triangleColor[1] + 0.08f),
        Clamp01(state.triangleColor[2] + 0.04f),
        1.0f,
    };
    const XMFLOAT4 rightColor = {
        Clamp01(state.triangleColor[1] + 0.10f * pulse),
        Clamp01(state.triangleColor[2] + 0.18f),
        Clamp01(state.triangleColor[0] + 0.06f),
        1.0f,
    };
    const XMFLOAT4 leftColor = {
        Clamp01(state.triangleColor[2] + 0.04f),
        Clamp01(state.triangleColor[0] + 0.12f),
        Clamp01(state.triangleColor[1] + 0.20f * pulse),
        1.0f,
    };

    return {{
        {{top.x, top.y, 0.50f, 1.0f}, topColor},
        {{right.x, right.y, 0.50f, 1.0f}, rightColor},
        {{left.x, left.y, 0.50f, 1.0f}, leftColor},
    }};
}

void ApplyFallbackMenuSelection(OkaneFrameState& state, uint32_t selectedIndex)
{
    const uint32_t itemCount = std::min<uint32_t>(state.menuItemCount, OKANE_MAX_MENU_ITEMS);
    for (uint32_t index = 0; index < itemCount; ++index)
    {
        state.menuItems[index].highlighted = (index == selectedIndex) ? 1u : 0u;
    }
}

uint32_t GetFallbackMenuSelection(const OkaneFrameState& state)
{
    const uint32_t itemCount = std::min<uint32_t>(state.menuItemCount, OKANE_MAX_MENU_ITEMS);
    for (uint32_t index = 0; index < itemCount; ++index)
    {
        if (state.menuItems[index].highlighted != 0)
        {
            return index;
        }
    }

    return 0;
}

void ApplyFallbackMenuAction(OkaneFrameState& /*state*/, uint32_t /*selectedIndex*/)
{
    // No longer used — miner actions are handled directly in Game::OnKeyDown.
}

void ApplyConfigToFrameState(const OkaneConfig& cfg, OkaneFrameState& state)
{
    if (!cfg.poolUrl.empty())
        strcpy_s(state.minerStatus.poolUrl, cfg.poolUrl.c_str());
    if (!cfg.walletAddr.empty())
        strcpy_s(state.minerStatus.walletAddr, cfg.walletAddr.c_str());
    if (!cfg.poolUrl.empty() || !cfg.walletAddr.empty())
        strcpy_s(state.minerStatus.poolStatusText, "configured");
}

XMFLOAT4 GetPoolStatusColor(const OkaneFrameState& state)
{
    if (state.minerStatus.lastError[0] != '\0')
    {
        return {0.92f, 0.28f, 0.24f, 1.0f};
    }

    const std::string poolStatus = ToUpperAscii(ReadFixedString(state.minerStatus.poolStatusText, 64));
    if (poolStatus == "ACTIVE")
    {
        return {0.20f, 0.78f, 0.38f, 1.0f};
    }
    if (poolStatus == "CONNECTING")
    {
        return {0.95f, 0.76f, 0.20f, 1.0f};
    }
    if (poolStatus == "CONFIGURED")
    {
        return {0.55f, 0.76f, 0.98f, 1.0f};
    }

    return {0.70f, 0.76f, 0.88f, 1.0f};
}

void UpdateLocalFrameState(OkaneFrameState& state, double totalSeconds, double elapsedSeconds)
{
    const float total = static_cast<float>(totalSeconds);
    const float pulse = 0.5f + 0.5f * std::sin(total * 2.35f);

    state.clearColor[0] = Clamp01(0.07f + 0.04f * std::sin(total * 0.67f));
    state.clearColor[1] = Clamp01(0.09f + 0.03f * std::sin(total * 0.93f + 0.85f));
    state.clearColor[2] = Clamp01(0.13f + 0.05f * std::sin(total * 0.79f + 1.70f));
    state.clearColor[3] = 1.0f;

    state.triangleColor[0] = Clamp01(0.55f + 0.35f * std::sin(total * 1.15f + 0.35f));
    state.triangleColor[1] = Clamp01(0.45f + 0.30f * std::sin(total * 1.55f + 1.10f));
    state.triangleColor[2] = Clamp01(0.35f + 0.40f * std::sin(total * 1.35f + 2.20f));
    state.triangleColor[3] = 1.0f;

    state.triangleScale = 0.72f + 0.12f * std::sin(total * 1.40f + 0.40f);
    state.triangleOffsetX = 0.18f * std::sin(total * 0.85f);
    state.triangleOffsetY = 0.10f * std::cos(total * 1.20f);
    state.triangleRotationRadians += static_cast<float>(elapsedSeconds) * 0.90f;
    state.trianglePulse = pulse;
    ++state.frameIndex;
}
}  // namespace

Game::Game() noexcept(false) : m_frame(0), m_vertexBufferView{}, m_menuVertexBufferView{}, m_menuVertexCount(0)
{
    Tracef("[okane][Game] ctor begin this=%p\n", this);

    std::memset(&m_goFrameState, 0, sizeof(m_goFrameState));
    m_goFrameState.clearColor[0] = 0.08f;
    m_goFrameState.clearColor[1] = 0.09f;
    m_goFrameState.clearColor[2] = 0.12f;
    m_goFrameState.clearColor[3] = 1.0f;
    m_goFrameState.menuScreen = OKANE_MENU_MAIN;
    m_goFrameState.triangleColor[0] = 0.90f;
    m_goFrameState.triangleColor[1] = 0.55f;
    m_goFrameState.triangleColor[2] = 0.30f;
    m_goFrameState.triangleColor[3] = 1.0f;
    m_goFrameState.triangleScale = 0.80f;
    m_goFrameState.menuItemCount = 3;
    strcpy_s(m_goFrameState.menuTitle, "OKANEIZER");
    strcpy_s(m_goFrameState.menuItems[0].label, "Start Mining");
    strcpy_s(m_goFrameState.menuItems[1].label, "Stop Mining");
    strcpy_s(m_goFrameState.menuItems[2].label, "Exit");
    m_goFrameState.menuItems[0].highlighted = 1;
    strcpy_s(m_goFrameState.minerStatus.poolUrl, "not configured");
    strcpy_s(m_goFrameState.minerStatus.poolStatusText, "idle");
    strcpy_s(m_goFrameState.minerStatus.hashRateText, "idle");
    strcpy_s(m_goFrameState.minerStatus.walletAddr, "pending");

    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->SetClearColor(c_Background);
    m_deviceResources->RegisterDeviceNotify(this);

    Tracef("[okane][Game] ctor end deviceResources=%p\n", m_deviceResources.get());
}

Game::~Game()
{
    Tracef("[okane][Game] dtor begin this=%p\n", this);
    if (m_miner) m_miner->Stop();
    if (m_stratumClient) m_stratumClient->Disconnect();
    if (m_deviceResources)
    {
        m_deviceResources->WaitForGpu();
    }
    Tracef("[okane][Game] dtor end\n");
}

void Game::EnsureMiner()
{
    if (m_miner) return;

    std::string poolUrl = m_config.poolUrl;
    if (poolUrl.empty()) poolUrl = "stratum+tcp://sha256.poolbinance.com:443";

    std::string walletAddr = m_config.walletAddr;
    std::string rpcUser = m_config.username.empty() ? std::string("user") : m_config.username;
    std::string rpcPass = m_config.password.empty() ? std::string("pass") : m_config.password;

    // Update frame state with resolved values.
    strcpy_s(m_goFrameState.minerStatus.poolUrl, poolUrl.c_str());

    if (IsStratumUrl(poolUrl))
    {
        // Worker name for stratum: "wallet.worker" or just username.
        std::string workerName = rpcUser;
        if (!walletAddr.empty())
            workerName = walletAddr + "." + m_config.workerName;
        if (m_config.workerName.empty() && !walletAddr.empty())
            workerName = walletAddr;

        // Build list of pool URLs to try (primary + alternatives).
        std::vector<std::string> poolUrls;
        poolUrls.push_back(poolUrl);
        if (!m_config.poolUrlSsl.empty()) poolUrls.push_back(m_config.poolUrlSsl);
        if (!m_config.poolUrlAlt1.empty()) poolUrls.push_back(m_config.poolUrlAlt1);
        if (!m_config.poolUrlAlt2.empty()) poolUrls.push_back(m_config.poolUrlAlt2);
        if (!m_config.poolUrlAlt3.empty()) poolUrls.push_back(m_config.poolUrlAlt3);

        bool connected = false;
        std::string lastErr;
        for (const auto& url : poolUrls)
        {
            if (!IsStratumUrl(url)) continue;

            Tracef("[okane][Game] trying pool: %s\n", url.c_str());
            strcpy_s(m_goFrameState.minerStatus.poolStatusText, "CONNECTING...");
            strcpy_s(m_goFrameState.minerStatus.poolUrl, url.c_str());

            m_stratumClient = std::make_unique<OkaneStratumClient>(url, workerName, rpcPass);

            std::string err;
            if (m_stratumClient->Connect(err))
            {
                poolUrl = url;
                connected = true;
                Tracef("[okane][Game] connected to %s\n", url.c_str());
                break;
            }

            Tracef("[okane][Game] connect failed (%s): %s\n", url.c_str(), err.c_str());
            lastErr = err;
            m_stratumClient.reset();
        }

        if (!connected)
        {
            Tracef("[okane][Game] all pool URLs failed, last error: %s\n", lastErr.c_str());
            strcpy_s(m_goFrameState.minerStatus.poolStatusText, "ALL POOLS FAILED");
            return;
        }

        // Create GPU hasher sharing the existing D3D12 device (Xbox only allows one).
        if (!m_gpuHasher)
        {
            m_gpuHasher = std::make_unique<OkaneGpuHasher>(m_deviceResources->GetD3DDevice());
            if (m_gpuHasher->IsValid())
                Tracef("[okane][Game] GPU hasher created successfully\n");
            else
            {
                Tracef("[okane][Game] GPU hasher creation failed, falling back to CPU\n");
                m_gpuHasher.reset();
            }
        }

        strcpy_s(m_goFrameState.minerStatus.walletAddr, workerName.c_str());
        strcpy_s(m_goFrameState.minerStatus.poolStatusText, "CONNECTED");

        m_miner = std::make_unique<OkaneMiner>(m_stratumClient.get(), workerName, 0, m_gpuHasher.get());
        Tracef("[okane][Game] stratum miner created pool=%s worker=%s gpu=%s\n",
               poolUrl.c_str(), workerName.c_str(), m_gpuHasher ? "yes" : "no");
    }
    else
    {
        // GBT / Bitcoin Core RPC mode.
        m_rpcClient = std::make_unique<OkaneRpcClient>(poolUrl, rpcUser, rpcPass);

        if (walletAddr.empty())
        {
            std::string err;
            if (!m_rpcClient->GetNewAddress(walletAddr, err))
                walletAddr = "(could not fetch address)";
        }

        strcpy_s(m_goFrameState.minerStatus.walletAddr, walletAddr.c_str());
        m_miner = std::make_unique<OkaneMiner>(m_rpcClient.get(), walletAddr, 0, m_gpuHasher.get());
        Tracef("[okane][Game] rpc miner created pool=%s wallet=%s gpu=%s\n",
               poolUrl.c_str(), walletAddr.c_str(), m_gpuHasher ? "yes" : "no");
    }
}

void Game::Initialize(HWND window, int width, int height)
{
    Tracef("[okane][Game] Initialize begin hwnd=%p size=%dx%d\n", window, width, height);

    m_deviceResources->SetWindow(window, width, height);
    m_goFrameState.viewportWidth = static_cast<uint32_t>(width);
    m_goFrameState.viewportHeight = static_cast<uint32_t>(height);

    // Build config path now, but defer loading until after first present.
    m_pendingConfigPath[0] = '\0';
    {
        char modulePath[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH))
        {
            char drive[_MAX_DRIVE] = {};
            char dir[_MAX_DIR]     = {};
            _splitpath_s(modulePath, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
            _makepath_s(m_pendingConfigPath, MAX_PATH, drive, dir, "okane", ".csv");
        }
    }

    Tracef("[okane][Game] pending config path=%s\n",
           m_pendingConfigPath[0] != '\0' ? m_pendingConfigPath : "<empty>");
    m_config = LoadConfigCSV(m_pendingConfigPath);
    ApplyConfigToFrameState(m_config, m_goFrameState);

    Tracef("[okane][Game] CreateDeviceResources begin\n");
    m_deviceResources->CreateDeviceResources();
    Tracef("[okane][Game] CreateDeviceResources end\n");

    Tracef("[okane][Game] CreateDeviceDependentResources begin\n");
    CreateDeviceDependentResources();
    Tracef("[okane][Game] CreateDeviceDependentResources end\n");

    Tracef("[okane][Game] CreateWindowSizeDependentResources begin\n");
    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
    Tracef("[okane][Game] Initialize end\n");
}
#pragma region Frame Update

void Game::Tick()
{
    if (ShouldTraceFrame(m_frame) || !m_bootstrapPresented)
    {
        Tracef("[okane][Game] Tick begin frame=%llu bootstrapPresented=%d miner=%d\n",
               static_cast<unsigned long long>(m_frame), m_bootstrapPresented ? 1 : 0,
               m_miner ? 1 : 0);
    }

    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Frame %llu", m_frame);

#ifdef _GAMING_XBOX
    // We need a valid frame pipeline token before PresentX.
    m_deviceResources->WaitForOrigin();

    if (ShouldTraceFrame(m_frame) || !m_bootstrapPresented)
    {
        Tracef("[okane][Game] WaitForOrigin complete frame=%llu\n", static_cast<unsigned long long>(m_frame));
    }

    // Present a trivial first frame immediately so startup work does not leave
    // the shell splash screen visible.
    if (!m_bootstrapPresented)
    {
        Tracef("[okane][Game] bootstrap present frame=%llu\n", static_cast<unsigned long long>(m_frame));
        Render();
        m_bootstrapPresented = true;
        m_timer.ResetElapsedTime();

        PIXEndEvent();
        m_frame++;
        return;
    }
#endif

    m_timer.Tick([&]() { Update(m_timer); });

    Render();

    PIXEndEvent();

    if (ShouldTraceFrame(m_frame))
    {
        Tracef("[okane][Game] Tick end frame=%llu\n", static_cast<unsigned long long>(m_frame));
    }

    m_frame++;
}

void Game::Update(DX::StepTimer const& timer)
{
    if (ShouldTraceFrame(m_frame))
    {
        Tracef("[okane][Game] Update begin frame=%llu total=%.3f elapsed=%.6f\n",
               static_cast<unsigned long long>(m_frame), timer.GetTotalSeconds(), timer.GetElapsedSeconds());
    }

    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    UpdateLocalFrameState(m_goFrameState, timer.GetTotalSeconds(), timer.GetElapsedSeconds());

    // Sync miner telemetry into frame state.
    if (m_miner)
    {
        OkaneMinerSnapshot ms = m_miner->GetStatus();
        m_goFrameState.minerStatus.running = ms.running ? 1 : 0;
        m_goFrameState.minerStatus.hashRate = ms.hashRate;
        strcpy_s(m_goFrameState.minerStatus.hashRateText, ms.hashRateText.empty() ? "idle" : ms.hashRateText.c_str());
        m_goFrameState.minerStatus.blocksFound = ms.blocksFound;
        if (!ms.lastError.empty())
            strcpy_s(m_goFrameState.minerStatus.lastError, ms.lastError.c_str());
        else
            m_goFrameState.minerStatus.lastError[0] = '\0';

        // Update menu title to reflect miner state.
        if (ms.running)
        {
            strcpy_s(m_goFrameState.menuTitle, "MINING...");
            strcpy_s(m_goFrameState.minerStatus.poolStatusText,
                     ms.lastError.empty() ? (ms.hashRateText.empty() ? "CONNECTING" : "ACTIVE") : "ERROR");
            m_goFrameState.menuScreen = OKANE_MENU_MINING;
        }
        else if (m_goFrameState.menuScreen == OKANE_MENU_MINING)
        {
            strcpy_s(m_goFrameState.menuTitle, "STOPPED");
            strcpy_s(m_goFrameState.minerStatus.poolStatusText, "IDLE");
            m_goFrameState.menuScreen = OKANE_MENU_STOPPED;
        }
    }

    m_deviceResources->SetClearColor(m_goFrameState.clearColor);
    RefreshDynamicGeometry();

    PIXEndEvent();

    if (ShouldTraceFrame(m_frame))
    {
        Tracef("[okane][Game] Update end frame=%llu menuItems=%u running=%d clear=(%.3f, %.3f, %.3f, %.3f)\n",
               static_cast<unsigned long long>(m_frame), m_goFrameState.menuItemCount,
               m_goFrameState.minerStatus.running ? 1 : 0,
               m_goFrameState.clearColor[0], m_goFrameState.clearColor[1],
               m_goFrameState.clearColor[2], m_goFrameState.clearColor[3]);
    }
}
#pragma endregion

#pragma region Frame Render
void Game::Render()
{
    if (ShouldTraceFrame(m_frame) || !m_bootstrapPresented)
    {
        Tracef("[okane][Game] Render begin frame=%llu menuVerts=%u\n",
               static_cast<unsigned long long>(m_frame), m_menuVertexCount);
    }

    m_deviceResources->Prepare();
    Clear();

    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render");

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->DrawInstanced(3, 1, 0, 0);

    if (m_menuVertexCount > 0)
    {
        commandList->IASetVertexBuffers(0, 1, &m_menuVertexBufferView);
        commandList->DrawInstanced(m_menuVertexCount, 1, 0, 0);
    }

    PIXEndEvent(commandList);

    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Present");
    m_deviceResources->Present();
    PIXEndEvent();

    if (ShouldTraceFrame(m_frame) || !m_bootstrapPresented)
    {
        Tracef("[okane][Game] Render end frame=%llu present submitted\n",
               static_cast<unsigned long long>(m_frame));
    }
}

void Game::Clear()
{
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Clear");

    const auto rtvDescriptor = m_deviceResources->GetRenderTargetView();
    const auto dsvDescriptor = m_deviceResources->GetDepthStencilView();

    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
    commandList->ClearRenderTargetView(rtvDescriptor, m_goFrameState.clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const auto viewport    = m_deviceResources->GetScreenViewport();
    const auto scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    PIXEndEvent(commandList);
}
#pragma endregion

#pragma region Message Handlers
void Game::OnSuspending()
{
    Tracef("[okane][Game] OnSuspending\n");
    m_deviceResources->Suspend();
}

void Game::OnResuming()
{
    Tracef("[okane][Game] OnResuming\n");
    m_deviceResources->Resume();
    m_timer.ResetElapsedTime();
}

void Game::OnWindowMoved()
{
    const auto r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnWindowSizeChanged(int width, int height)
{
    Tracef("[okane][Game] OnWindowSizeChanged width=%d height=%d\n", width, height);

    if (!m_deviceResources->WindowSizeChanged(width, height))
    {
        Tracef("[okane][Game] OnWindowSizeChanged ignored (no change)\n");
        return;
    }

    m_goFrameState.viewportWidth = static_cast<uint32_t>(width);
    m_goFrameState.viewportHeight = static_cast<uint32_t>(height);
    CreateWindowSizeDependentResources();
}

void Game::OnKeyDown(UINT vk)
{
    uint32_t selected = GetFallbackMenuSelection(m_goFrameState);
    switch (vk)
    {
    case VK_UP:
    case 'W':
        if (selected > 0)
        {
            --selected;
        }
        ApplyFallbackMenuSelection(m_goFrameState, selected);
        RefreshDynamicGeometry();
        return;

    case VK_DOWN:
    case 'S':
        if (selected + 1 < std::min<uint32_t>(m_goFrameState.menuItemCount, OKANE_MAX_MENU_ITEMS))
        {
            ++selected;
        }
        ApplyFallbackMenuSelection(m_goFrameState, selected);
        RefreshDynamicGeometry();
        return;

    case VK_RETURN:
    case VK_SPACE:
        Tracef("[okane][Game] select index=%u label=%s\n",
               selected,
               m_goFrameState.menuItems[selected].label);
        if (selected == 0) // Start Mining
        {
            EnsureMiner();
            if (!m_miner) return;
            m_miner->Start();
            strcpy_s(m_goFrameState.menuTitle, "MINING...");
            m_goFrameState.menuScreen = OKANE_MENU_MINING;
            RefreshDynamicGeometry();
        }
        else if (selected == 1) // Stop Mining
        {
            if (m_miner)
                m_miner->Stop();
            strcpy_s(m_goFrameState.menuTitle, "STOPPED");
            strcpy_s(m_goFrameState.minerStatus.poolStatusText, "IDLE");
            strcpy_s(m_goFrameState.minerStatus.hashRateText, "idle");
            m_goFrameState.minerStatus.running = 0;
            m_goFrameState.minerStatus.lastError[0] = '\0';
            m_goFrameState.menuScreen = OKANE_MENU_STOPPED;
            RefreshDynamicGeometry();
        }
        else if (selected == 2) // Exit
        {
            if (m_miner)
                m_miner->Stop();
            ExitGame();
        }
        return;
    }
}

void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    width  = 1280;
    height = 720;
}
#pragma endregion

#pragma region Direct3D Resources
void Game::CreateDeviceDependentResources()
{
    Tracef("[okane][Game] CreateDeviceDependentResources start\n");

    auto device = m_deviceResources->GetD3DDevice();

#ifdef _GAMING_DESKTOP
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {D3D_SHADER_MODEL_6_0};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
        (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
    {
        throw std::runtime_error("Shader Model 6.0 is not supported!");
    }
#endif

    const auto vertexShaderBlob = ReadData(L"VertexShader.cso");
    Tracef("[okane][Game] VertexShader.cso bytes=%zu\n", vertexShaderBlob.size());
    DX::ThrowIfFailed(device->CreateRootSignature(0, vertexShaderBlob.data(), vertexShaderBlob.size(),
                                                  IID_GRAPHICS_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));
    Tracef("[okane][Game] Root signature created\n");

    const auto pixelShaderBlob = ReadData(L"PixelShader.cso");
    Tracef("[okane][Game] PixelShader.cso bytes=%zu\n", pixelShaderBlob.size());

    static const D3D12_INPUT_ELEMENT_DESC s_inputElementDesc[2] = {
        {"SV_Position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout                        = {s_inputElementDesc, static_cast<UINT>(std::size(s_inputElementDesc))};
    psoDesc.pRootSignature                     = m_rootSignature.Get();
    psoDesc.VS                                 = {vertexShaderBlob.data(), vertexShaderBlob.size()};
    psoDesc.PS                                 = {pixelShaderBlob.data(), pixelShaderBlob.size()};
    psoDesc.RasterizerState                    = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState                         = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable      = FALSE;
    psoDesc.DepthStencilState.StencilEnable    = FALSE;
    psoDesc.DSVFormat                          = m_deviceResources->GetDepthBufferFormat();
    psoDesc.SampleMask                         = UINT_MAX;
    psoDesc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets                   = 1;
    psoDesc.RTVFormats[0]                      = m_deviceResources->GetBackBufferFormat();
    psoDesc.SampleDesc.Count                   = 1;

    DX::ThrowIfFailed(
        device->CreateGraphicsPipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    Tracef("[okane][Game] Graphics pipeline state created\n");

    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    const auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Vertex) * 3);

    DX::ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                      IID_GRAPHICS_PPV_ARGS(m_vertexBuffer.ReleaseAndGetAddressOf())));
    Tracef("[okane][Game] Triangle vertex buffer created bytes=%zu\n", sizeof(Vertex) * size_t{3});

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes  = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes    = sizeof(Vertex) * 3;

    {
        const CD3DX12_HEAP_PROPERTIES menuHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const auto menuResDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Vertex) * kMaxMenuQuadVerts);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &menuHeapProps, D3D12_HEAP_FLAG_NONE, &menuResDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_GRAPHICS_PPV_ARGS(m_menuVertexBuffer.ReleaseAndGetAddressOf())));
         Tracef("[okane][Game] Menu vertex buffer created bytes=%zu\n",
             sizeof(Vertex) * size_t{kMaxMenuQuadVerts});

        m_menuVertexBufferView.BufferLocation = m_menuVertexBuffer->GetGPUVirtualAddress();
        m_menuVertexBufferView.StrideInBytes  = sizeof(Vertex);
        m_menuVertexBufferView.SizeInBytes    = sizeof(Vertex) * kMaxMenuQuadVerts;
    }

    RefreshDynamicGeometry();

    // Do not stall startup on Xbox here.
#ifndef _GAMING_XBOX
    m_deviceResources->WaitForGpu();
#endif

    Tracef("[okane][Game] CreateDeviceDependentResources end\n");
}

void Game::CreateWindowSizeDependentResources() {}

void Game::RefreshDynamicGeometry()
{
    if (!m_vertexBuffer)
    {
        return;
    }

    const auto vertices = BuildTriangleVertices(m_goFrameState);

    UINT8* vertexDataBegin = nullptr;
    const CD3DX12_RANGE readRange(0, 0);
    DX::ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)));
    std::memcpy(vertexDataBegin, vertices.data(), sizeof(Vertex) * vertices.size());
    m_vertexBuffer->Unmap(0, nullptr);

    if (!m_menuVertexBuffer)
    {
        m_menuVertexCount = 0;
        return;
    }

    std::vector<Vertex> menuVerts;
    menuVerts.reserve(kMaxMenuQuadVerts);

    const XMFLOAT4 panelColor = {0.08f, 0.10f, 0.14f, 1.0f};
    const XMFLOAT4 sectionColor = {0.14f, 0.16f, 0.22f, 1.0f};
    const XMFLOAT4 textColor = {0.92f, 0.93f, 0.89f, 1.0f};
    const XMFLOAT4 accentColor = {0.96f, 0.62f, 0.16f, 1.0f};
    const XMFLOAT4 okColor = {0.20f, 0.78f, 0.38f, 1.0f};
    const XMFLOAT4 warnColor = {0.95f, 0.76f, 0.20f, 1.0f};
    const XMFLOAT4 errorColor = {0.92f, 0.28f, 0.24f, 1.0f};

    AppendQuad(menuVerts, -0.96f, 0.94f, 0.96f, 0.28f, panelColor, 0.42f);
    AppendQuad(menuVerts, -0.96f, 0.22f, 0.96f, -0.94f, sectionColor, 0.42f);

    const std::string title = ShortenForUi(ToUpperAscii(ReadFixedString(m_goFrameState.menuTitle, OKANE_MAX_LABEL_LEN)), 18);
    const std::string poolUrl = ShortenForUi(
        ToUpperAscii(ReadFixedString(m_goFrameState.minerStatus.poolUrl, 128)), 28);
    const std::string wallet = ShortenForUi(
        ToUpperAscii(ReadFixedString(m_goFrameState.minerStatus.walletAddr, 128)), 24);
    const std::string hashRate = ShortenForUi(
        ToUpperAscii(ReadFixedString(m_goFrameState.minerStatus.hashRateText, 32)), 16);
    const std::string lastError = ShortenForUi(
        ToUpperAscii(ReadFixedString(m_goFrameState.minerStatus.lastError, 256)), 30);
    const std::string poolStatus = ShortenForUi(
        ToUpperAscii(ReadFixedString(m_goFrameState.minerStatus.poolStatusText, 64)), 12);

    const std::string stateLine = !lastError.empty() ? "STATE: ERROR" :
        (m_goFrameState.minerStatus.running ? "STATE: MINING" : "STATE: READY");
    const std::string poolStateLine = "POOL: " + (poolStatus.empty() ? std::string("IDLE") : poolStatus);
    const std::string hashLine = "HASH: " + (hashRate.empty() ? std::string("IDLE") : hashRate);
    const std::string blocksLine = "BLOCKS: " + std::to_string(m_goFrameState.minerStatus.blocksFound);
    const std::string targetLine = "TARGET: " + (poolUrl.empty() ? std::string("UNSET") : poolUrl);
    const std::string walletLine = "WALLET: " + (wallet.empty() ? std::string("UNSET") : wallet);
    const std::string errorLine = "ERROR: " + (lastError.empty() ? std::string("NONE") : lastError);
    const std::string hintLine = "W/S MOVE   ENTER SELECT";

    AppendText(menuVerts, title, -0.90f, 0.84f, 0.048f, 0.100f, 0.056f, accentColor);
    AppendText(menuVerts, stateLine, -0.90f, 0.66f, 0.024f, 0.050f, 0.032f,
               !lastError.empty() ? errorColor : (m_goFrameState.minerStatus.running ? okColor : warnColor));
    AppendText(menuVerts, poolStateLine, -0.90f, 0.56f, 0.024f, 0.050f, 0.032f,
               GetPoolStatusColor(m_goFrameState));
    AppendText(menuVerts, targetLine, -0.90f, 0.46f, 0.020f, 0.042f, 0.027f, textColor);
    AppendText(menuVerts, hashLine, -0.90f, 0.36f, 0.020f, 0.042f, 0.027f, textColor);
    AppendText(menuVerts, blocksLine, 0.12f, 0.66f, 0.024f, 0.050f, 0.032f, textColor);
    AppendText(menuVerts, walletLine, 0.12f, 0.56f, 0.020f, 0.042f, 0.027f, textColor);
    AppendText(menuVerts, errorLine, 0.12f, 0.46f, 0.020f, 0.042f, 0.027f,
               lastError.empty() ? textColor : errorColor);
    AppendText(menuVerts, hintLine, 0.12f, 0.36f, 0.018f, 0.038f, 0.024f, accentColor);

    const uint32_t itemCount = std::min<uint32_t>(m_goFrameState.menuItemCount, OKANE_MAX_MENU_ITEMS);
    const float itemLeft = -0.90f;
    const float itemRight = 0.90f;
    const float itemHeight = 0.13f;
    const float itemGap = 0.045f;
    const float firstItemTop = 0.12f;

    for (uint32_t index = 0; index < itemCount; ++index)
    {
        const bool selected = (m_goFrameState.menuItems[index].highlighted != 0);
        const float top = firstItemTop - static_cast<float>(index) * (itemHeight + itemGap);
        const float bottom = top - itemHeight;
        const XMFLOAT4 barColor = selected ? accentColor : XMFLOAT4{0.18f, 0.20f, 0.26f, 1.0f};
        const XMFLOAT4 labelColor = selected ? XMFLOAT4{0.10f, 0.08f, 0.06f, 1.0f} : textColor;
        const std::string itemLabel = ShortenForUi(
            ToUpperAscii(ReadFixedString(m_goFrameState.menuItems[index].label, OKANE_MAX_LABEL_LEN)), 28);

        AppendQuad(menuVerts, itemLeft, top, itemRight, bottom, barColor, 0.41f);
        AppendText(menuVerts, itemLabel, itemLeft + 0.06f, top - 0.03f, 0.022f, 0.046f, 0.029f, labelColor);
    }

    if (menuVerts.size() > kMaxMenuQuadVerts)
    {
        menuVerts.resize(kMaxMenuQuadVerts);
    }

    m_menuVertexCount = static_cast<uint32_t>(menuVerts.size());

    UINT8* menuDataBegin = nullptr;
    DX::ThrowIfFailed(m_menuVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&menuDataBegin)));
    std::memcpy(menuDataBegin, menuVerts.data(), sizeof(Vertex) * m_menuVertexCount);
    m_menuVertexBuffer->Unmap(0, nullptr);
}

void Game::OnDeviceLost()
{
    Tracef("[okane][Game] OnDeviceLost\n");
    m_rootSignature.Reset();
    m_pipelineState.Reset();
    m_vertexBuffer.Reset();
    m_menuVertexBuffer.Reset();
    m_menuVertexCount   = 0;
}

void Game::OnDeviceRestored()
{
    Tracef("[okane][Game] OnDeviceRestored\n");
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}
#pragma endregion
