#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "windurango/kernelx.h"

namespace
{
struct Vertex
{
    float position[4];
    float color[4];
};

struct RenderState
{
    float clearColor[4];
    float triangleColor[4];
    float triangleScale;
    float triangleOffsetX;
    float triangleOffsetY;
    float triangleRotationRadians;
    float trianglePulse;
};

float clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

std::array<float, 2> rotate_and_offset(float x, float y, float scale, float radians, float offsetX, float offsetY)
{
    const float scaledX = x * scale;
    const float scaledY = y * scale;
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    return {
        (scaledX * cosine) - (scaledY * sine) + offsetX,
        (scaledX * sine) + (scaledY * cosine) + offsetY,
    };
}

void update_local_frame_state(RenderState &state, double totalSeconds, double elapsedSeconds)
{
    const float total = static_cast<float>(totalSeconds);
    const float pulse = 0.5f + 0.5f * std::sin(total * 2.35f);

    state.clearColor[0] = clamp01(0.07f + 0.04f * std::sin(total * 0.67f));
    state.clearColor[1] = clamp01(0.09f + 0.03f * std::sin(total * 0.93f + 0.85f));
    state.clearColor[2] = clamp01(0.13f + 0.05f * std::sin(total * 0.79f + 1.70f));
    state.clearColor[3] = 1.0f;

    state.triangleColor[0] = clamp01(0.55f + 0.35f * std::sin(total * 1.15f + 0.35f));
    state.triangleColor[1] = clamp01(0.45f + 0.30f * std::sin(total * 1.55f + 1.10f));
    state.triangleColor[2] = clamp01(0.35f + 0.40f * std::sin(total * 1.35f + 2.20f));
    state.triangleColor[3] = 1.0f;

    state.triangleScale = 0.72f + 0.12f * std::sin(total * 1.40f + 0.40f);
    state.triangleOffsetX = 0.18f * std::sin(total * 0.85f);
    state.triangleOffsetY = 0.10f * std::cos(total * 1.20f);
    state.triangleRotationRadians += static_cast<float>(elapsedSeconds) * 0.90f;
    state.trianglePulse = pulse;
}

std::array<Vertex, 3> build_triangle_vertices(const RenderState &state)
{
    constexpr std::array<std::array<float, 2>, 3> baseTriangle = {{
        {{0.0f, 0.70f}},
        {{0.62f, -0.45f}},
        {{-0.62f, -0.45f}},
    }};

    const float pulse = clamp01(state.trianglePulse);
    const float scale = state.triangleScale > 0.15f ? state.triangleScale : 0.15f;

    const auto top = rotate_and_offset(baseTriangle[0][0], baseTriangle[0][1], scale, state.triangleRotationRadians,
                                       state.triangleOffsetX, state.triangleOffsetY);
    const auto right = rotate_and_offset(baseTriangle[1][0], baseTriangle[1][1], scale,
                                         state.triangleRotationRadians, state.triangleOffsetX, state.triangleOffsetY);
    const auto left = rotate_and_offset(baseTriangle[2][0], baseTriangle[2][1], scale,
                                        state.triangleRotationRadians, state.triangleOffsetX, state.triangleOffsetY);

    const std::array<float, 4> topColor = {
        clamp01(state.triangleColor[0] + 0.20f * pulse),
        clamp01(state.triangleColor[1] + 0.08f),
        clamp01(state.triangleColor[2] + 0.04f),
        1.0f,
    };
    const std::array<float, 4> rightColor = {
        clamp01(state.triangleColor[1] + 0.10f * pulse),
        clamp01(state.triangleColor[2] + 0.18f),
        clamp01(state.triangleColor[0] + 0.06f),
        1.0f,
    };
    const std::array<float, 4> leftColor = {
        clamp01(state.triangleColor[2] + 0.04f),
        clamp01(state.triangleColor[0] + 0.12f),
        clamp01(state.triangleColor[1] + 0.20f * pulse),
        1.0f,
    };

    return {{
        {{top[0], top[1], 0.50f, 1.0f}, {topColor[0], topColor[1], topColor[2], topColor[3]}},
        {{right[0], right[1], 0.50f, 1.0f}, {rightColor[0], rightColor[1], rightColor[2], rightColor[3]}},
        {{left[0], left[1], 0.50f, 1.0f}, {leftColor[0], leftColor[1], leftColor[2], leftColor[3]}},
    }};
}

template <typename InterfaceType>
void safe_release(InterfaceType *&value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}

using D3DCompileProc = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR,
                                         LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

ID3DBlob *compile_shader(const char *source, const char *entryPoint, const char *target)
{
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler)
    {
        compiler = LoadLibraryW(L"d3dcompiler_43.dll");
    }
    if (!compiler)
    {
        throw std::runtime_error("LoadLibraryW(d3dcompiler_47.dll) failed");
    }

