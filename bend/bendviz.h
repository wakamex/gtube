// The player's side of the visualizer effect written in Bend (viz.bend, compiled to viz_bend.c,
// with bridge.c). The Bend program runs on a thread of its own; the player asks for frames and
// takes the newest finished one, never waiting.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BENDVIZ_W 1024
#define BENDVIZ_H 512

bool bendviz_start(const char *gpu_heap);  // once; "512MB" caps the GPU's heap, "off" keeps to the CPU
void bendviz_request(const float params[5], int w, int h, bool gpu);  // time, bass, mids, hue, beat
bool bendviz_take(uint32_t *out, int *w, int *h, bool *gpu, double *ms);  // rows BENDVIZ_W apart
bool bendviz_gpu(void);  // the GPU is in use
