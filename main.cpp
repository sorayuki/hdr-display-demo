#include <array>
#include <memory>
#include <vector>
#include <math.h>

#include <immintrin.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <regex>

#include "color_conv.h"

using Microsoft::WRL::ComPtr;

struct float16x4 {
    unsigned short value[4];
    
    operator std::array<float, 4>() const {
        __m128i v16 = _mm_loadu_si64((const void*)value);
        __m128 v32 = _mm_cvtph_ps(v16);
        std::array<float, 4> out;
        _mm_storeu_ps(out.data(), v32);
        return out;
    }

    float16x4(const std::array<float, 4>& in) {
        __m128 v32 = _mm_loadu_ps(in.data());
        __m128i v16 = _mm_cvtps_ph(v32, 0);
        _mm_storeu_si64((void*)value, v16);
    }
};


class DxContext {
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIFactory2> dxgiFactory;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11Texture2D> stagingTexture;
    
    int width = 0;
    int height = 0;
    std::vector<unsigned short> buffer;

public:
    bool ShouldReset(int width, int height) {
        if (dxgiFactory && dxgiFactory->IsCurrent()) {
            if (width == this->width && height == this->height)
                return false;
        }

        return true;
    }

    void Init(int width, int height, HWND hwnd) {
        this->width = width;
        this->height = height;

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

        // 创建swapchain并绑定hwnd
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Width = width;
        scDesc.Height = height;
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
    }

    void ReloadTexture(int yuvmat, int yuvrange, int primary) {
        D3D11_MAPPED_SUBRESOURCE mappedResource = {};
        D3D11_TEXTURE2D_DESC texdesc = {};
        stagingTexture->GetDesc(&texdesc);
        HRESULT hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_WRITE, 0, &mappedResource);

        const unsigned short* py = buffer.data();
        const unsigned short* puv = buffer.data() + texdesc.Width * texdesc.Height;

        if (SUCCEEDED(hr)) {
            // 转换参数
            auto yuv2rgb = getYuv2RgbMat(yuvmat, yuvrange);
            // i：行，j：列
            for(int i = 0; i < texdesc.Height; ++i) {
                float16x4 *pix = (float16x4*)((char*)mappedResource.pData + i * mappedResource.RowPitch);
                for(int j = 0; j < texdesc.Width; ++j) {
                    std::array<float, 4> d = {
                        (py[i * texdesc.Width + j] >> 6) / 1023.0f,
                        (puv[i / 2 * texdesc.Width / 2 * 2 + j / 2 * 2] >> 6) / 1023.0f,
                        (puv[i / 2 * texdesc.Width / 2 * 2 + j / 2 * 2 + 1] >> 6) / 1023.0f,
                        1.0f
                    };

                    // YUV转非线性RGB
                    MulMatrix(d.data(), yuv2rgb);

                    // 非线性RGB转线性RGB
                    from_hlg(d.data());

                    // 线性RGB转换到BT.709色基，暂时先不管601和709的细微差别，只处理2020
                    if (primary == 1) {
                        rgb_2020_to_709(d.data());
                    }

                    pix[j] = d;
                }
            }
            d3dContext->Unmap(stagingTexture.Get(), 0);
        }
    }

    void LoadP010(const unsigned short* data, int yuvmat, int yuvrange, int primary) {
        buffer = std::vector<unsigned short>(data, data + width * height * 3 / 2);
        ReloadTexture(yuvmat, yuvrange, primary);
    }

    void Present() {
        d3dContext->CopyResource(backBuffer.Get(), stagingTexture.Get());
        swapChain->Present(1, 0);
    }
};

std::unique_ptr<DxContext> dxctx;

int yuv2rgb_index = 0;
int yuv2rgb_max = 3;
const wchar_t* yuv2rgb_name[] = {
    L"BT.601",
    L"BT.709",
    L"BT.2020"
};

int transfer_index = 0;
int transfer_max = 1;
const wchar_t* transfer_name[] = {
    L"HLG"
};

int yuvrange_index = 0;
int yuvrange_max = 2;
const wchar_t* yuvrange_name[] = {
    L"Full",
    L"Limited"
};

int primary_index = 0;
int primary_max = 2;
const wchar_t* primary_name[] = {
    L"sRGB",
    L"BT.2020"
};

void UpdateTitle(HWND hwnd) {
    std::wstring title = L"Matrix: " + std::wstring(yuv2rgb_name[yuv2rgb_index]) +
                            L", Range: " + std::wstring(yuvrange_name[yuvrange_index]) +
                            L", Transfer: " + std::wstring(transfer_name[transfer_index]) +
                            L", Primary: " + std::wstring(primary_name[primary_index]);
    SetWindowTextW(hwnd, title.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
    case WM_DISPLAYCHANGE:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (dxctx)
            dxctx->Present();
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_CHAR:
    {
        switch (wParam) {
        case '1':
            yuv2rgb_index = (yuv2rgb_index + 1) % yuv2rgb_max;
            break;
        case '2':
            yuvrange_index = (yuvrange_index + 1) % yuvrange_max;
            break;
        case '3':
            transfer_index = (transfer_index + 1) % transfer_max;
            break;
        case '4':
            primary_index = (primary_index + 1) % primary_max;
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        if (dxctx) {
            dxctx->ReloadTexture(yuv2rgb_index, yuvrange_index, primary_index);
            InvalidateRect(hwnd, nullptr, TRUE); // 重绘窗口
        }
        UpdateTitle(hwnd);

        break;
    }
    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;

        // 获取拖放的文件名
        wchar_t filePath[MAX_PATH];
        DragQueryFile(hDrop, 0, filePath, MAX_PATH);
        DragFinish(hDrop);

        std::wstring wcsFilePath(filePath);

        // 从文件名获取分辨率
        std::wregex re(L"_(\\d+)x(\\d+)");
        std::wsmatch match;
        int width = 0, height = 0;
        if (std::regex_search(wcsFilePath, match, re)) {
            width = std::stoi(match[1].str());
            height = std::stoi(match[2].str());

            // 打开文件
            FILE* fp = _wfopen(wcsFilePath.c_str(), L"rb");
            if (fp) {
                std::vector<unsigned short> data(width * height * 3 / 2); // P010格式
                size_t readSize = fread(data.data(), sizeof(unsigned short), data.size(), fp);
                if (readSize == data.size()) {
                    dxctx = std::make_unique<DxContext>();
                    dxctx->Init(width, height, hwnd);
                    dxctx->LoadP010(data.data(), yuv2rgb_index, yuvrange_index, primary_index);
                    // resize窗口但保持位置不变
                    SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
                    InvalidateRect(hwnd, nullptr, TRUE); // 重绘窗口
                    UpdateTitle(hwnd);
                } else {
                    MessageBox(hwnd, L"文件读取不完整", L"错误", MB_OK | MB_ICONERROR);
                }
                fclose(fp);
            } else {
                MessageBox(hwnd, L"无法打开文件", L"错误", MB_OK | MB_ICONERROR);
            }
        } else {
            MessageBox(hwnd, L"无法从文件名中解析分辨率", L"错误", MB_OK | MB_ICONERROR);
        }
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
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = L"HDRWindow";
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName, L"P010 HDR Viewer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;
    
    DragAcceptFiles(hwnd, TRUE);
    ShowWindow(hwnd, nCmdShow);
    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}