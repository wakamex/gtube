// The queue the player remembers between runs, with its current track (queue.txt in its data folder,
// and the radio it is, if any), written to a temporary file and renamed over the old one. The window's
// state is gesso's gs_window_state (window.txt).
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "player.h"

typedef struct {
    bool on;          // the queue is a radio
    char seed[64];    // the song it started from
    char title[256];
    char more[2048];  // YouTube's token for its next page
} radio_state;

void state_load_queue(const char *dir, player *p, radio_state *radio);
void state_save_queue(const char *dir, player *p, const radio_state *radio);