    auto *compile = reinterpret_cast<D3DCompileProc>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile)
    {
        throw std::runtime_error("GetProcAddress(D3DCompile) failed");
    }

    ID3DBlob *shader = nullptr;
    ID3DBlob *errors = nullptr;
    const HRESULT hr = compile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target, 0, 0,
                               &shader, &errors);
    if (FAILED(hr))
    {
        std::string message = "D3DCompile failed";
        if (errors && errors->GetBufferPointer())
        {
            message = static_cast<const char *>(errors->GetBufferPointer());
        }
        safe_release(errors);
        safe_release(shader);
        throw std::runtime_error(message);
    }

    safe_release(errors);
    return shader;
}

const char *vertex_shader_source()
{
    return R"(
struct VSInput
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = input.position;
    output.color = input.color;
    return output;
}
)";
}

const char *pixel_shader_source()
{
    return R"(
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.color;
}
)";
}

class TriangleApplication
{
public:
    TriangleApplication() = default;
    ~TriangleApplication()
    {
        cleanup();
    }

    void initialize(HWND windowHandle)
    {
        hwnd = windowHandle;
        create_device();
        create_swap_chain_resources();
        create_pipeline();
        startTime = std::chrono::steady_clock::now();
        lastFrameTime = startTime;
    }

    void resize(UINT width, UINT height)
    {
        if (!device || width == 0 || height == 0)
        {
            return;
        }

        safe_release(renderTargetView);
        const HRESULT hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            throw std::runtime_error("ResizeBuffers failed");
        }
        create_render_target_view();
    }

    void render()
    {
        const auto now = std::chrono::steady_clock::now();
        const double totalSeconds = std::chrono::duration<double>(now - startTime).count();
        const double elapsedSeconds = std::chrono::duration<double>(now - lastFrameTime).count();
        lastFrameTime = now;

        update_local_frame_state(renderState, totalSeconds, elapsedSeconds);
        const auto vertices = build_triangle_vertices(renderState);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT mapResult = context->Map(vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(mapResult))
        {
            throw std::runtime_error("Map(vertexBuffer) failed");
        }
        std::memcpy(mapped.pData, vertices.data(), sizeof(vertices));
        context->Unmap(vertexBuffer, 0);

        const float clearColor[4] = {
            renderState.clearColor[0],
            renderState.clearColor[1],
            renderState.clearColor[2],
            renderState.clearColor[3],
        };
        context->ClearRenderTargetView(renderTargetView, clearColor);

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        context->OMSetRenderTargets(1, &renderTargetView, nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(inputLayout);
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->RSSetViewports(1, &viewport);
        context->Draw(3, 0);

        swapChain->Present(1, 0);
    }

private:
    void cleanup()
    {
        safe_release(vertexBuffer);
        safe_release(inputLayout);
        safe_release(vertexShader);
        safe_release(pixelShader);
        safe_release(renderTargetView);
        safe_release(swapChain);
        safe_release(context);
        safe_release(device);
    }

    void create_device()
    {
        RECT rect = {};
        GetClientRect(hwnd, &rect);

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferDesc.Width = rect.right - rect.left;
        swapChainDesc.BufferDesc.Height = rect.bottom - rect.top;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.OutputWindow = hwnd;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL requestedLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
        D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requestedLevels,
            static_cast<UINT>(std::size(requestedLevels)), D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device,
            &createdLevel, &context);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11CreateDeviceAndSwapChain failed");
        }
        (void)createdLevel;
    }

    void create_swap_chain_resources()
    {
        create_render_target_view();

        RECT rect = {};
        GetClientRect(hwnd, &rect);
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(rect.right - rect.left);
        viewport.Height = static_cast<float>(rect.bottom - rect.top);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
    }

    void create_render_target_view()
    {
        ID3D11Texture2D *backBuffer = nullptr;
        const HRESULT getBufferResult = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                             reinterpret_cast<void **>(&backBuffer));
        if (FAILED(getBufferResult))
        {
            throw std::runtime_error("IDXGISwapChain::GetBuffer failed");
        }

        const HRESULT createViewResult = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        safe_release(backBuffer);
        if (FAILED(createViewResult))
        {
            throw std::runtime_error("CreateRenderTargetView failed");
        }
    }

    void create_pipeline()
    {
        ID3DBlob *vertexShaderBlob = compile_shader(vertex_shader_source(), "main", "vs_4_0");
        ID3DBlob *pixelShaderBlob = compile_shader(pixel_shader_source(), "main", "ps_4_0");

        const HRESULT vertexShaderResult = device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(),
                                                                      vertexShaderBlob->GetBufferSize(), nullptr,
                                                                      &vertexShader);
        if (FAILED(vertexShaderResult))
        {
            safe_release(vertexShaderBlob);
            safe_release(pixelShaderBlob);
            throw std::runtime_error("CreateVertexShader failed");
        }

        const HRESULT pixelShaderResult = device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
                                                                    pixelShaderBlob->GetBufferSize(), nullptr,
                                                                    &pixelShader);
        if (FAILED(pixelShaderResult))
        {
            safe_release(vertexShaderBlob);
            safe_release(pixelShaderBlob);
            throw std::runtime_error("CreatePixelShader failed");
        }

        D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        const HRESULT inputLayoutResult = device->CreateInputLayout(
            inputLayoutDesc, static_cast<UINT>(std::size(inputLayoutDesc)), vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(), &inputLayout);
        safe_release(vertexShaderBlob);
        safe_release(pixelShaderBlob);
        if (FAILED(inputLayoutResult))
        {
            throw std::runtime_error("CreateInputLayout failed");
        }

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.ByteWidth = sizeof(Vertex) * 3;
        vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        const HRESULT vertexBufferResult = device->CreateBuffer(&vertexBufferDesc, nullptr, &vertexBuffer);
        if (FAILED(vertexBufferResult))
        {
            throw std::runtime_error("CreateBuffer(vertexBuffer) failed");
        }
    }

    HWND hwnd = nullptr;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain *swapChain = nullptr;
    ID3D11RenderTargetView *renderTargetView = nullptr;
    ID3D11VertexShader *vertexShader = nullptr;
    ID3D11PixelShader *pixelShader = nullptr;
    ID3D11InputLayout *inputLayout = nullptr;
    ID3D11Buffer *vertexBuffer = nullptr;
    D3D11_VIEWPORT viewport = {};
    RenderState renderState = {};
    std::chrono::steady_clock::time_point startTime = {};
    std::chrono::steady_clock::time_point lastFrameTime = {};
};

