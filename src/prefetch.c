#include "prefetch.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 32
#define BATCH 3            // songs per yt-dlp run
#define RETRY_SECONDS 120  // after yt-dlp found no address for a song
#define KEEP_SECONDS 3600  // an address with less left than this is fetched again

typedef char video_id[16];

struct prefetch {
    tools *tools;
    account *account;
    SDL_Mutex *lock;  // guards everything below
    SDL_Condition *wake;
    SDL_Thread *thread;
    bool quit;
    video_id wants[PREFETCH_WANTS];
    int nwants;
    struct { video_id id; char *url; double expire; } cache[CACHE];
    struct { video_id id; double until; } failed[CACHE];
    video_id busy[BATCH];
    int nbusy;
};

static double now(void) {
    SDL_Time t;
    return SDL_GetCurrentTime(&t) ? t / 1e9 : 0;
}

static bool is_fresh(prefetch *pf, const char *id) {
    for (int i = 0; i < CACHE; i++)
        if (pf->cache[i].url && !strcmp(pf->cache[i].id, id)) return pf->cache[i].expire - now() > KEEP_SECONDS;
    return false;
}

static bool has_failed(prefetch *pf, const char *id) {
    for (int i = 0; i < CACHE; i++)
        if (!strcmp(pf->failed[i].id, id)) return pf->failed[i].until > now();
    return false;
}

// The wanted songs that need an address, in order (up to max).
static int missing(prefetch *pf, video_id *out, int max) {
    int n = 0;
    for (int i = 0; i < pf->nwants && n < max; i++)
        if (!is_fresh(pf, pf->wants[i]) && !has_failed(pf, pf->wants[i])) SDL_strlcpy(out[n++], pf->wants[i], sizeof *out);
    return n;
}

static void keep(prefetch *pf, const char *id, const char *url) {
    const char *e = strstr(url, "expire=");  // YouTube's addresses carry their expiry, in Unix seconds
    double expire = e && (e[-1] == '?' || e[-1] == '&') ? SDL_atof(e + 7) : 0;
    if (expire <= 0) expire = now() + 6 * 3600;
    int slot = 0;
    for (int i = 0; i < CACHE; i++) {
        if (!strcmp(pf->cache[i].id, id) || !pf->cache[i].url) { slot = i; break; }
        if (pf->cache[i].expire < pf->cache[slot].expire) slot = i;  // else the oldest
    }
    free(pf->cache[slot].url);
    SDL_strlcpy(pf->cache[slot].id, id, sizeof pf->cache[slot].id);
    pf->cache[slot].url = SDL_strdup(url);
    pf->cache[slot].expire = expire;
}

static void mark_failed(prefetch *pf, const char *id) {
    int slot = 0;
    for (int i = 0; i < CACHE; i++) {
        if (!strcmp(pf->failed[i].id, id) || !pf->failed[i].id[0]) { slot = i; break; }
        if (pf->failed[i].until < pf->failed[slot].until) slot = i;
    }
    SDL_strlcpy(pf->failed[slot].id, id, sizeof pf->failed[slot].id);
    pf->failed[slot].until = now() + RETRY_SECONDS;
}

// One yt-dlp run for `n` songs, each address kept as yt-dlp prints it. In the background, the run is
// stopped once the song it is working on is no longer the first one wanted; otherwise when *cancel
// is set. Returns how many it found.
static int resolve(prefetch *pf, video_id *ids, int n, bool background, SDL_AtomicInt *cancel) {
    char urls[BATCH][64], jar[1200];
    const char *args[8 + BATCH];
    int k = 0;
    args[k++] = "-f", args[k++] = "251/bestaudio[acodec=opus]", args[k++] = "--simulate";
    args[k++] = "--print", args[k++] = "%(id)s %(url)s";
    for (int i = 0; i < n; i++) snprintf(urls[i], sizeof urls[i], "https://www.youtube.com/watch?v=%s", ids[i]), args[k++] = urls[i];
    args[k] = NULL;
    bool signed_in = account_jar_file(pf->account, jar, sizeof jar);
    SDL_Process *proc = tools_ytdlp(pf->tools, args, signed_in ? jar : NULL);
    SDL_IOStream *out = proc ? SDL_GetProcessOutput(proc) : NULL;
    char line[8192], errors[1024] = "";
    size_t len = 0, nerr = 0;
    if (!proc) SDL_snprintf(errors, sizeof errors, "could not start yt-dlp (%s)", SDL_GetError());
    bool found[BATCH] = { 0 };
    int got = 0;
    while (out) {
        char c;
        size_t r = SDL_ReadIO(out, &c, 1);
        nerr = tools_errors(proc, errors, sizeof errors, nerr);
        if (r && c != '\n') {
            if (len < sizeof line - 1) line[len++] = c;
            continue;
        }
        if (r) {  // a line: "<id> <address>"
            line[len] = 0, len = 0;
            char *sp = strchr(line, ' ');
            if (!sp || strncmp(sp + 1, "https://", 8)) continue;
            *sp = 0;
            for (int i = 0; i < n; i++)
                if (!found[i] && !strcmp(ids[i], line)) {
                    SDL_LockMutex(pf->lock);
                    keep(pf, ids[i], sp + 1);
                    for (int b = 0; b < pf->nbusy; b++)  // (no longer busy, though the run goes on)
                        if (!strcmp(pf->busy[b], ids[i])) memmove(pf->busy[b], pf->busy[b + 1], sizeof *pf->busy * (size_t)(--pf->nbusy - b)), b--;
                    SDL_UnlockMutex(pf->lock);
                    found[i] = true, got++;
                }
            continue;
        }
        if (SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY || got == n) break;
        if (background) {
            SDL_LockMutex(pf->lock);
            video_id first;
            int at = 0;
            while (found[at]) at++;
            bool stop = pf->quit || missing(pf, &first, 1) == 0 || strcmp(first, ids[at]);
            SDL_UnlockMutex(pf->lock);
            if (stop) break;
        } else if (SDL_GetAtomicInt(cancel)) break;
        SDL_Delay(5);
    }
    if (proc) SDL_KillProcess(proc, true), SDL_WaitProcess(proc, true, NULL);
    SDL_DestroyProcess(proc);
    if (signed_in) account_jar_done(jar);
    if (got < n && errors[0]) SDL_Log("prefetch: yt-dlp: %s", errors);
    return got;
}

