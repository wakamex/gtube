#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "gs_mix.h"
#include "gs_http.h"
#include "gs_opus.h"
#include "gs_webm.h"

#define RATE 48000
#define MAX_TRACKS 2000
#define NEED_SIGNIN "YouTube wants a signed-in session: press s to sign in"

// One track playing: its WebM downloaded from a prefetched address (curl), or else written by yt-dlp
// to a pipe, and a thread decoding it into the stream.
typedef struct {
    player *p;
    SDL_Mutex *lock;  // guards proc, which the thread starts and stopping kills
    SDL_Process *proc;
    SDL_Thread *thread;
    gs_stream *stream;
    SDL_AtomicInt stop, got_audio, failed;
    SDL_AtomicInt waiting;  // for its address, which is being fetched
    gs_opus *opus;
    gs_webm *webm;
    bool bad;
    bool started;  // its first audio has played (logged)
    int preskip;
    double duration;
    char id[32], url[256], cookies[1200];
    long long received;  // bytes of WebM taken from the address
    char errors[2048];
    size_t nerrors;
} playback;

struct player {
    tools *tools;
    account *account;
    gs_jobs *jobs;
    bool audio;
    SDL_Mutex *lock;  // guards the queue and status
    track *queue;
    int n, current;
    int listing;  // links being listed
    int version;  // bumped on every change to the queue or the current track (for saving it)
    char status[256];
    playback *now;
    prefetch *prefetch;
    char expected[PREFETCH_WANTS][32];  // the app's own guesses (player_expect)
    int nexpected;
};

// ---- Listing a link ----

typedef struct {
    player *p;
    char url[1024];
} list_job;

static void set_status(player *p, const char *fmt, const char *arg) {
    SDL_LockMutex(p->lock);
    SDL_snprintf(p->status, sizeof p->status, fmt, arg);
    SDL_UnlockMutex(p->lock);
}

// Runs yt-dlp to completion, collecting its output and error output (read together, so neither
// pipe fills and stalls it). Returns the output, malloc'd, or NULL.
static char *collect(player *p, const char *const *args, const char *cookies, char *errors, size_t esize, int *code) {
    SDL_Process *proc = tools_ytdlp(p->tools, args, cookies);
    errors[0] = 0;
    *code = -1;
    if (!proc) return NULL;
    SDL_IOStream *out = SDL_GetProcessOutput(proc);
    size_t n = 0, cap = 65536, used = 0;
    char *data = malloc(cap);
    for (;;) {
        if (n + 4096 > cap) data = realloc(data, cap *= 2);
        size_t got = SDL_ReadIO(out, data + n, cap - n - 1);
        used = tools_errors(proc, errors, esize, used);
        if (got) { n += got; continue; }
        if (SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY) break;
        SDL_Delay(5);
    }
    data[n] = 0;
    SDL_WaitProcess(proc, true, code);
    tools_errors(proc, errors, esize, used);
    SDL_DestroyProcess(proc);
    return data;
}

// Runs yt-dlp signed in when there is a session, anonymously otherwise.
static char *collect_signed(player *p, const char *const *args, char *errors, size_t esize, int *code) {
    char jar[1200];
    bool signed_in = account_jar_file(p->account, jar, sizeof jar);
    char *out = collect(p, args, signed_in ? jar : NULL, errors, esize, code);
    if (signed_in) account_jar_done(jar);
    return out;
}

