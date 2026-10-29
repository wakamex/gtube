#include "library.h"

#include <stdlib.h>
#include <string.h>

typedef enum { LOAD_BROWSE, LOAD_SEARCH, LOAD_RADIO, LOAD_RATE } load_kind;

typedef struct {
    library *l;
    load_kind kind;
    int shelf, gen;
    char arg[256];
    char more[2048];
    search_kind search;
    item song;  // for rating
    rating rate, was;
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

bool library_disliked(library *l, const char *id) {
    for (int i = 0; i < l->ndisliked; i++)
        if (!strcmp(l->disliked[i], id)) return true;
    return false;
}

static void disliked_set(library *l, const char *id, bool on) {
    for (int i = 0; i < l->ndisliked; i++)
        if (!strcmp(l->disliked[i], id)) { if (!on) SDL_memmove(l->disliked + i, l->disliked + i + 1, sizeof *l->disliked * (size_t)(--l->ndisliked - i)); return; }
    if (!on) return;
    l->disliked = realloc(l->disliked, sizeof *l->disliked * (size_t)(l->ndisliked + 1));
    SDL_strlcpy(l->disliked[l->ndisliked++], id, sizeof *l->disliked);
}

// ---- Shelves ----

static void append(library *l, shelf *s, const item *items, int n) {
    if (s->n + n > s->cap) s->items = realloc(s->items, sizeof *s->items * (size_t)(s->cap = (s->n + n) * 2));
    memcpy(s->items + s->n, items, sizeof *items * (size_t)n);
    s->n += n;
    if (s == &l->shelves[SHELF_LIKED])
        for (int i = 0; i < n; i++) liked_add(l, items[i].id);
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
    case LOAD_SEARCH: {  // up to five pages of one kind (everything comes as one)
        const char *more = NULL;
        for (int pages = 1; deliver(j, &pg, api_search(l->account, j->arg, j->search, more, &pg)) && pg.more[0] && pages < 5; pages++) {
            SDL_strlcpy(j->more, pg.more, sizeof j->more);
            more = j->more;
            page_free(&pg);
        }
        page_free(&pg);
        break;
    }
    case LOAD_RADIO:
        deliver(j, &pg, api_radio(l->account, j->arg, j->more[0] ? j->more : NULL, &pg));
        page_free(&pg);
        break;
    case LOAD_RATE: {
        static const char *const verbs[] = { "clear the rating of", "like", "dislike" };
        char error[160];
        if (!api_rate(l->account, j->song.id, j->rate, error, sizeof error)) {  // undo what was shown
            SDL_LockMutex(l->lock);
            if (j->was == RATE_LIKE) liked_add(l, j->song.id); else liked_remove(l, j->song.id);
            disliked_set(l, j->song.id, j->was == RATE_DISLIKE);
            SDL_snprintf(l->note, sizeof l->note, "could not %s %s: %s", verbs[j->rate], j->song.title, error);
            SDL_UnlockMutex(l->lock);
        }
        free(j);
        return;
    }
    }
    finish(j);
}

// Empties a shelf for a load, which the caller starts.
static load *prepare(library *l, int which, load_kind kind, const char *arg, const char *title) {
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
    return j;
}

// Empties a shelf and starts loading it.
static void start(library *l, int which, load_kind kind, const char *arg, const char *title) {
    gs_jobs_add(l->jobs, run, prepare(l, which, kind, arg, title));
}

void library_init(library *l, account *a, gs_jobs *jobs) {
    memset(l, 0, sizeof *l);
    l->account = a, l->jobs = jobs;
    l->lock = SDL_CreateMutex();
}

void library_free(library *l) {
    for (int i = 0; i < SHELVES; i++) free(l->shelves[i].items);
    free(l->liked);
    free(l->disliked);
    SDL_DestroyMutex(l->lock);
}

void library_signed_in(library *l) {
    start(l, SHELF_LIKED, LOAD_BROWSE, LIKED_MUSIC, "Liked music");
    start(l, SHELF_PLAYLISTS, LOAD_BROWSE, LIBRARY_PLAYLISTS, "Playlists");
}

void library_open(library *l, int which, const char *id, const char *title) { start(l, which, LOAD_BROWSE, id, title); }

void library_copy(library *l, int which, shelf *out) {
    SDL_LockMutex(l->lock);
    *out = l->shelves[which];
    out->items = malloc(sizeof *out->items * (size_t)(out->cap = out->n ? out->n : 1));
    memcpy(out->items, l->shelves[which].items, sizeof *out->items * (size_t)out->n);
    SDL_UnlockMutex(l->lock);
}

void library_put(library *l, int which, shelf *copy) {
    if (copy->loading) {
        library_open(l, which, copy->source, copy->title);
        free(copy->items);
        return;
    }
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[which];
    free(s->items);
    int gen = s->gen + 1;  // (a load still under way for it drops its pages)
    *s = *copy;
    s->gen = gen;
    SDL_UnlockMutex(l->lock);
}
void library_search(library *l, const char *query, search_kind kind) {
    load *j = prepare(l, SHELF_SEARCH, LOAD_SEARCH, query, query);
    j->search = kind;
    gs_jobs_add(l->jobs, run, j);
}
void library_radio(library *l, const char *id, const char *title) { start(l, SHELF_RADIO, LOAD_RADIO, id, title); }

int library_radio_resume(library *l, const char *id, const char *title, const char *more) {
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[SHELF_RADIO];
    s->n = 0, s->loading = false, s->error[0] = 0;
    SDL_strlcpy(s->source, id, sizeof s->source);
    SDL_strlcpy(s->title, title, sizeof s->title);
    SDL_strlcpy(s->more, more, sizeof s->more);
    int gen = ++s->gen;
    SDL_UnlockMutex(l->lock);
    return gen;
}

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

void library_rate(library *l, const item *song, rating r) {
    load *j = calloc(1, sizeof *j);
    j->l = l, j->kind = LOAD_RATE, j->song = *song, j->rate = r;
    SDL_LockMutex(l->lock);  // shown at once; undone if YouTube refuses
    j->was = library_liked(l, song->id) ? RATE_LIKE : library_disliked(l, song->id) ? RATE_DISLIKE : RATE_NONE;
    bool like = r == RATE_LIKE;
    if (like) liked_add(l, song->id); else liked_remove(l, song->id);
    disliked_set(l, song->id, r == RATE_DISLIKE);
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
