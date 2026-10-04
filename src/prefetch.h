// Stream addresses fetched ahead of playing. yt-dlp takes about 4 s to find a song's audio address,
// and the address then plays through curl in a fraction of a second (http_stream). The app names
// the songs it expects to play, most likely first; one yt-dlp at a time finds addresses for the
// first of them that have none, several in one run (each after the first takes about 2.8 s
// instead of 4). A run is stopped when the song it is working on is no longer the first one wanted.
// YouTube's addresses last six hours; one with less than an hour left is fetched again.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "account.h"
#include "tools.h"

#define PREFETCH_WANTS 8

typedef struct prefetch prefetch;

prefetch *prefetch_new(tools *t, account *a);
void prefetch_free(prefetch *pf);
// The video ids expected to play, most likely first (at most PREFETCH_WANTS), replacing the last list.
void prefetch_want(prefetch *pf, const char *const *ids, int n);
// A fresh address for the video, if one has been fetched.
bool prefetch_address(prefetch *pf, const char *id, char *url, size_t size);
bool prefetch_busy(prefetch *pf, const char *id);    // its address is being fetched now
void prefetch_forget(prefetch *pf, const char *id);  // its address did not play
// Fetches the video's address now, on the calling thread, and keeps it; setting *cancel gives up.
// False if yt-dlp found none.
bool prefetch_fetch(prefetch *pf, const char *id, char *url, size_t size, SDL_AtomicInt *cancel);
