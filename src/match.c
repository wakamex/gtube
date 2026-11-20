#include "match.h"

#include <math.h>
#include <string.h>

// Whether a part of a title is only what an upload adds to the song: the kind of video ("Official
// Music Video", "Lyrics", "Audio", "HD") or a remaster ("2011 Remastered Version"). Each word must be
// one of these, or a year, and one must say which.
static bool packaging(const char *s, size_t n) {
    static const char *const marks[] = { "video", "audio", "lyric", "lyrics", "visualizer", "visualiser", "remaster", "remastered", "hd", "4k", "hq", "explicit" };
    static const char *const also[] = { "official", "music", "digital", "version" };
    bool marked = false;
    for (size_t i = 0; i < n;) {
        while (i < n && s[i] == ' ') i++;
        size_t j = i;
        while (j < n && s[j] != ' ') j++;
        if (j == i) break;
        bool known = j - i == 4 && (!strncmp(s + i, "19", 2) || !strncmp(s + i, "20", 2)) && SDL_isdigit(s[i + 2]) && SDL_isdigit(s[i + 3]);
        for (size_t k = 0; k < SDL_arraysize(marks) && !known; k++)
            if (strlen(marks[k]) == j - i && !SDL_strncasecmp(s + i, marks[k], j - i)) known = marked = true;
        for (size_t k = 0; k < SDL_arraysize(also) && !known; k++)
            if (strlen(also[k]) == j - i && !SDL_strncasecmp(s + i, also[k], j - i)) known = true;
        if (!known) return false;
        i = j;
    }
    return marked;
}

// A song's title as its uploads share it: without packaging at its end (in brackets, or after a
// dash or a bar), and without the artist's own name before or after a dash ("Artist - Song",
// "Song - Artist"). Nothing else is taken off. Empty for titles that name no one song ("Intro").
static void song_title(const track *t, char *out, size_t size) {
    static const char *const dashes[] = { " - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", " | " };
    SDL_strlcpy(out, t->title, size);
    size_t alen = strlen(t->artist);
    for (bool cut = true; cut;) {
        cut = false;
        size_t n = strlen(out);
        while (n && out[n - 1] == ' ') out[--n] = 0;
        char close = n ? out[n - 1] : 0;
        if (close == ')' || close == ']') {  // "Song (Official Video)"
            char *open = SDL_strrchr(out, close == ')' ? '(' : '[');
            if (open && open > out && packaging(open + 1, (size_t)(out + n - 1 - (open + 1)))) *open = 0, cut = true;
            continue;
        }
        for (size_t d = 0; d < SDL_arraysize(dashes) && !cut; d++) {  // "Song - Remastered 2011", "Song | Official Video", "Song - Artist"
            char *at = NULL;
            for (char *p = strstr(out, dashes[d]); p; p = strstr(p + 1, dashes[d])) at = p;
            const char *rest = at ? at + strlen(dashes[d]) : NULL;
            if (at && (packaging(rest, strlen(rest)) || (alen && !SDL_strcasecmp(rest, t->artist)))) *at = 0, cut = true;
        }
    }
    for (size_t d = 0; d < 3; d++) {  // "Artist - Song"
        size_t dl = strlen(dashes[d]);
        if (alen && !SDL_strncasecmp(out, t->artist, alen) && !strncmp(out + alen, dashes[d], dl) && out[alen + dl])
            SDL_memmove(out, out + alen + dl, strlen(out + alen + dl) + 1);
    }
    static const char *const generic[] = { "intro", "outro", "interlude", "skit", "untitled", "prelude" };
    for (size_t i = 0; i < SDL_arraysize(generic); i++)
        if (!SDL_strcasecmp(out, generic[i])) out[0] = 0;
}

// The same song under another video: YouTube's radio offers a song's audio track and its music
// video, and other uploads of it. Two songs that differ must never count as one, so it takes the
// same video, or the same artist exactly, lengths within 5 s, and the same title once only what an
// upload adds is taken off (song_title). Other versions (live, remixes, parts) stay different.
bool same_song(const track *x, const track *y) {
    if (x->id[0] && !strcmp(x->id, y->id)) return true;
    if (!x->artist[0] || SDL_strcasecmp(x->artist, y->artist) || !SDL_strcasecmp(x->artist, "Various Artists")) return false;
    if (x->duration <= 0 || y->duration <= 0 || fabs(x->duration - y->duration) > 5) return false;
    char a[256], b[256];
    song_title(x, a, sizeof a), song_title(y, b, sizeof b);
    return a[0] && !SDL_strcasecmp(a, b);
}
