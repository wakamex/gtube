// Bend's GPU heap as a Direct3D 11 buffer shared with CUDA (Windows): the player makes the buffer
// and two fences before Bend starts, and Bend's heap is that buffer (bridge.c, bv_heap). Each frame
// a small shader reads the pixels straight from where Bend drew them, so no frame is copied into a
// texture (0.1 ms of the GPU's time a 4K frame). Direct3D 11 shares a buffer only through a legacy
// (KMT) handle, which CUDA imports; the fences are shared as NT handles.
#include "bend_heap.h"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <string.h>

#include "bend/bendviz.h"

struct bend_heap {
    ID3D11Device *dev;
    ID3D11DeviceContext4 *ctx;
    ID3D11Buffer *buf, *consts;
    ID3D11ShaderResourceView *srv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11RasterizerState *rs;
    ID3D11Fence *fence_cuda, *fence_d3d;  // CUDA signals the first, Direct3D the second
    HANDLE fence_cuda_handle, fence_d3d_handle;
    UINT64 d3d_value;
    unsigned long long at;  // the frame drawn: its byte in the buffer, its size
    int w, h;
    bool have;
};

// One triangle over the viewport; each pixel reads its frame pixel (scaled to the rectangle) from
// the heap, ARGB as Bend packs it.
static const char shader_src[] =
    "ByteAddressBuffer heap : register(t0);\n"
    "cbuffer frame : register(b0) { uint at; uint fw; uint fh; uint pad; float4 rect; };\n"
    "float4 vs(uint id : SV_VertexID) : SV_Position {\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 ps(float4 pos : SV_Position) : SV_Target {\n"
    "    float2 f = (pos.xy - rect.xy) / rect.zw;\n"
    "    uint x = min((uint)(f.x * fw), fw - 1), y = min((uint)(f.y * fh), fh - 1);\n"
    "    uint v = heap.Load(at + (y * fw + x) * 4);\n"
    "    return float4((v >> 16) & 255, (v >> 8) & 255, v & 255, 255) / 255.0;\n"
    "}\n";

typedef HRESULT(WINAPI *compile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static ID3DBlob *compile(compile_fn fn, const char *entry, const char *target) {
    ID3DBlob *code = NULL, *errors = NULL;
    fn(shader_src, sizeof shader_src - 1, "bend_heap", NULL, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) ID3D10Blob_Release(errors);
    return code;
}

bend_heap *bend_heap_new(void *device, unsigned long long bytes) {
    ID3D11Device *dev = device;
    ID3D11Device5 *dev5 = NULL;
    ID3D11DeviceContext *ctx = NULL;
    ID3DBlob *vcode = NULL, *pcode = NULL;
    bend_heap *hp = calloc(1, sizeof *hp);
    HMODULE lib = LoadLibraryA("d3dcompiler_47.dll");
    compile_fn fn = lib ? (compile_fn)(void *)GetProcAddress(lib, "D3DCompile") : NULL;
    if (!dev || !hp || !fn || bytes > 0xFFFFFFFFull || FAILED(ID3D11Device_QueryInterface(dev, &IID_ID3D11Device5, (void **)&dev5))) goto fail;
    hp->dev = dev;
    ID3D11Device_AddRef(dev);
    ID3D11Device_GetImmediateContext(dev, &ctx);
    if (FAILED(ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11DeviceContext4, (void **)&hp->ctx))) goto fail;
    D3D11_BUFFER_DESC bd = { (UINT)bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                             D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = { .Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
                                           .BufferEx = { 0, (UINT)(bytes / 4), D3D11_BUFFEREX_SRV_FLAG_RAW } };
    D3D11_BUFFER_DESC cd = { 32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_RASTERIZER_DESC rd = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0, 0, TRUE, FALSE, FALSE, FALSE };
    IDXGIResource *r = NULL;
    HANDLE buf_handle = NULL;
    bool ok = SUCCEEDED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &hp->buf))
        && SUCCEEDED(ID3D11Buffer_QueryInterface(hp->buf, &IID_IDXGIResource, (void **)&r))
        && SUCCEEDED(IDXGIResource_GetSharedHandle(r, &buf_handle))
        && SUCCEEDED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)hp->buf, &sd, &hp->srv))
        && SUCCEEDED(ID3D11Device_CreateBuffer(dev, &cd, NULL, &hp->consts))
        && SUCCEEDED(ID3D11Device_CreateRasterizerState(dev, &rd, &hp->rs))
        && (vcode = compile(fn, "vs", "vs_5_0")) && (pcode = compile(fn, "ps", "ps_5_0"))
        && SUCCEEDED(ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vcode), ID3D10Blob_GetBufferSize(vcode), NULL, &hp->vs))
        && SUCCEEDED(ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(pcode), ID3D10Blob_GetBufferSize(pcode), NULL, &hp->ps))
        && SUCCEEDED(ID3D11Device5_CreateFence(dev5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence, (void **)&hp->fence_cuda))
        && SUCCEEDED(ID3D11Device5_CreateFence(dev5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence, (void **)&hp->fence_d3d))
        && SUCCEEDED(ID3D11Fence_CreateSharedHandle(hp->fence_cuda, NULL, GENERIC_ALL, NULL, &hp->fence_cuda_handle))
        && SUCCEEDED(ID3D11Fence_CreateSharedHandle(hp->fence_d3d, NULL, GENERIC_ALL, NULL, &hp->fence_d3d_handle));
    if (r) IDXGIResource_Release(r);
    if (vcode) ID3D10Blob_Release(vcode);
    if (pcode) ID3D10Blob_Release(pcode);
    if (!ok) goto fail;
    ID3D11Device5_Release(dev5);
    ID3D11DeviceContext_Release(ctx);
    bendviz_d3d11_heap(buf_handle, bytes, hp->fence_cuda_handle, hp->fence_d3d_handle);
    return hp;
