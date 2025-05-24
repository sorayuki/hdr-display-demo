#include <array>
#include <immintrin.h>
#include <math.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

ComPtr<ID3D11Device> d3dDevice;
ComPtr<ID3D11DeviceContext> d3dContext;
ComPtr<IDXGIFactory2> dxgiFactory;
ComPtr<IDXGISwapChain1> swapChain;
ComPtr<ID3D11Texture2D> backBuffer;
ComPtr<ID3D11Texture2D> stagingTexture;

struct float16x4 {
    unsigned short value[4];
    
    operator std::array<float, 4>() const {
        __m128i v16 = _mm_loadu_si64((const void*)value);
        __m128 v32 = _mm_cvtph_ps(v16);
        std::array<float, 4> out;
        _mm_storeu_ps(out.data(), v32);
        return out;
    }

    float16x4(std::array<float, 4>&& in) {
        __m128 v32 = _mm_loadu_ps(in.data());
        __m128i v16 = _mm_cvtps_ph(v32, 0);
        _mm_storeu_si64((void*)value, v16);
    }
};

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
    case WM_DISPLAYCHANGE:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        D3D11_MAPPED_SUBRESOURCE mappedResource = {};
        D3D11_TEXTURE2D_DESC texdesc = {};
        stagingTexture->GetDesc(&texdesc);
        HRESULT hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_WRITE, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            for(int i = 0; i < texdesc.Height; ++i) {
                float16x4 *pix = (float16x4*)((char*)mappedResource.pData + i * mappedResource.RowPitch);
                for(int j = 0; j < texdesc.Width; ++j)
                    pix[j] = std::array<float, 4>{ 10000.0f / 80.0f, 10000.0f / 80.0f, 10000.0f / 80.0f, 1.0f };
            }
            d3dContext->Unmap(stagingTexture.Get(), 0);
            d3dContext->CopyResource(backBuffer.Get(), stagingTexture.Get());
        }
        swapChain->Present(1, 0);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // 注册窗口类
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HDRWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindow(
        wc.lpszClassName, L"D3D11 HDR Example",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;
    
    RECT rc;
    GetClientRect(hwnd, &rc);

    // 创建D3D11设备和上下文
    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 }; // 常用特性级别
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,   // 硬件驱动
        nullptr,                    // 无软件光栅化器
        0,                          // 标准标志（如需debug可用D3D11_CREATE_DEVICE_DEBUG）
        featureLevels,              // 支持的特性级别数组
        ARRAYSIZE(featureLevels),   // 数组大小
        D3D11_SDK_VERSION,          // SDK版本
        &d3dDevice,                 // 返回设备
        &featureLevel,              // 返回实际特性级别
        &d3dContext                 // 返回上下文
    );
    // 此处可加错误检查
    CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

    // 2. 创建swapchain并绑定hwnd
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = rc.right - rc.left;
    scDesc.Height = rc.bottom - rc.top;
    scDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR格式
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    dxgiFactory->CreateSwapChainForHwnd(
        d3dDevice.Get(),
        hwnd,                // 绑定的窗口句柄
        &scDesc,
        nullptr, nullptr,
        &swapChain);

    swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

    // 创建用于CPU访问的staging纹理
    D3D11_TEXTURE2D_DESC texdesc = {};
    backBuffer->GetDesc(&texdesc);
    texdesc.Usage = D3D11_USAGE_STAGING;
    texdesc.BindFlags = 0;
    texdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texdesc.MiscFlags = 0;
    d3dDevice->CreateTexture2D(&texdesc, nullptr, &stagingTexture);

    ShowWindow(hwnd, nCmdShow);
    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}