// The visualizer: what is playing, analysed (a 64-band spectrum with falling peaks, the waveform,
// bass, mids, treble and beats) and drawn by one of several effects after Winamp, Milkdrop and the
// demoscene, into a low-resolution framebuffer scaled up with crisp pixels, with a sine scroller
// carrying the track's name.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "gs_text.h"

typedef struct viz viz;

viz *viz_new(SDL_Renderer *ren, int rate);
void viz_free(viz *v);
// Moves the textures to another renderer: NULL destroys them (before their renderer goes).
void viz_set_renderer(viz *v, SDL_Renderer *ren);

// The latest audio as heard (interleaved stereo), once per frame; dt is the time since the last.
void viz_feed(viz *v, const float *lr, int frames, double dt);
void viz_draw(viz *v, SDL_FRect area, double t, const char *title, const char *artist, gs_glyphs *g, gs_fontset *f);

void viz_step(viz *v, int dir);  // the next (1) or previous (-1) effect
bool viz_is_bend(const viz *v);     // the effect written in Bend is showing
bool viz_bend_stats(const viz *v, char *out, size_t size);  // its last frame, for the stats overlay
void viz_bend_switch(viz *v);       // moves that effect between the GPU and the CPU
void viz_bend_ready(viz *v);        // the renderer is settled for that effect: its program may start
void viz_set_vsync(viz *v, bool on);  // whether presents wait for the vertical blank
const char *viz_name(const viz *v);
int viz_index(const viz *v, int *count);
// Auto changes the effect every 40 s and with each new track; both it and the scroller start on.
void viz_set_auto(viz *v, bool on);
void viz_set_scroller(viz *v, bool on);
bool viz_get_auto(const viz *v);
bool viz_get_scroller(const viz *v);

// A test signal (a four-on-the-floor kick, hats, a bass line and chords) at absolute time t, for
// showing the visualizer without playing anything.
void viz_test_signal(double t, float *lr, int frames, int rate);