static int SDLCALL work(void *user) {
    prefetch *pf = user;
    SDL_LockMutex(pf->lock);
    while (!pf->quit) {
        video_id batch[BATCH];
        int n = SDL_GetAtomicInt(&pf->tools->state) == 1 ? missing(pf, batch, BATCH) : 0;
        if (!n) {
            SDL_WaitConditionTimeout(pf->wake, pf->lock, 1000);  // (failures expire and tools get ready unannounced)
            continue;
        }
        memcpy(pf->busy, batch, sizeof batch);
        pf->nbusy = n;
        SDL_UnlockMutex(pf->lock);
        Uint64 start = SDL_GetTicks();
        int got = resolve(pf, batch, n, true, NULL);
        SDL_Log("prefetch: %d of %d address%s in %.1f s", got, n, n == 1 ? "" : "es", (SDL_GetTicks() - start) / 1000.0);
        SDL_LockMutex(pf->lock);
        pf->nbusy = 0;
        // A song it found no address for, while still wanted, is not tried again for a while.
        video_id first;
        for (int i = 0; i < n; i++)
            if (!is_fresh(pf, batch[i]) && missing(pf, &first, 1) && !strcmp(first, batch[i])) mark_failed(pf, batch[i]);
    }
    SDL_UnlockMutex(pf->lock);
    return 0;
}

prefetch *prefetch_new(tools *t, account *a) {
    prefetch *pf = calloc(1, sizeof *pf);
    pf->tools = t, pf->account = a;
    pf->lock = SDL_CreateMutex();
    pf->wake = SDL_CreateCondition();
    pf->thread = SDL_CreateThread(work, "gtube prefetch", pf);
    return pf;
}

void prefetch_free(prefetch *pf) {
    if (!pf) return;
    SDL_LockMutex(pf->lock);
    pf->quit = true;
    SDL_SignalCondition(pf->wake);
    SDL_UnlockMutex(pf->lock);
    SDL_WaitThread(pf->thread, NULL);
    for (int i = 0; i < CACHE; i++) free(pf->cache[i].url);
    SDL_DestroyCondition(pf->wake);
    SDL_DestroyMutex(pf->lock);
    free(pf);
}

void prefetch_want(prefetch *pf, const char *const *ids, int n) {
    video_id wants[PREFETCH_WANTS];
    int m = 0;
    for (int i = 0; i < n && m < PREFETCH_WANTS; i++) {
        bool seen = !ids[i] || !ids[i][0] || strlen(ids[i]) >= sizeof *wants;
        for (int k = 0; k < m && !seen; k++) seen = !strcmp(wants[k], ids[i]);
        if (!seen) SDL_strlcpy(wants[m++], ids[i], sizeof *wants);
    }
    SDL_LockMutex(pf->lock);
    if (m != pf->nwants || memcmp(wants, pf->wants, sizeof *wants * (size_t)m)) {
        memcpy(pf->wants, wants, sizeof *wants * (size_t)m);
        pf->nwants = m;
        SDL_SignalCondition(pf->wake);
    }
    SDL_UnlockMutex(pf->lock);
}

bool prefetch_address(prefetch *pf, const char *id, char *url, size_t size) {
    bool ok = false;
    SDL_LockMutex(pf->lock);
    if (is_fresh(pf, id))
        for (int i = 0; i < CACHE; i++)
            if (pf->cache[i].url && !strcmp(pf->cache[i].id, id)) ok = SDL_strlcpy(url, pf->cache[i].url, size) < size;
    SDL_UnlockMutex(pf->lock);
    return ok;
}

bool prefetch_busy(prefetch *pf, const char *id) {
    SDL_LockMutex(pf->lock);
    bool busy = false;
    for (int i = 0; i < pf->nbusy; i++) busy |= !strcmp(pf->busy[i], id);
    SDL_UnlockMutex(pf->lock);
    return busy;
}

void prefetch_forget(prefetch *pf, const char *id) {
    SDL_LockMutex(pf->lock);
    for (int i = 0; i < CACHE; i++)
        if (pf->cache[i].url && !strcmp(pf->cache[i].id, id)) free(pf->cache[i].url), pf->cache[i].url = NULL, pf->cache[i].id[0] = 0;
    SDL_UnlockMutex(pf->lock);
}

bool prefetch_fetch(prefetch *pf, const char *id, char *url, size_t size, SDL_AtomicInt *cancel) {
    video_id one;
    if (strlen(id) >= sizeof one) return false;
    SDL_strlcpy(one, id, sizeof one);
    prefetch_forget(pf, id);
    return resolve(pf, &one, 1, false, cancel) && prefetch_address(pf, id, url, size);
}
