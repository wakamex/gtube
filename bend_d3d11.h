// Frames of the Bend effect into Direct3D 11 textures shared with CUDA, on Windows (bend_d3d11.c).
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "bend_heap.h"

typedef struct bend_share bend_share;

// Three textures of w x h and two fences, shared with Bend; NULL if the renderer's Direct3D 11 device
// can't (the player then maps its texture each frame instead).
bend_share *bend_share_new(SDL_Renderer *ren, int w, int h);
void bend_share_free(bend_share *s);  // before the renderer goes
bool bend_share_failed(void);         // Bend couldn't import them
void bend_share_size(const bend_share *s, int *w, int *h);
// Each frame: the texture to draw, the newest Bend frame (fresh if it is new), or NULL before the
// first; gpu and ms describe a fresh one.
SDL_Texture *bend_share_frame(bend_share *s, bool *fresh, bool *gpu, double *ms);

// Bend's GPU heap as a buffer of the renderer's Direct3D 11 device, frames drawn from it where Bend
// drew them (bend_heap.c): made before Bend starts; NULL if the device can't.
bend_heap *bend_heap_for(SDL_Renderer *ren, unsigned long long bytes);
// Each frame: takes the newest frame and draws it over a on the renderer's target, between the
// renderer's own drawing (as bend_heap_draw).
bool bend_heap_render(bend_heap *hp, SDL_Renderer *ren, SDL_FRect a, bool *fresh, int *w, int *h, bool *gpu, double *ms);