static void list_tracks(void *user) {
    list_job *j = user;
    player *p = j->p;
    while (SDL_GetAtomicInt(&p->tools->state) == 0) SDL_Delay(50);  // tools still being prepared
    if (SDL_GetAtomicInt(&p->tools->state) < 0) {
        set_status(p, "%s", p->tools->status);
    } else {
        const char *args[] = { "--flat-playlist", "--print", "%(id)s\t%(title)s\t%(artist,uploader,channel)s\t%(duration)s", j->url, NULL };
        char errors[2048];
        int code;
        char *out = collect_signed(p, args, errors, sizeof errors, &code);
        if (errors[0]) SDL_Log("yt-dlp: %s", errors);
        int added = 0;
        SDL_LockMutex(p->lock);
        for (char *line = out ? strtok(out, "\n") : NULL; line; line = strtok(NULL, "\n")) {
            char *f[4] = { line, NULL, NULL, NULL };
            for (int k = 1; k < 4; k++) f[k] = f[k - 1] ? strchr(f[k - 1], '\t') : NULL, f[k] ? (*f[k]++ = 0) : 0;
            if (!f[1] || strlen(f[0]) >= sizeof p->queue[0].id || p->n >= MAX_TRACKS) continue;
            track *t = NULL;
            for (int k = 0; k < p->n && !t; k++)  // a track already queued from its link, waiting for its title
                if (!p->queue[k].title[0] && !strcmp(p->queue[k].id, f[0])) t = &p->queue[k];
            if (!t) t = &p->queue[p->n++];
            memset(t, 0, sizeof *t);
            SDL_strlcpy(t->id, f[0], sizeof t->id);
            SDL_strlcpy(t->title, f[1], sizeof t->title);
            SDL_strlcpy(t->artist, f[2] && strcmp(f[2], "NA") ? f[2] : "", sizeof t->artist);
            t->duration = f[3] && strcmp(f[3], "NA") ? SDL_atof(f[3]) : 0;
            added++;
            p->version++;
        }
        if (added) SDL_snprintf(p->status, sizeof p->status, "added %d track%s", added, added == 1 ? "" : "s");
        else if (account_needed(errors)) SDL_strlcpy(p->status, NEED_SIGNIN, sizeof p->status);
        else SDL_snprintf(p->status, sizeof p->status, "no tracks found at that link (yt-dlp exit %d)", code);
        SDL_UnlockMutex(p->lock);
        SDL_free(out);
    }
    SDL_LockMutex(p->lock);
    p->listing--;
    SDL_UnlockMutex(p->lock);
    free(j);
}

// The video a link names directly (watch?v=ID or youtu.be/ID), if any.
static bool video_id(const char *url, char id[32]) {
    const char *v = strstr(url, "v=");
    if (v && (v == url || v[-1] == '?' || v[-1] == '&')) v += 2;
    else if ((v = strstr(url, "youtu.be/"))) v += 9;
    else return false;
    int n = 0;
    while (n < 31 && (SDL_isalnum((unsigned char)v[n]) || v[n] == '_' || v[n] == '-')) n++;
    if (n != 11) return false;
    memcpy(id, v, 11), id[11] = 0;
    return true;
}

void player_add(player *p, const char *url) {
    // A link to one video is queued at once, so it can start streaming while the link is listed
    // (each yt-dlp run spends seconds extracting from YouTube; this overlaps the two).
    char id[32];
    if (video_id(url, id)) {
        SDL_LockMutex(p->lock);
        if (p->n < MAX_TRACKS) {
            memset(&p->queue[p->n], 0, sizeof p->queue[0]);
            SDL_strlcpy(p->queue[p->n++].id, id, sizeof id);
            p->version++;
        }
        SDL_UnlockMutex(p->lock);
    }
    list_job *j = malloc(sizeof *j);
    j->p = p;
    SDL_strlcpy(j->url, url, sizeof j->url);
    SDL_LockMutex(p->lock);
    p->listing++;
    SDL_snprintf(p->status, sizeof p->status, "listing %.200s", url);
    SDL_UnlockMutex(p->lock);
    gs_jobs_add(p->jobs, list_tracks, j);
}

void player_add_track(player *p, const track *t) {
    SDL_LockMutex(p->lock);
    if (p->n < MAX_TRACKS) p->queue[p->n++] = *t, p->version++;
    SDL_UnlockMutex(p->lock);
}

void player_set_queue(player *p, const track *tracks, int n, int start) {
    if (n > MAX_TRACKS) n = MAX_TRACKS;
    if (start < 0 || start >= n) return;
    SDL_LockMutex(p->lock);
    bool same = p->now && p->current >= 0 && !strcmp(p->queue[p->current].id, tracks[start].id);
    memcpy(p->queue, tracks, sizeof *tracks * (size_t)n);
    p->n = n;
    p->version++;
    if (same) p->current = start;
    SDL_UnlockMutex(p->lock);
    if (!same) player_play(p, start);
}

// ---- Playing a track ----

static void on_frame(void *user, const uint8_t *data, size_t len, double seconds) {
    playback *pb = user;
    (void)seconds;
    if (SDL_GetAtomicInt(&pb->stop) || pb->bad) return;
    if (!pb->opus) {  // the first frame: the track headers have been read, set up the decoder
        size_t hl;
        const uint8_t *head = gs_webm_codec_private(pb->webm, &hl);
        int channels = 2;
        if (!gs_webm_codec(pb->webm) || strcmp(gs_webm_codec(pb->webm), "A_OPUS") || !gs_opus_head(head, hl, &channels, &pb->preskip)) {
            pb->bad = true;
            return;
        }
        pb->opus = gs_opus_new(channels);
        if (gs_webm_duration(pb->webm) > 0) pb->duration = gs_webm_duration(pb->webm);
    }
    float lr[5760 * 2];
    int frames = gs_opus_decode(pb->opus, data, len, lr, 5760);
    if (frames <= 0) return;
    int skip = pb->preskip < frames ? pb->preskip : frames;  // the encoder's priming samples
    pb->preskip -= skip;
    SDL_SetAtomicInt(&pb->got_audio, 1);
    if (!gs_stream_write(pb->stream, lr + 2 * skip, frames - skip)) SDL_SetAtomicInt(&pb->stop, 1);
}

