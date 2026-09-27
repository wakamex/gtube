// The player's side of the visualizer effect written in Bend (viz.bend, compiled to viz_bend.c,
// with bridge.c). The Bend program runs on a thread of its own; the player asks for frames and
// takes the newest finished one, never waiting.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BENDVIZ_MAX 4096  // the largest width or height

bool bendviz_start(const char *gpu_heap);  // once; "512MB" caps the GPU's heap, "off" keeps to the CPU
void bendviz_request(const float params[5], int w, int h, bool gpu);  // time, bass, mids, hue, beat
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
#endif
bool bendviz_gpu(void);  // the GPU is in use
void bendviz_times(double *draw_ms, double *copy_ms);  // the last frame's time: drawing, and copying it back
