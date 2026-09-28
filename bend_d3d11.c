// Frames of the Bend effect into Direct3D 11 textures shared with CUDA (Windows): three textures
// and two fences made here and shared as NT handles; Bend copies each frame into a texture on the
// GPU and signals one fence, and each frame here Direct3D signals the other and waits for Bend's.
// No texture is mapped, so taking a frame costs next to nothing and holds up none of Bend's calls
// into the driver (see bend/bridge.c).
#include "bend_d3d11.h"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include "bend/bendviz.h"

struct bend_share {
    int w, h;
    ID3D11Texture2D *tex[3];
    SDL_Texture *sdl[3];
    HANDLE handle[3];
    ID3D11Fence *fence_cuda, *fence_d3d;  // CUDA signals the first, Direct3D the second
    HANDLE fence_cuda_handle, fence_d3d_handle;
    ID3D11DeviceContext4 *ctx;
    UINT64 d3d_value;
    int shown;
};

bend_share *bend_share_new(SDL_Renderer *ren, int w, int h) {
    ID3D11Device *dev = SDL_GetPointerProperty(SDL_GetRendererProperties(ren), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
    ID3D11Device5 *dev5 = NULL;
    ID3D11DeviceContext *ctx = NULL;
    bend_share *s = SDL_calloc(1, sizeof *s);
    if (!dev || !s || FAILED(ID3D11Device_QueryInterface(dev, &IID_ID3D11Device5, (void **)&dev5))) goto fail;
    ID3D11Device_GetImmediateContext(dev, &ctx);
    if (FAILED(ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11DeviceContext4, (void **)&s->ctx))) goto fail;
    s->w = w, s->h = h, s->shown = -1;
    D3D11_TEXTURE2D_DESC desc = { w, h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                  D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE };
    for (int i = 0; i < 3; i++) {
        IDXGIResource1 *r = NULL;
        bool ok = SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &desc, NULL, &s->tex[i]))
            && SUCCEEDED(ID3D11Texture2D_QueryInterface(s->tex[i], &IID_IDXGIResource1, (void **)&r))
            && SUCCEEDED(IDXGIResource1_CreateSharedHandle(r, NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &s->handle[i]));
        if (r) IDXGIResource1_Release(r);
        if (!ok) goto fail;
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER, s->tex[i]);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ARGB8888);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        s->sdl[i] = SDL_CreateTextureWithProperties(ren, props);
        SDL_DestroyProperties(props);
        if (!s->sdl[i]) goto fail;
        SDL_SetTextureBlendMode(s->sdl[i], SDL_BLENDMODE_NONE);
    }
    if (FAILED(ID3D11Device5_CreateFence(dev5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence, (void **)&s->fence_cuda))
        || FAILED(ID3D11Device5_CreateFence(dev5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence, (void **)&s->fence_d3d))
        || FAILED(ID3D11Fence_CreateSharedHandle(s->fence_cuda, NULL, GENERIC_ALL, NULL, &s->fence_cuda_handle))
        || FAILED(ID3D11Fence_CreateSharedHandle(s->fence_d3d, NULL, GENERIC_ALL, NULL, &s->fence_d3d_handle)))
        goto fail;
    ID3D11Device5_Release(dev5);
    ID3D11DeviceContext_Release(ctx);
    bendviz_d3d11_share((void *const[3]){ s->handle[0], s->handle[1], s->handle[2] }, s->fence_cuda_handle, s->fence_d3d_handle, w, h);
    return s;
fail:
    if (dev5) ID3D11Device5_Release(dev5);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    bend_share_free(s);
    return NULL;
}

void bend_share_free(bend_share *s) {
    if (!s) return;
    bendviz_d3d11_unshare();  // Bend lets go first
    for (int i = 0; i < 3; i++) {
        if (s->sdl[i]) SDL_DestroyTexture(s->sdl[i]);
        if (s->handle[i]) CloseHandle(s->handle[i]);
        if (s->tex[i]) ID3D11Texture2D_Release(s->tex[i]);
    }
    if (s->fence_cuda_handle) CloseHandle(s->fence_cuda_handle);
    if (s->fence_d3d_handle) CloseHandle(s->fence_d3d_handle);
    if (s->fence_cuda) ID3D11Fence_Release(s->fence_cuda);
    if (s->fence_d3d) ID3D11Fence_Release(s->fence_d3d);
    if (s->ctx) ID3D11DeviceContext4_Release(s->ctx);
    SDL_free(s);
}

bool bend_share_failed(void) { return bendviz_d3d11_failed(); }

void bend_share_size(const bend_share *s, int *w, int *h) { *w = s->w, *h = s->h; }

SDL_Texture *bend_share_frame(bend_share *s, bool *fresh, bool *gpu, double *ms) {
    // Direct3D will reach this value once done with everything drawn so far (the renderer is flushed
    // first, so that includes the texture on screen); Bend waits for it before writing that texture.
    SDL_FlushRenderer(SDL_GetRendererFromTexture(s->sdl[0]));
    ID3D11DeviceContext4_Signal(s->ctx, s->fence_d3d, ++s->d3d_value);
    unsigned long long wait;
    int w, h;
    int slot = bendviz_d3d11_take(s->d3d_value, &wait, &w, &h, gpu, ms);
    *fresh = slot >= 0;
    if (slot >= 0) {
        ID3D11DeviceContext4_Wait(s->ctx, s->fence_cuda, wait);  // Bend's copy into it is done
        s->shown = slot;
    }
    return s->shown >= 0 ? s->sdl[s->shown] : NULL;
}

bend_heap *bend_heap_for(SDL_Renderer *ren, unsigned long long bytes) {
    return bend_heap_new(SDL_GetPointerProperty(SDL_GetRendererProperties(ren), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL), bytes);
}

bool bend_heap_render(bend_heap *hp, SDL_Renderer *ren, SDL_FRect a, bool *fresh, int *w, int *h, bool *gpu, double *ms) {
    // The renderer's drawing so far goes first, and it sets all its state again after this.
    SDL_FlushRenderer(ren);
    SDL_PropertiesID props = SDL_GetRendererProperties(ren);
    ID3D11Device *dev = SDL_GetPointerProperty(props, SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
    SDL_Texture *target = SDL_GetRenderTarget(ren);
    ID3D11Resource *res = NULL;
    if (target) {
        res = SDL_GetPointerProperty(SDL_GetTextureProperties(target), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL);
        if (res) ID3D11Resource_AddRef(res);
    } else {
        // The swap chain's current back buffer (buffer 0), for this frame only: a reference kept
        // would stop the renderer resizing it.
        IDXGISwapChain *chain = SDL_GetPointerProperty(props, SDL_PROP_RENDERER_D3D11_SWAPCHAIN_POINTER, NULL);
        if (chain) IDXGISwapChain_GetBuffer(chain, 0, &IID_ID3D11Texture2D, (void **)&res);
    }
    ID3D11RenderTargetView *rtv = NULL;
    if (dev && res) ID3D11Device_CreateRenderTargetView(dev, res, NULL, &rtv);
    bool drew = bend_heap_draw(hp, rtv, a.x, a.y, a.w, a.h, fresh, w, h, gpu, ms);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (res) ID3D11Resource_Release(res);
    return drew;
}
#endif