static bool feed(void *user, const uint8_t *data, size_t len) {
    playback *pb = user;
    pb->received += (long long)len;
    return gs_webm_feed(pb->webm, data, len) && !pb->bad && !SDL_GetAtomicInt(&pb->stop);
}

// The address's whole length, which YouTube puts in it ("clen"), or 0.
static long long address_length(const char *url) {
    const char *c = strstr(url, "clen=");
    return c && (c[-1] == '?' || c[-1] == '&') ? SDL_strtoll(c + 5, NULL, 10) : 0;
}

// Plays the track from its address. A connection that breaks picks up where it left off, on a new
// address if that one has expired (a track paused for hours). False if no audio came at all.
static bool play_address(playback *pb, char *url, size_t size) {
    gs_http_request r = { .url = url, .agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36" };
    long long total = address_length(url);
    for (int tries = 0; tries < 3 && !SDL_GetAtomicInt(&pb->stop) && !pb->bad;) {
        long long before = pb->received;
        int status = gs_http_stream(&r, pb->received, feed, pb, &pb->stop);
        if (SDL_GetAtomicInt(&pb->stop) || pb->bad || (total && pb->received >= total)) break;
        if (status >= 200 && status < 300 && !total) break;  // the end, as far as can be told
        if (!SDL_GetAtomicInt(&pb->got_audio)) return false;
        if (pb->received > before) tries = 0; else tries++;
        if (status < 200 || status >= 300) {  // expired, most likely: a new address for the same track
            SDL_Log("player: address answered %d at byte %lld; fetching a new one", status, pb->received);
            if (!prefetch_fetch(pb->p->prefetch, pb->id, url, size, &pb->stop)) break;
            r.url = url;
        }
    }
    return SDL_GetAtomicInt(&pb->got_audio);
}

static void play_pipe(playback *pb) {
    // The process is started under the lock, so stopping either sees it and kills it, or stops it starting.
    const char *args[] = { "-f", "251/bestaudio[acodec=opus]", "-o", "-", "--quiet", pb->url, NULL };
    SDL_LockMutex(pb->lock);
    if (!SDL_GetAtomicInt(&pb->stop)) {
        bool signed_in = account_jar_file(pb->p->account, pb->cookies, sizeof pb->cookies);
        if (!signed_in) pb->cookies[0] = 0;
        pb->proc = SDL_GetAtomicInt(&pb->p->tools->state) == 1 ? tools_ytdlp(pb->p->tools, args, signed_in ? pb->cookies : NULL) : NULL;
    }
    SDL_UnlockMutex(pb->lock);
    SDL_IOStream *out = pb->proc ? SDL_GetProcessOutput(pb->proc) : NULL;
    uint8_t *buf = malloc(65536);
    bool bad = !out;
    while (!bad && !SDL_GetAtomicInt(&pb->stop)) {
        size_t n = SDL_ReadIO(out, buf, 65536);
        pb->nerrors = tools_errors(pb->proc, pb->errors, sizeof pb->errors, pb->nerrors);
        if (!n) {
            SDL_IOStatus st = SDL_GetIOStatus(out);
            if (st == SDL_IO_STATUS_NOT_READY) { SDL_Delay(5); continue; }
            break;  // the end of the stream, or yt-dlp stopped
        }
        if (!gs_webm_feed(pb->webm, buf, n) || pb->bad) bad = true;
    }
    free(buf);
    if (pb->proc && !SDL_GetAtomicInt(&pb->stop)) SDL_WaitProcess(pb->proc, true, NULL);
    pb->nerrors = tools_errors(pb->proc, pb->errors, sizeof pb->errors, pb->nerrors);
    if (pb->errors[0] && !SDL_GetAtomicInt(&pb->stop)) SDL_Log("yt-dlp: %s", pb->errors);
    if (bad) pb->bad = true;
}

static int SDLCALL play_thread(void *user) {
    playback *pb = user;
    prefetch *pf = pb->p->prefetch;
    pb->webm = gs_webm_new(on_frame, pb);
    while (SDL_GetAtomicInt(&pb->waiting) && prefetch_busy(pf, pb->id) && !SDL_GetAtomicInt(&pb->stop)) SDL_Delay(20);
    SDL_SetAtomicInt(&pb->waiting, 0);
    char *url = malloc(8192);
    bool played = false;
    if (prefetch_address(pf, pb->id, url, 8192)) {
        played = play_address(pb, url, 8192);
        if (!played && !SDL_GetAtomicInt(&pb->stop)) {
            SDL_Log("player: the prefetched address did not play; asking yt-dlp");
            prefetch_forget(pf, pb->id);
            gs_webm_free(pb->webm);
            gs_opus_free(pb->opus);
            pb->webm = gs_webm_new(on_frame, pb), pb->opus = NULL, pb->bad = false, pb->received = 0;
        }
    }
    free(url);
    if (!played && !SDL_GetAtomicInt(&pb->stop)) play_pipe(pb);
    gs_webm_free(pb->webm);
    pb->webm = NULL;
    if (pb->bad || !SDL_GetAtomicInt(&pb->got_audio)) SDL_SetAtomicInt(&pb->failed, 1);
    gs_stream_end(pb->stream);
    return 0;
}

static void stop_playback(player *p) {
    playback *pb = p->now;
    if (!pb) return;
    p->now = NULL;
    if (p->audio) gs_mix_remove(gs_stream_render, pb->stream);
    SDL_SetAtomicInt(&pb->stop, 1);
    gs_stream_stop(pb->stream);
    SDL_LockMutex(pb->lock);
    if (pb->proc) SDL_KillProcess(pb->proc, true);
    SDL_UnlockMutex(pb->lock);
    SDL_WaitThread(pb->thread, NULL);
    SDL_DestroyProcess(pb->proc);
    SDL_DestroyMutex(pb->lock);
    if (pb->cookies[0]) account_jar_done(pb->cookies);
    gs_opus_free(pb->opus);
    gs_stream_free(pb->stream);
    free(pb);
}

void player_play(player *p, int index) {
    stop_playback(p);
    SDL_LockMutex(p->lock);
    if (index < 0 || index >= p->n) { SDL_UnlockMutex(p->lock); return; }
    p->current = index;
    p->version++;
    playback *pb = calloc(1, sizeof *pb);
    pb->p = p;
    SDL_strlcpy(pb->id, p->queue[index].id, sizeof pb->id);
    SDL_snprintf(pb->url, sizeof pb->url, "https://www.youtube.com/watch?v=%s", pb->id);
    pb->duration = p->queue[index].duration;
    SDL_UnlockMutex(p->lock);
    pb->stream = gs_stream_new(RATE, 8, 0.4);
    pb->lock = SDL_CreateMutex();
    // An address already being fetched is waited for, and the track stays wanted meanwhile; any
    // other track plays through yt-dlp at once.
    SDL_SetAtomicInt(&pb->waiting, prefetch_busy(p->prefetch, pb->id));
    pb->thread = SDL_CreateThread(play_thread, "gtube play", pb);
    p->now = pb;
    if (p->audio) gs_mix_add(gs_stream_render, pb->stream);
}

void player_next(player *p) {
    if (p->current + 1 < p->n) player_play(p, p->current + 1);
}

void player_previous(player *p) {
    if (p->now && gs_stream_position(p->now->stream) > 3) player_play(p, p->current);
    else if (p->current > 0) player_play(p, p->current - 1);
}

void player_toggle_pause(player *p) {
    if (p->now) gs_stream_pause(p->now->stream, !gs_stream_paused(p->now->stream));
    else if (p->current >= 0) player_play(p, p->current);  // a restored queue, or one that ran out
}

void player_load(player *p, const track *tracks, int n, int current) {
    if (n > MAX_TRACKS) n = MAX_TRACKS;
    SDL_LockMutex(p->lock);
    memcpy(p->queue, tracks, sizeof *tracks * (size_t)n);
    p->n = n;
    p->current = current >= 0 && current < n ? current : -1;
    p->version++;
    SDL_UnlockMutex(p->lock);
}

int player_version(player *p) {
    SDL_LockMutex(p->lock);
    int v = p->version;
    SDL_UnlockMutex(p->lock);
    return v;
}

// The songs to have addresses for: the current track while it waits to be played (a restored queue)
// or waits for its address, the one after it, then the app's guesses.
static void expect(player *p) {
    const char *ids[2 + PREFETCH_WANTS];
    int n = 0;
    SDL_LockMutex(p->lock);
    bool waiting = p->now && SDL_GetAtomicInt(&p->now->waiting);
    if ((!p->now || waiting) && p->current >= 0 && p->current < p->n) ids[n++] = p->queue[p->current].id;
    int next = p->current + 1;
    if (next < p->n) ids[n++] = p->queue[next].id;
    for (int i = 0; i < p->nexpected; i++)  // (not the track playing, which has its audio)
        if (!p->now || p->current < 0 || strcmp(p->expected[i], p->queue[p->current].id)) ids[n++] = p->expected[i];
    prefetch_want(p->prefetch, ids, n);  // (copies the ids)
    SDL_UnlockMutex(p->lock);
}

void player_expect(player *p, const char *const *ids, int n) {
    SDL_LockMutex(p->lock);
    p->nexpected = 0;
    for (int i = 0; i < n && p->nexpected < PREFETCH_WANTS; i++) SDL_strlcpy(p->expected[p->nexpected++], ids[i], sizeof p->expected[0]);
    SDL_UnlockMutex(p->lock);
}

bool player_track_id(player *p, int index, char *id, size_t size) {
    SDL_LockMutex(p->lock);
    bool ok = index >= 0 && index < p->n;
    if (ok) SDL_strlcpy(id, p->queue[index].id, size);
    SDL_UnlockMutex(p->lock);
    return ok;
}

void player_update(player *p) {
    expect(p);
    SDL_LockMutex(p->lock);
    int n = p->n;
    SDL_UnlockMutex(p->lock);
    if (!p->now && p->current < 0 && n > 0 && SDL_GetAtomicInt(&p->tools->state) == 1) {  // tracks, and yt-dlp ready: start
        player_play(p, 0);
        return;
    }
    if (p->now && !p->now->started && gs_stream_position(p->now->stream) > 0) {
        p->now->started = true;
        SDL_LockMutex(p->lock);
        if (!strcmp(p->status, NEED_SIGNIN)) p->status[0] = 0;  // signing in worked
        SDL_Log("player: playing \"%s\", at %.3f s", p->current >= 0 ? p->queue[p->current].title : "", gs_stream_position(p->now->stream));
        SDL_UnlockMutex(p->lock);
    }
    if (p->now && gs_stream_finished(p->now->stream)) {
        playback *pb = p->now;
        if (SDL_GetAtomicInt(&pb->failed) && account_needed(pb->errors) && !account_signed_in(p->account)) {
            set_status(p, "%s", NEED_SIGNIN);
            stop_playback(p);  // signing in plays it again
            return;
        }
        if (SDL_GetAtomicInt(&pb->failed)) set_status(p, "could not play %s; skipping", p->queue[p->current].title);
        if (p->current + 1 < n) player_play(p, p->current + 1);
        else stop_playback(p);
    }
}

// ---- Setup and display ----

player *player_new(tools *t, account *a, gs_jobs *jobs, bool audio) {
    player *p = calloc(1, sizeof *p);
    p->tools = t, p->account = a, p->jobs = jobs, p->audio = audio;
    p->lock = SDL_CreateMutex();
    p->queue = calloc(MAX_TRACKS, sizeof *p->queue);
    p->current = -1;
    p->prefetch = prefetch_new(t, a);
    return p;
}

void player_free(player *p) {
    if (!p) return;
    stop_playback(p);
    prefetch_free(p->prefetch);
    gs_jobs_wait(p->jobs);
    SDL_DestroyMutex(p->lock);
    free(p->queue);
    free(p);
}

int player_queue(player *p, track *out, int max, int *current) {
    SDL_LockMutex(p->lock);
    int n = p->n < max ? p->n : max;
    if (n > 0) memcpy(out, p->queue, sizeof *out * (size_t)n);
    *current = p->current;
    SDL_UnlockMutex(p->lock);
    return n;
}

double player_position(player *p) { return p->now ? gs_stream_position(p->now->stream) : 0; }
double player_duration(player *p) { return p->now ? p->now->duration : 0; }
bool player_paused(player *p) { return p->now && gs_stream_paused(p->now->stream); }

bool player_loading(player *p) {
    SDL_LockMutex(p->lock);
    bool listing = p->listing > 0;
    SDL_UnlockMutex(p->lock);
    return listing || (p->now && !SDL_GetAtomicInt(&p->now->got_audio));
}

void player_status(player *p, char *out, size_t size) {
    SDL_LockMutex(p->lock);
    SDL_strlcpy(out, p->status, size);
    SDL_UnlockMutex(p->lock);
}

gs_stream *player_stream(player *p) { return p->now ? p->now->stream : NULL; }
