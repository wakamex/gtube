// The lists the player shows besides its queue: liked music, the library's playlists, search
// results, an opened album or playlist, and the radio being played. Each loads in the background,
// every page of it, and can be read at any time under the library's lock. Also tracks which songs
// are liked, from the Liked music list, and likes or unlikes them.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "api.h"
#include "gs_jobs.h"

enum { SHELF_LIKED, SHELF_PLAYLISTS, SHELF_SEARCH, SHELF_OPEN, SHELF_RADIO, SHELVES };

typedef struct {
    item *items;
    int n, cap;
    char title[256];   // of an opened album or playlist, or the search
    char source[128];  // the browse id, query or seed song
    char more[2048];   // the next page, when there is one
    bool loading;
    char error[160];
    int gen;           // bumped whenever the shelf is refilled, so older loads drop their pages
} shelf;

typedef struct {
    account *account;
    gs_jobs *jobs;
    SDL_Mutex *lock;  // guards everything below
    shelf shelves[SHELVES];
    char (*liked)[16];  // ids of liked songs, sorted when `sorted`
    int nliked, liked_cap;
    bool sorted;
    char note[160];     // the last like or unlike that failed
} library;

void library_init(library *l, account *a, gs_jobs *jobs);
void library_free(library *l);  // after the jobs have finished

void library_signed_in(library *l);  // (re)loads Liked music and the playlists
void library_open(library *l, int shelf, const char *browse_id, const char *title);
void library_search(library *l, const char *query);
void library_radio(library *l, const char *video_id, const char *title);
void library_radio_more(library *l);  // the radio's next page, if it has one and is not loading

bool library_liked(library *l, const char *video_id);  // call under the lock
void library_like(library *l, const item *song, bool like);
