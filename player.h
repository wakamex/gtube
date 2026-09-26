// The queue and playback: tracks listed from any YouTube or YouTube Music link by yt-dlp, each played
// by streaming its Opus audio from yt-dlp through gs_webm, gs_opus and a gs_stream into the mixer.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "gs_jobs.h"
#include "gs_stream.h"
#include "tools.h"

typedef struct {
    char id[32], title[256], artist[160];
    double duration;  // seconds, 0 if unknown
} track;

typedef struct player player;

player *player_new(tools *t, gs_jobs *jobs, bool audio);
void player_free(player *p);
void player_add(player *p, const char *url);  // lists the link's tracks in the background, then plays if idle
void player_add_track(player *p, const track *t);  // (for display tests: queues without listing or playing)
void player_play(player *p, int index);
void player_next(player *p);
void player_previous(player *p);  // restarts the track if more than 3 s in
void player_toggle_pause(player *p);
void player_update(player *p);    // call every frame: moves on at the end of a track

// For the display. The queue is copied under the player's lock.
int player_queue(player *p, track *out, int max, int *current);
double player_position(player *p);
double player_duration(player *p);
bool player_paused(player *p);
bool player_loading(player *p);        // listing a link, or waiting for a track's first audio
void player_status(player *p, char *out, size_t size);

gs_stream *player_stream(player *p);   // the current track's stream (for offline rendering), or NULL
