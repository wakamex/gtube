// The player on Bend's Vulkan device: SDL's Vulkan renderer made on the device and queue Bend runs
// on, and four textures of images Bend makes and copies each frame into (see bend/bridge.c). The one
// queue runs Bend's kernels, its copies and the player's drawing in the order they are submitted, so
// no fences are needed, and the GPU never turns to another context.
#include "bend_vulkan.h"

#include <SDL3/SDL_vulkan.h>

#include "bend/bendviz.h"

struct bend_share {
    int w, h;
    SDL_Texture *sdl[BENDVIZ_TEXTURES];
    int shown;
};

static bool on_bend;  // the renderer draws on Bend's device

bool bend_vk_window(const char *title, int w, int h, SDL_WindowFlags flags, SDL_Window **win, SDL_Renderer **ren) {
    *win = SDL_CreateWindow(title, w, h, flags | SDL_WINDOW_VULKAN);
    if (!*win) return false;
    Uint32 n = 0;
    char const *const *iexts = SDL_Vulkan_GetInstanceExtensions(&n);
    static const char *const dexts[] = { "VK_KHR_swapchain" };
    void *inst, *phys, *dev;
    unsigned family;
    if (iexts && bendviz_vk_open(iexts, (int)n, dexts, 1, &inst, &phys, &dev, &family)) {
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, *win);
        SDL_SetStringProperty(p, SDL_PROP_RENDERER_CREATE_NAME_STRING, "vulkan");
        SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, inst);
        SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, phys);
        SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER, dev);
        SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER, family);
        SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PRESENT_QUEUE_FAMILY_INDEX_NUMBER, family);
        *ren = SDL_CreateRendererWithProperties(p);
        SDL_DestroyProperties(p);
        if (*ren) return on_bend = true;
        SDL_Log("bend: no renderer on Bend's Vulkan device: %s", SDL_GetError());
    }
    SDL_DestroyWindow(*win);
    *win = NULL;
    return false;
}

bool bend_vk_on(void) { return on_bend; }

void bend_vk_hold(void) {
    if (on_bend) bendviz_vk_lock();
}

void bend_vk_let_go(void) {
    if (on_bend) bendviz_vk_unlock();
}

// With vsync the player keeps at most one frame on the GPU (see bendviz_vk_settle).
void bend_vk_presented(bool vsync) {
    if (!on_bend) return;
    bendviz_vk_mark();
    if (vsync) bendviz_vk_settle();
}

bend_share *bend_share_new(SDL_Renderer *ren, int w, int h) {
    unsigned long long image[BENDVIZ_TEXTURES];
    bend_share *s = on_bend ? SDL_calloc(1, sizeof *s) : NULL;
    if (!s || !bendviz_vk_images(w, h, image)) goto fail;
    s->w = w, s->h = h, s->shown = -1;
    for (int i = 0; i < BENDVIZ_TEXTURES; i++) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER, (Sint64)image[i]);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER, 1);  // VK_IMAGE_LAYOUT_GENERAL
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ARGB8888);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        s->sdl[i] = SDL_CreateTextureWithProperties(ren, props);
        SDL_DestroyProperties(props);
        if (!s->sdl[i]) goto fail;
        SDL_SetTextureBlendMode(s->sdl[i], SDL_BLENDMODE_NONE);
    }
    return s;
fail:
    bend_share_free(s);
    return NULL;
}

void bend_share_free(bend_share *s) {
    if (!s) return;
    for (int i = 0; i < BENDVIZ_TEXTURES; i++)
        if (s->sdl[i]) SDL_DestroyTexture(s->sdl[i]);
    bendviz_vk_unshare();  // (the images go once the queue is done with them)
    SDL_free(s);
}

void bend_share_size(const bend_share *s, int *w, int *h) { *w = s->w, *h = s->h; }

// The texture drawn last is Bend's to write again once the draws of it are done (see bendviz_vk_mark).
SDL_Texture *bend_share_frame(bend_share *s, bool *fresh, bool *gpu, double *ms) {
    int w, h;
    int slot = bendviz_vk_take(&w, &h, gpu, ms);
    *fresh = slot >= 0;
    if (slot >= 0) s->shown = slot;
    return s->shown >= 0 ? s->sdl[s->shown] : NULL;
}
