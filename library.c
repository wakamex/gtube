#include "library.h"

#include <stdlib.h>
#include <string.h>

typedef enum { LOAD_BROWSE, LOAD_SEARCH, LOAD_RADIO, LOAD_LIKE, LOAD_UNLIKE } load_kind;

typedef struct {
    library *l;
    load_kind kind;
    int shelf, gen;
    char arg[256];
    char more[2048];
    item song;  // for liking
} load;

// ---- Liked songs ----

static int cmp_id(const void *a, const void *b) { return strcmp(a, b); }

static void liked_add(library *l, const char *id) {
    if (l->nliked == l->liked_cap) l->liked = realloc(l->liked, sizeof *l->liked * (size_t)(l->liked_cap = l->liked_cap ? l->liked_cap * 2 : 256));
    SDL_strlcpy(l->liked[l->nliked++], id, sizeof *l->liked);
    l->sorted = false;
}

bool library_liked(library *l, const char *id) {
    if (!l->nliked) return false;
    if (!l->sorted) qsort(l->liked, (size_t)l->nliked, sizeof *l->liked, cmp_id), l->sorted = true;
    return bsearch(id, l->liked, (size_t)l->nliked, sizeof *l->liked, cmp_id) != NULL;
}

static void liked_remove(library *l, const char *id) {
    for (int i = 0; i < l->nliked; i++)
        if (!strcmp(l->liked[i], id)) l->liked[i][0] = 0;  // an empty id sorts first and matches nothing
    l->sorted = false;
}

// ---- Shelves ----

static void append(library *l, shelf *s, const item *items, int n) {
    if (s->n + n > s->cap) s->items = realloc(s->items, sizeof *s->items * (size_t)(s->cap = (s->n + n) * 2));
    memcpy(s->items + s->n, items, sizeof *items * (size_t)n);
    s->n += n;
    if (s == &l->shelves[SHELF_LIKED])
        for (int i = 0; i < n; i++) liked_add(l, items[i].id);
}

static void heading(library *l, shelf *s, const char *text) {
    item h = { .kind = ITEM_HEADING };
    SDL_strlcpy(h.title, text, sizeof h.title);
    append(l, s, &h, 1);
}

// Keeps the page if its shelf has not been refilled since; returns whether to go on.
static bool deliver(load *j, page *pg, bool ok) {
    library *l = j->l;
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[j->shelf];
    bool current = s->gen == j->gen;
    if (current) {
        if (ok) append(l, s, pg->items, pg->n);
        else SDL_strlcpy(s->error, pg->error, sizeof s->error);
        SDL_strlcpy(s->more, ok ? pg->more : "", sizeof s->more);
    }
    SDL_UnlockMutex(l->lock);
    return current && ok;
}

static void finish(load *j) {
    SDL_LockMutex(j->l->lock);
    if (j->l->shelves[j->shelf].gen == j->gen) j->l->shelves[j->shelf].loading = false;
    SDL_UnlockMutex(j->l->lock);
    free(j);
}

static void run(void *user) {
    load *j = user;
    library *l = j->l;
    page pg;
    switch (j->kind) {
    case LOAD_BROWSE: {  // every page
        const char *more = j->more[0] ? j->more : NULL;
        while (deliver(j, &pg, api_browse(l->account, j->arg, more, &pg)) && pg.more[0]) {
            SDL_strlcpy(j->more, pg.more, sizeof j->more);
            more = j->more;
            page_free(&pg);
        }
        page_free(&pg);
        break;
    }
    case LOAD_SEARCH: {
        static const char *const titles[] = { "Songs", "Albums", "Playlists" };
        for (int k = SEARCH_SONGS; k <= SEARCH_PLAYLISTS; k++) {
            bool ok = api_search(l->account, j->arg, (search_kind)k, &pg);
            SDL_LockMutex(l->lock);
            if (l->shelves[j->shelf].gen == j->gen && ok && pg.n) heading(l, &l->shelves[j->shelf], titles[k]);
            SDL_UnlockMutex(l->lock);
            pg.more[0] = 0;  // a search shows its first page of each kind
            bool go = deliver(j, &pg, ok);
            page_free(&pg);
            if (!go) break;
        }
        break;
    }
    case LOAD_RADIO:
        deliver(j, &pg, api_radio(l->account, j->arg, j->more[0] ? j->more : NULL, &pg));
        page_free(&pg);
        break;
    case LOAD_LIKE: case LOAD_UNLIKE: {
        bool like = j->kind == LOAD_LIKE;
        char error[160];
        if (!api_like(l->account, j->song.id, like, error, sizeof error)) {  // undo what was shown
            SDL_LockMutex(l->lock);
            if (like) liked_remove(l, j->song.id); else liked_add(l, j->song.id);
            SDL_snprintf(l->note, sizeof l->note, "could not %s %s: %s", like ? "like" : "unlike", j->song.title, error);
            SDL_UnlockMutex(l->lock);
        }
        free(j);
        return;
    }
    }
    finish(j);
}