TriangleApplication *application_from_window(HWND hwnd)
{
    return reinterpret_cast<TriangleApplication *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        auto *createStruct = reinterpret_cast<CREATESTRUCTW *>(lParam);
        auto *application = reinterpret_cast<TriangleApplication *>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        return 0;
    }
    case WM_SIZE:
    {
        TriangleApplication *application = application_from_window(hwnd);
        if (application)
        {
            application->resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

std::wstring make_window_title()
{
    SYSTEMOSVERSIONINFO version = {};
    GetSystemOSVersion(&version);
    const CONSOLE_TYPE console = GetConsoleType();

    wchar_t buffer[256] = {};
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Triangle WinDurango POC  Console=%d  OS=%u.%u.%u", static_cast<int>(console),
             static_cast<unsigned>(version.MajorVersion), static_cast<unsigned>(version.MinorVersion),
             static_cast<unsigned>(version.BuildNumber));
    return std::wstring(buffer);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    try
    {
        const std::wstring title = make_window_title();
        const wchar_t *className = L"TriangleWinDurangoWindow";

        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = window_proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass))
        {
            return 1;
        }

        TriangleApplication application;
        HWND window = CreateWindowExW(0, className, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                      1280, 720, nullptr, nullptr, instance, &application);
        if (!window)
        {
            return 1;
        }

        ShowWindow(window, showCommand);
        UpdateWindow(window);
        application.initialize(window);

        MSG message = {};
        while (message.message != WM_QUIT)
        {
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                continue;
            }
            application.render();
        }

        return static_cast<int>(message.wParam);
    }
    catch (const std::exception &error)
    {
        MessageBoxA(nullptr, error.what(), "Triangle WinDurango POC", MB_ICONERROR | MB_OK);
        return 1;
    }
}
