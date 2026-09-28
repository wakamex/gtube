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
void bendviz_request(const float params[5], int w, int h, bool gpu);  // time, bass, mids, hue, beat
bool bendviz_wait(double ms);  // up to ms for a frame newer than the last taken: whether one is ready
// Lends the newest finished frame (rows packed, w x h) if one arrived since the last call, else
// NULL; hand it back with bendviz_return, soon (Bend waits for it before its next frame).
const uint32_t *bendviz_borrow(int *w, int *h, bool *gpu, double *ms);
void bendviz_return(void);

// Frames drawn on the GPU can stay there (graphics interop) when the player takes them with
// bendviz_to_d3d11; bendviz_device_frames(true) asks for that from the next frame on.
void bendviz_device_frames(bool on);
#ifdef _WIN32
// Copies the newest device frame into a Direct3D 11 texture (ID3D11Texture2D*, ARGB, tw x th),
// on the GPU: 1 done; 0 no such frame (w and h give a waiting frame's size, for a texture that
// size); -1 interop failed (stop asking for device frames).
int bendviz_to_d3d11(void *texture, int tw, int th, int *w, int *h, bool *gpu, double *ms);
void bendviz_release_d3d11(void);  // before destroying that texture or the renderer
void bendviz_interop_apart(bool apart);  // presents wait for the vertical blank (vsync)
#endif
bool bendviz_gpu(void);  // the GPU is in use
// For measuring: the heap's span, and kernel launches, time spent in the launch calls and waiting for them so far.
void bendviz_heap(void **base, size_t *bytes);
void bendviz_launches(unsigned long long *launches, double *launch_ms, double *wait_ms);
void bendviz_edges(double *before_ms, double *after_ms);
int bendviz_kernels(double *ms, double *gap_ms, unsigned *groups, int most);
void bendviz_times(double *draw_ms, double *wait_ms, double *copy_ms);  // the last frame's time: drawing (of which waiting for the GPU), and copying it back
void bendviz_host_parts(double *before_ms, double *launch_ms, double *after_ms);  // the host's part of that drawing
void bendviz_cycle(unsigned long long *drawn, unsigned long long *dropped, double *took_ms, double *began_ms, unsigned long long *faults, double *fault_ms);  // totals: see bridge.c
