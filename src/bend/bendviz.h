// The player's side of the visualizer effect written in Bend (viz.bend, compiled to viz_bend.c,
// with bridge.c). The Bend program runs on a thread of its own; the player asks for frames and
// takes the newest finished one, never waiting.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BENDVIZ_MAX 4096           // the largest width or height
#define BENDVIZ_PIXELS (1L << 23)  // the most pixels (3840x2160 fits): a bigger frame gets fewer rows

bool bendviz_start(const char *gpu_heap);  // once; "512MB" caps the GPU's heap, "off" keeps to the CPU
void bendviz_request(const float params[5], int w, int h, int fx, bool gpu);  // time, bass, mids, hue, beat; fx 0 plasma, 1 tree, 2 voxels
bool bendviz_wait(double ms);  // up to ms for a frame newer than the last taken: whether one is ready
void bendviz_discard(void);  // marks the newest frame taken without taking it (for measuring)
// Lends the newest finished frame (rows packed, w x h) if one arrived since the last call, else
// NULL; hand it back with bendviz_return, soon (Bend waits for it before its next frame).
const uint32_t *bendviz_borrow(int *w, int *h, bool *gpu, double *ms);
void bendviz_return(void);

// Frames drawn on the GPU can stay there when the player draws on Bend's Vulkan device and takes
// them with bendviz_vk_take; bendviz_device_frames(true) asks for that from the next frame on.
void bendviz_device_frames(bool on);
// The player on Bend's Vulkan device (see bridge.c). bendviz_vk_open opens the device with the
// extensions the player's drawing needs, before bendviz_start: a VkInstance, VkPhysicalDevice,
// VkDevice and queue family, or false.
bool bendviz_vk_open(const char *const *iexts, int niexts, const char *const *dexts, int ndexts, void **inst, void **phys, void **dev,
                     unsigned *family);
void bendviz_vk_lock(void);  // held around the player's drawing when the queue is shared
void bendviz_vk_unlock(void);
void bendviz_vk_pump(void);  // holding the lock, before each present: makes Bend's waiting submit
void bendviz_vk_mark(void);  // after each present: the draws so far are marked (see bridge.c)
void bendviz_vk_settle(void);  // with vsync, after that: waits for the frame before this one
#define BENDVIZ_TEXTURES 10  // image slots: six over Bend's frame buffers, and up to four it copies frames into
bool bendviz_vk_frames(int w, int h, bool over);  // frames of w x h go into images (B8G8R8A8, linear, GENERAL layout)
void bendviz_vk_unshare(void);  // after the player's textures of them are gone
// The slot of a new frame and its VkImage (the same for the slot until bendviz_vk_frames), or -1.
int bendviz_vk_take(int *w, int *h, bool *gpu, double *ms, unsigned long long *image);
bool bendviz_gpu(void);  // the GPU is in use
// For measuring: the heap's span, and kernel launches, time spent in the launch calls and waiting for them so far.
void bendviz_heap(void **base, size_t *bytes);
void bendviz_launches(unsigned long long *launches, double *launch_ms, double *wait_ms);
void bendviz_edges(double *before_ms, double *after_ms);
int bendviz_kernels(double *ms, double *gap_ms, unsigned *groups, int most);
void bendviz_times(double *draw_ms, double *wait_ms, double *copy_ms);  // the last frame's time: drawing (of which waiting for the GPU), and copying it back
void bendviz_host_parts(double *before_ms, double *launch_ms, double *after_ms);  // the host's part of that drawing
void bendviz_cycle(unsigned long long *drawn, unsigned long long *dropped, double *took_ms, double *began_ms, unsigned long long *faults, double *fault_ms);  // totals: see bridge.c
