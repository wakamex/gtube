// YouTube Music's own web API (the one music.youtube.com's page calls), over gs_http.h:
// search, playlists and albums, liked music, the library, radio, and liking. Requests are signed
// with the session when there is one. A response is read by walking its whole tree for list items
// and the continuation token, which holds up as YouTube rearranges its page layouts.
#pragma once
#include <stdbool.h>

#include "account.h"

typedef enum { ITEM_SONG, ITEM_ALBUM, ITEM_PLAYLIST, ITEM_HEADING } item_kind;

typedef struct {
    item_kind kind;
    char id[64];  // a song's video id, or an album's or playlist's browse id
    char title[256];
    char artist[160];  // for a song; an album's or playlist's subtitle
    double duration;   // seconds, 0 if unknown
} item;

typedef struct {
    item *items;
    int n;
    char more[2048];  // continuation token for the next page, or empty at the end
    bool radio;       // `more` continues a radio (api_radio), not a page (api_browse)
    char error[160];
} page;

typedef enum { SEARCH_SONGS, SEARCH_ALBUMS, SEARCH_PLAYLISTS } search_kind;

// Each blocks (run them as jobs) and fills `out`, which the caller frees with page_free. False on
// failure, with out->error saying why.
bool api_search(account *a, const char *query, search_kind kind, page *out);
bool api_browse(account *a, const char *browse_id, const char *more, page *out);  // `more` NULL for the first page
bool api_radio(account *a, const char *video_id, const char *more, page *out);
bool api_like(account *a, const char *video_id, bool like, char *error, size_t size);
void page_free(page *p);

#define LIKED_MUSIC "VLLM"                     // browse id of the Liked music playlist
#define LIBRARY_PLAYLISTS "FEmusic_liked_playlists"  // the library's playlists
