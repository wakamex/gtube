// What the player remembers between runs, as small text files in its data folder: the window's
// size, position and whether it was maximised (window.txt), and the queue with its current track
// (queue.txt). Each is written to a temporary file and renamed over the old one.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "player.h"

typedef struct {
    int x, y, w, h;  // the size and position when not maximised
    bool maximized;
} window_state;

bool state_load_window(const char *dir, window_state *out);
void state_save_window(const char *dir, const window_state *ws);
void state_load_queue(const char *dir, player *p);
void state_save_queue(const char *dir, player *p);
