#include <SDL.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

void check(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("D3D call failed: " + std::to_string(hr));
}

std::vector<uint32_t> load(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(path);
    const auto size = file.tellg();
    std::vector<uint32_t> data(static_cast<size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

int main(int argc, char **argv)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return 1;
    auto *window = SDL_CreateWindow("CoAS Sea E2E", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    640, 400, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
    if (!window)
        return 2;
    auto *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 400;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9 *dev = nullptr;
    check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev));
    D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    IDirect3DVertexDeclaration9 *decl = nullptr;
    check(dev->CreateVertexDeclaration(elements, &decl));
    check(dev->SetVertexDeclaration(decl));
    const float vertices[][8] = {
        {-1, 0, -1, 0, 1, 0, 0, 0}, {-1, 0, 1, 0, 1, 0, 0, 1},
        {1, 0, 1, 0, 1, 0, 1, 1}, {1, 0, -1, 0, 1, 0, 1, 0}};
    float constants[64][4]{};
    auto set = [&](int i, std::array<float, 4> v) { for (int j = 0; j < 4; j++) constants[i][j] = v[j]; };
    set(0, {0, 1, .5f, -.04f});
    set(1, {2, -1, .0003662165f, 0});
    set(2, {.2f, .2f, .2f, .2f});
    set(23, {0, 2, 0, 1});
    set(24, {1, 0, 0, 0});
    set(25, {0, 0, 1, 0});
    set(26, {0, 0, 0, .5f});
    set(27, {0, 0, 0, 1});
    set(28, {.9f, .8f, .7f, 0});
    set(29, {.04f, .22f, .39f, 1});
    set(30, {1, 1, 1, 1});
    set(33, {1, 0, 0, 1});
    set(34, {.75f, 1, .5f, 1});
    set(35, {1, 0, 0, 1});
    set(36, {1, 0, 0, 0});
    set(37, {0, 1, 0, 0});
    set(38, {0, 0, 1, 0});
    set(39, {0, 0, 0, 1});
    set(58, {0, 1, 0, 1});
    check(dev->SetVertexShaderConstantF(0, &constants[0][0], 64));
    IDirect3DTexture9 *bump = nullptr;
    IDirect3DVolumeTexture9 *volume = nullptr;
    check(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &bump, nullptr));
    D3DLOCKED_RECT rect{};
    check(bump->LockRect(0, &rect, nullptr, 0));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            reinterpret_cast<uint32_t *>(static_cast<char *>(rect.pBits) + y * rect.Pitch)[x] = 0xff80ff80;
    check(bump->UnlockRect(0));
    check(dev->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &volume, nullptr));
    D3DLOCKED_BOX box{};
    check(volume->LockBox(0, &box, nullptr, 0));
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                reinterpret_cast<uint32_t *>(static_cast<char *>(box.pBits) + z * box.SlicePitch + y * box.RowPitch)[x] = 0xff80ff80;
    check(volume->UnlockBox(0));
    IDirect3DCubeTexture9 *cube = nullptr;
    check(dev->CreateCubeTexture(4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, &cube, nullptr));
    IDirect3DSurface9 *back = nullptr;
    check(dev->GetRenderTarget(0, &back));
    for (int face = 0; face < 6; face++)
    {
        IDirect3DSurface9 *surface = nullptr;
        check(cube->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, &surface));
        check(dev->SetRenderTarget(0, surface));
        check(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff3090e0, 1, 0));
        surface->Release();
    }
    check(dev->SetRenderTarget(0, back));
    check(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    check(dev->SetRenderState(D3DRS_LIGHTING, FALSE));
    check(dev->SetRenderState(D3DRS_FOGENABLE, FALSE));
    check(dev->SetRenderState(D3DRS_ZENABLE, FALSE));
    check(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
    check(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
    check(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA));
    int failures = 0;
    for (const auto &name : {"sea2", "sea2sunroad"})
    {
        const auto vsCode = load(std::string("resource/techniques/weather/sea_") + name + "_vertex_shader.vso");
        const auto psCode = load(std::string("resource/techniques/weather/sea_") + name + "_pixel_shader.pso");
        IDirect3DVertexShader9 *vs = nullptr;
        IDirect3DPixelShader9 *ps = nullptr;
        check(dev->CreateVertexShader(vsCode.data(), &vs));
        check(dev->CreatePixelShader(psCode.data(), &ps));
        check(dev->SetVertexShader(vs));
        check(dev->SetPixelShader(ps));
        check(dev->SetTexture(3, cube));
        for (bool useVolume : {false, true})
        {
            check(dev->SetTexture(0, useVolume ? static_cast<IDirect3DBaseTexture9 *>(volume) : bump));
            check(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0));
            check(dev->BeginScene());
            check(dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(vertices[0])));
            check(dev->EndScene());
            IDirect3DSurface9 *readback = nullptr;
            check(dev->CreateOffscreenPlainSurface(640, 400, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr));
            check(dev->GetRenderTargetData(back, readback));
            check(readback->LockRect(&rect, nullptr, D3DLOCK_READONLY));
            auto pixel = reinterpret_cast<uint32_t *>(static_cast<char *>(rect.pBits) + 200 * rect.Pitch)[320];
            const bool pass = (pixel & 0xffffff) != 0;
            printf("%s %s center=%08x %s\n", name, useVolume ? "volume" : "2D", pixel, pass ? "PASS" : "FAIL");
            fflush(stdout);
            failures += !pass;
            check(readback->UnlockRect());
            readback->Release();
            check(dev->Present(nullptr, nullptr, nullptr, nullptr));
            SDL_PumpEvents();
            if (const auto path = std::getenv("STORM_HEARTBEAT"))
                std::ofstream(path).put('\n');
        }
        vs->Release();
        ps->Release();
    }
    back->Release();
    cube->Release();
    volume->Release();
    bump->Release();
    decl->Release();
    dev->Release();
    d3d->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
