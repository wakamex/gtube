#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 2000

static void path(const char *dir, const char *name, char *out, size_t size) { snprintf(out, size, "%s%s", dir, name); }

static void save(const char *dir, const char *name, const char *text, size_t len) {
    char final[1200], tmp[1210];
    path(dir, name, final, sizeof final);
    snprintf(tmp, sizeof tmp, "%s.tmp", final);
    if (SDL_SaveFile(tmp, text, len)) SDL_RenamePath(tmp, final);
}

bool state_load_window(const char *dir, window_state *out) {
    char file[1200];
    path(dir, "window.txt", file, sizeof file);
    char *text = SDL_LoadFile(file, NULL);
    int max = 0;
    bool ok = text && sscanf(text, "%d %d %d %d %d", &out->x, &out->y, &out->w, &out->h, &max) == 5 && out->w >= 200 && out->h >= 150;
    out->maximized = max != 0;
    SDL_free(text);
    return ok;
}

void state_save_window(const char *dir, const window_state *ws) {
    char text[100];
    int n = snprintf(text, sizeof text, "%d %d %d %d %d\n", ws->x, ws->y, ws->w, ws->h, ws->maximized);
    save(dir, "window.txt", text, (size_t)n);
}

// Tabs and line breaks would split a line; a title with one keeps a space instead.
static void field(char *out, size_t *n, size_t cap, const char *s) {
    for (; *s && *n + 2 < cap; s++) out[(*n)++] = *s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s;
}

// queue.txt: "current N", then "radio SEED<tab>TITLE<tab>TOKEN" if the queue is a radio, then one
// track per line: id, title, artist and duration, tab-separated.
void state_save_queue(const char *dir, player *p, const radio_state *radio) {
    track *q = malloc(sizeof *q * MAX_TRACKS);
    int current, n = player_queue(p, q, MAX_TRACKS, &current);
    size_t cap = 64 + sizeof *radio + (size_t)n * (sizeof *q + 16), len = 0;
    char *text = malloc(cap);
    len += (size_t)snprintf(text, cap, "current %d\n", current);
    if (radio->on && radio->more[0]) {
        memcpy(text + len, "radio ", 6), len += 6;
        field(text, &len, cap, radio->seed), text[len++] = '\t';
        field(text, &len, cap, radio->title), text[len++] = '\t';
        field(text, &len, cap, radio->more), text[len++] = '\n';
    }
    for (int i = 0; i < n; i++) {
        field(text, &len, cap, q[i].id), text[len++] = '\t';
        field(text, &len, cap, q[i].title), text[len++] = '\t';
        field(text, &len, cap, q[i].artist);
        len += (size_t)snprintf(text + len, cap - len, "\t%.0f\n", q[i].duration);
    }
    save(dir, "queue.txt", text, len);
    free(text), free(q);
}

void state_load_queue(const char *dir, player *p, radio_state *radio) {
    memset(radio, 0, sizeof *radio);
    char file[1200];
    path(dir, "queue.txt", file, sizeof file);
    char *text = SDL_LoadFile(file, NULL);
    if (!text) return;
    track *q = calloc(MAX_TRACKS, sizeof *q);
    int n = 0, current = -1;
    char *save_line;
    for (char *line = SDL_strtok_r(text, "\n", &save_line); line && n < MAX_TRACKS; line = SDL_strtok_r(NULL, "\n", &save_line)) {
        if (!strncmp(line, "current ", 8)) { current = atoi(line + 8); continue; }
        if (!strncmp(line, "radio ", 6)) {
            char *f[3] = { line + 6, NULL, NULL };
            for (int k = 1; k < 3; k++) f[k] = f[k - 1] ? strchr(f[k - 1], '\t') : NULL, f[k] ? (*f[k]++ = 0) : 0;
            if (f[2] && f[2][0]) {
                radio->on = true;
                SDL_strlcpy(radio->seed, f[0], sizeof radio->seed);
                SDL_strlcpy(radio->title, f[1], sizeof radio->title);
                SDL_strlcpy(radio->more, f[2], sizeof radio->more);
            }
            continue;
        }
        char *f[4] = { line, NULL, NULL, NULL };
        for (int k = 1; k < 4; k++) f[k] = f[k - 1] ? strchr(f[k - 1], '\t') : NULL, f[k] ? (*f[k]++ = 0) : 0;
        if (!f[3] || !f[0][0] || strlen(f[0]) >= sizeof q->id) continue;
        SDL_strlcpy(q[n].id, f[0], sizeof q->id);
        SDL_strlcpy(q[n].title, f[1], sizeof q->title);
        SDL_strlcpy(q[n].artist, f[2], sizeof q->artist);
        q[n++].duration = atof(f[3]);
    }
    if (n) player_load(p, q, n, current);
    free(q);
    SDL_free(text);
}
