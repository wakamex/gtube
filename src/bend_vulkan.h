// The player on Bend's Vulkan device, and frames of the Bend effect in images Bend copies them into
// (bend_vulkan.c).
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct bend_share bend_share;

// A window and a renderer drawing on Bend's Vulkan device and queue, so the frames Bend draws reach
// the screen without leaving the GPU, and the GPU runs one context; false (nothing made) if there is
// none, and the caller makes an ordinary renderer.
bool bend_vk_window(const char *title, int w, int h, SDL_WindowFlags flags, SDL_Window **win, SDL_Renderer **ren);
bool bend_vk_on(void);  // the renderer draws on Bend's device
// Held by the player's thread whenever it may call SDL (which submits to the queue from many calls),
// on Bend's device: from the renderer's making on, let go only while it waits.
void bend_vk_hold(void);
void bend_vk_let_go(void);
void bend_vk_pump(void);             // makes Bend's waiting submit: after drawing the effect, and before each present
void bend_vk_presented(bool vsync);  // after each present

// Frames of w x h in textures of Bend's images; NULL if the renderer isn't on Bend's device.
// Over: drawn from the buffers Bend drew them in, where it can (without vsync; see bendviz_vk_frames).
bend_share *bend_share_new(SDL_Renderer *ren, int w, int h, bool over);
void bend_share_free(bend_share *s);  // before the renderer goes
void bend_share_size(const bend_share *s, int *w, int *h, bool *over);
// Each frame: the texture to draw, the newest Bend frame (fresh if it is new), or NULL before the
// first; gpu and ms describe a fresh one.
SDL_Texture *bend_share_frame(bend_share *s, bool *fresh, bool *gpu, double *ms);
