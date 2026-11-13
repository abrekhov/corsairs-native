#include <SDL.h>
#include <d3d9.h>
#include <cmath>
#include <bit>
#include <cstdio>
#include <stdexcept>

void check(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("D3D fog E2E call failed");
}

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return 1;
    auto *window = SDL_CreateWindow("CoAS Fog E2E", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    320, 240, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
    if (!window)
        return 2;
    auto *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9 *dev = nullptr;
    check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev));
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    check(dev->SetTransform(D3DTS_WORLD, &identity));
    check(dev->SetTransform(D3DTS_VIEW, &identity));
    check(dev->SetRenderState(D3DRS_LIGHTING, FALSE));
    check(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    check(dev->SetRenderState(D3DRS_ZENABLE, FALSE));
    check(dev->SetRenderState(D3DRS_FOGENABLE, TRUE));
    check(dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP));
    check(dev->SetRenderState(D3DRS_FOGCOLOR, 0));
    check(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    check(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    check(dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
    IDirect3DSurface9 *back = nullptr, *readback = nullptr;
    check(dev->GetRenderTarget(0, &back));
    check(dev->CreateOffscreenPlainSurface(320, 240, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr));
    struct Vertex { float x, y, z; DWORD color; };
    const Vertex plane[] = {{-3000,-3000,6000,0xffffffff}, {-3000,3000,6000,0xffffffff},
                            {3000,3000,6000,0xffffffff}, {3000,-3000,6000,0xffffffff}};
    struct ScreenVertex { float x, y, z, rhw; DWORD color; };
    const ScreenVertex screen[] = {{0,0,.6f,1,0xffffffff}, {0,240,.6f,1,0xffffffff},
                                   {320,240,.6f,1,0xffffffff}, {320,0,.6f,1,0xffffffff}};
    int failures = 0;
    for (int pass = 0; pass < 6; pass++)
    {
        const bool ortho = pass == 3;
        const bool transformed = pass == 4;
        const float nearPlane = pass == 1 ? 4000.f : pass == 2 ? 5000.f : .1f;
        const float farPlane = pass == 1 ? 8000.f : pass == 2 ? 9000.f : 10000.f;
        D3DMATRIX projection{};
        projection._11 = projection._22 = 1;
        projection._33 = farPlane / (farPlane - nearPlane);
        projection._43 = -projection._33 * nearPlane;
        projection._34 = 1;
        if (ortho)
        {
            projection = identity;
            projection._11 = projection._22 = 1.f / 3000.f;
            projection._33 = .0001f;
        }
        float density = ortho || transformed ? 1.f : .0001f;
        check(dev->SetRenderState(D3DRS_FOGDENSITY, std::bit_cast<DWORD>(density)));
        check(dev->SetTransform(D3DTS_PROJECTION, &projection));
        check(dev->SetFVF((transformed ? D3DFVF_XYZRHW : D3DFVF_XYZ) | D3DFVF_DIFFUSE));
        check(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0));
        check(dev->BeginScene());
        check(dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, transformed ? static_cast<const void *>(screen) : plane,
                                   transformed ? sizeof(ScreenVertex) : sizeof(Vertex)));
        check(dev->EndScene());
        check(dev->GetRenderTargetData(back, readback));
        D3DLOCKED_RECT locked{};
        check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
        DWORD pixel = reinterpret_cast<DWORD *>(static_cast<char *>(locked.pBits) + 120 * locked.Pitch)[160];
        const int red = (pixel >> 16) & 255;
        const int expected = transformed ? 255 : std::lround(255 * std::exp(-.6));
        const bool passResult = std::abs(red - expected) <= 2 && std::abs(int(pixel & 255) - expected) <= 2;
        printf("fog pass=%d %s near=%g far=%g center=%08x expected=%d %s\n", pass,
               ortho ? "orthographic" : transformed ? "XYZRHW" : "perspective", nearPlane, farPlane,
               pixel, expected, passResult ? "PASS" : "FAIL");
        fflush(stdout);
        failures += !passResult;
        check(readback->UnlockRect());
        check(dev->Present(nullptr, nullptr, nullptr, nullptr));
        SDL_PumpEvents();
    }
    readback->Release();
    back->Release();
    dev->Release();
    d3d->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