fail:
    if (dev5) ID3D11Device5_Release(dev5);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    bend_heap_free(hp);
    return NULL;
}

bool bend_heap_failed(void) { return bendviz_d3d11_heap_state() < 0; }

bool bend_heap_draw(bend_heap *hp, void *target, float x, float y, float w, float h, bool *fresh, int *fw, int *fh, bool *gpu, double *ms) {
    // Direct3D reaches this value once done with everything submitted so far, which covers every
    // draw from an older frame; Bend waits for it before drawing into that frame's buffer again.
    ID3D11DeviceContext4_Signal(hp->ctx, hp->fence_d3d, ++hp->d3d_value);
    unsigned long long wait, at;
    int nw, nh;
    *fresh = bendviz_d3d11_heap_take(hp->d3d_value, &wait, &at, &nw, &nh, gpu, ms) == 1;
    if (*fresh) {
        ID3D11DeviceContext4_Wait(hp->ctx, hp->fence_cuda, wait);  // Bend's frame is done
        hp->at = at, hp->w = *fw = nw, hp->h = *fh = nh, hp->have = true;
    }
    if (!hp->have || !target) return false;
    ID3D11DeviceContext *c = (ID3D11DeviceContext *)hp->ctx;
    struct { UINT at, fw, fh, pad; float rect[4]; } k = { (UINT)hp->at, (UINT)hp->w, (UINT)hp->h, 0, { x, y, w, h } };
    ID3D11DeviceContext_UpdateSubresource(c, (ID3D11Resource *)hp->consts, 0, NULL, &k, 0, 0);
    ID3D11RenderTargetView *rtv = target;
    D3D11_VIEWPORT vp = { x, y, w, h, 0, 1 };
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(c, NULL, NULL, 0xFFFFFFFF);
    ID3D11DeviceContext_OMSetDepthStencilState(c, NULL, 0);
    ID3D11DeviceContext_RSSetState(c, hp->rs);
    ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
    ID3D11DeviceContext_IASetInputLayout(c, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(c, hp->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(c, hp->ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &hp->srv);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &hp->consts);
    ID3D11DeviceContext_Draw(c, 3, 0);
    ID3D11ShaderResourceView *none = NULL;
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &none);
    return true;
}

void bend_heap_free(bend_heap *hp) {
    if (!hp) return;
    if (hp->ctx) ID3D11DeviceContext4_Flush(hp->ctx);  // (Bend's GPU may wait for a signal queued here)
    if (hp->fence_d3d_handle) bendviz_d3d11_heap_stop();  // Bend lets go first (it was shared)
    if (hp->vs) ID3D11VertexShader_Release(hp->vs);
    if (hp->ps) ID3D11PixelShader_Release(hp->ps);
    if (hp->rs) ID3D11RasterizerState_Release(hp->rs);
    if (hp->consts) ID3D11Buffer_Release(hp->consts);
    if (hp->srv) ID3D11ShaderResourceView_Release(hp->srv);
    if (hp->buf) ID3D11Buffer_Release(hp->buf);
    if (hp->fence_cuda_handle) CloseHandle(hp->fence_cuda_handle);
    if (hp->fence_d3d_handle) CloseHandle(hp->fence_d3d_handle);
    if (hp->fence_cuda) ID3D11Fence_Release(hp->fence_cuda);
    if (hp->fence_d3d) ID3D11Fence_Release(hp->fence_d3d);
    if (hp->ctx) ID3D11DeviceContext4_Release(hp->ctx);
    if (hp->dev) ID3D11Device_Release(hp->dev);
    free(hp);
}
#endif
