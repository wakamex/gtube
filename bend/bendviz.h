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
bool bendviz_gpu(void);  // the GPU is in use
void bendviz_times(double *draw_ms, double *copy_ms);  // the last frame's time: drawing, and copying it back