// Empties a shelf and starts loading it.
static void start(library *l, int which, load_kind kind, const char *arg, const char *title) {
    load *j = calloc(1, sizeof *j);
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[which];
    s->n = 0, s->more[0] = 0, s->error[0] = 0, s->loading = true;
    SDL_strlcpy(s->source, arg, sizeof s->source);
    SDL_strlcpy(s->title, title ? title : "", sizeof s->title);
    if (which == SHELF_LIKED) l->nliked = 0;
    j->gen = ++s->gen;
    SDL_UnlockMutex(l->lock);
    j->l = l, j->kind = kind, j->shelf = which;
    SDL_strlcpy(j->arg, arg, sizeof j->arg);
    gs_jobs_add(l->jobs, run, j);
}

void library_init(library *l, account *a, gs_jobs *jobs) {
    memset(l, 0, sizeof *l);
    l->account = a, l->jobs = jobs;
    l->lock = SDL_CreateMutex();
}

void library_free(library *l) {
    for (int i = 0; i < SHELVES; i++) free(l->shelves[i].items);
    free(l->liked);
    SDL_DestroyMutex(l->lock);
}

void library_signed_in(library *l) {
    start(l, SHELF_LIKED, LOAD_BROWSE, LIKED_MUSIC, "Liked music");
    start(l, SHELF_PLAYLISTS, LOAD_BROWSE, LIBRARY_PLAYLISTS, "Playlists");
}

void library_open(library *l, int which, const char *id, const char *title) { start(l, which, LOAD_BROWSE, id, title); }
void library_search(library *l, const char *query) { start(l, SHELF_SEARCH, LOAD_SEARCH, query, query); }
void library_radio(library *l, const char *id, const char *title) { start(l, SHELF_RADIO, LOAD_RADIO, id, title); }

void library_radio_more(library *l) {
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[SHELF_RADIO];
    load *j = NULL;
    if (!s->loading && s->more[0]) {
        j = calloc(1, sizeof *j);
        j->l = l, j->kind = LOAD_RADIO, j->shelf = SHELF_RADIO, j->gen = s->gen;
        SDL_strlcpy(j->arg, s->source, sizeof j->arg);
        SDL_strlcpy(j->more, s->more, sizeof j->more);
        s->loading = true;
    }
    SDL_UnlockMutex(l->lock);
    if (j) gs_jobs_add(l->jobs, run, j);
}

void library_like(library *l, const item *song, bool like) {
    load *j = calloc(1, sizeof *j);
    j->l = l, j->kind = like ? LOAD_LIKE : LOAD_UNLIKE, j->song = *song;
    SDL_LockMutex(l->lock);  // shown at once; undone if YouTube refuses
    if (like) liked_add(l, song->id); else liked_remove(l, song->id);
    shelf *s = &l->shelves[SHELF_LIKED];
    if (like && s->gen) {  // newest first, as YouTube lists them
        if (s->n + 1 > s->cap) s->items = realloc(s->items, sizeof *s->items * (size_t)(s->cap = (s->n + 1) * 2));
        memmove(s->items + 1, s->items, sizeof *s->items * (size_t)s->n);
        s->items[0] = *song, s->n++;
    } else if (!like) {
        int k = 0;
        for (int i = 0; i < s->n; i++)
            if (strcmp(s->items[i].id, song->id)) s->items[k++] = s->items[i];
        s->n = k;
    }
    l->note[0] = 0;
    SDL_UnlockMutex(l->lock);
    gs_jobs_add(l->jobs, run, j);
}
