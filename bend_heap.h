// Bend's GPU heap as a Direct3D 11 buffer shared with CUDA, each frame drawn from where Bend drew
// it (Windows; bend_heap.c). Needs no SDL, so the headless harness (bend/bench_d3d.c) uses it as
// gtube does (bend_d3d11.c).
#pragma once
#include <stdbool.h>

typedef struct bend_heap bend_heap;

// Before bendviz_start: a buffer of `bytes` (at least Bend's GPU heap) and two fences on dev (an
// ID3D11Device*), shared with Bend. NULL if the device can't.
bend_heap *bend_heap_new(void *dev, unsigned long long bytes);
// Each frame, once the caller's drawing so far is submitted to the device's context: takes the
// newest frame if one came (*fresh; fw, fh, gpu and ms then describe it), and draws the newest
// taken into rtv (an ID3D11RenderTargetView*) over the rectangle x, y, w, h in pixels, scaled.
// False, drawing nothing, before the first frame. With rtv NULL it only takes (Bend waits for that).
bool bend_heap_draw(bend_heap *hp, void *rtv, float x, float y, float w, float h, bool *fresh, int *fw, int *fh, bool *gpu, double *ms);
bool bend_heap_failed(void);  // Bend's heap could not be the buffer (frames then come another way)
void bend_heap_free(bend_heap *hp);  // stops Bend for good first
