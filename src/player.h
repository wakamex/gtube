// The queue and playback: tracks listed from any YouTube or YouTube Music link by yt-dlp, each played
// by streaming its Opus audio through gs_webm, gs_opus and a gs_stream into the mixer: from an
// address fetched ahead of time (prefetch.h) when there is one, and otherwise from yt-dlp itself.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "account.h"
#include "gs_jobs.h"
#include "gs_stream.h"
#include "prefetch.h"
#include "tools.h"

typedef struct {
    char id[32], title[256], artist[160];
    double duration;  // seconds, 0 if unknown
} track;

typedef struct player player;

player *player_new(tools *t, account *a, gs_jobs *jobs, bool audio);
void player_free(player *p);
void player_add(player *p, const char *url);  // lists the link's tracks in the background, then plays if idle
void player_add_track(player *p, const track *t);  // queues a known track without listing it
// Replaces the queue and plays tracks[start], unless that song is already playing, which goes on.
void player_set_queue(player *p, const track *tracks, int n, int start);
void player_play(player *p, int index);
void player_next(player *p);
void player_previous(player *p);  // restarts the track if more than 3 s in
void player_toggle_pause(player *p);  // or plays the current track when nothing is playing
// Restores a saved queue with `current` shown as the track to play, without playing it.
void player_load(player *p, const track *tracks, int n, int current);
int player_version(player *p);        // changes whenever the queue or the current track does
void player_update(player *p);    // call every frame: moves on at the end of a track, and prefetches

// For the display. The queue is copied under the player's lock.
int player_queue(player *p, track *out, int max, int *current);
double player_position(player *p);
double player_duration(player *p);
bool player_paused(player *p);
bool player_loading(player *p);        // listing a link, or waiting for a track's first audio
void player_status(player *p, char *out, size_t size);

gs_stream *player_stream(player *p);   // the current track's stream (for offline rendering), or NULL
