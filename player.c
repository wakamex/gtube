#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gs_mix.h"
#include "gs_opus.h"
#include "gs_webm.h"

#define RATE 48000
#define MAX_TRACKS 2000

// One track playing: yt-dlp writing its WebM to a pipe, a thread decoding it into the stream.
typedef struct {
    player *p;
    SDL_Process *proc;
    SDL_Thread *thread;
    gs_stream *stream;
    SDL_AtomicInt stop, got_audio, failed;
    gs_opus *opus;
    gs_webm *webm;
    bool bad;
    int preskip;
    double duration;
    char url[256];
} playback;

struct player {
    tools *tools;
    gs_jobs *jobs;
    bool audio;
    SDL_Mutex *lock;  // guards the queue and status
    track *queue;
    int n, current;
    int listing;  // links being listed
    char status[256];
    playback *now;
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

static void list_tracks(void *user) {
    list_job *j = user;
    player *p = j->p;
    while (SDL_GetAtomicInt(&p->tools->state) == 0) SDL_Delay(50);  // tools still being prepared
    if (SDL_GetAtomicInt(&p->tools->state) < 0) {
        set_status(p, "%s", p->tools->status);
    } else {
        const char *args[] = { "--flat-playlist", "--print", "%(id)s\t%(title)s\t%(artist,uploader,channel)s\t%(duration)s", j->url, NULL };
        SDL_Process *proc = tools_ytdlp(p->tools, args);
        size_t n = 0;
        int code = -1;
        char *out = proc ? SDL_ReadProcess(proc, &n, &code) : NULL;
        SDL_DestroyProcess(proc);
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
        }
        if (!added) SDL_snprintf(p->status, sizeof p->status, "no tracks found at that link (yt-dlp exit %d)", code);
        else SDL_snprintf(p->status, sizeof p->status, "added %d track%s", added, added == 1 ? "" : "s");
        p->listing--;
        SDL_UnlockMutex(p->lock);
        SDL_free(out);
        free(j);
        return;
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
    if (p->n < MAX_TRACKS) p->queue[p->n++] = *t;
    SDL_UnlockMutex(p->lock);
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

static int SDLCALL play_thread(void *user) {
    playback *pb = user;
    SDL_IOStream *out = pb->proc ? SDL_GetProcessOutput(pb->proc) : NULL;
    gs_webm *w = pb->webm = gs_webm_new(on_frame, pb);
    uint8_t *buf = malloc(65536);
    bool bad = !out;
    while (!bad && !SDL_GetAtomicInt(&pb->stop)) {
        size_t n = SDL_ReadIO(out, buf, 65536);
        if (!n) {
            SDL_IOStatus st = SDL_GetIOStatus(out);
            if (st == SDL_IO_STATUS_NOT_READY) { SDL_Delay(5); continue; }
            break;  // end of the stream, or yt-dlp stopped
        }
        if (!gs_webm_feed(w, buf, n) || pb->bad) bad = true;
    }
    free(buf);
    gs_webm_free(w);
    pb->webm = NULL;
    if (bad || !SDL_GetAtomicInt(&pb->got_audio)) SDL_SetAtomicInt(&pb->failed, 1);
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
    if (pb->proc) SDL_KillProcess(pb->proc, true);
    SDL_WaitThread(pb->thread, NULL);
    SDL_DestroyProcess(pb->proc);
    gs_opus_free(pb->opus);
    gs_stream_free(pb->stream);
    free(pb);
}

void player_play(player *p, int index) {
    stop_playback(p);
    SDL_LockMutex(p->lock);
    if (index < 0 || index >= p->n) { SDL_UnlockMutex(p->lock); return; }
    p->current = index;
    playback *pb = calloc(1, sizeof *pb);
    pb->p = p;
    SDL_snprintf(pb->url, sizeof pb->url, "https://www.youtube.com/watch?v=%s", p->queue[index].id);
    pb->duration = p->queue[index].duration;
    SDL_UnlockMutex(p->lock);
    pb->stream = gs_stream_new(RATE, 8, 0.4);
    // yt-dlp starts here, not on the thread, so stopping can always kill it.
    const char *args[] = { "-f", "251/bestaudio[acodec=opus]", "-o", "-", "--quiet", pb->url, NULL };
    pb->proc = SDL_GetAtomicInt(&p->tools->state) == 1 ? tools_ytdlp(p->tools, args) : NULL;
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
}

void player_update(player *p) {
    SDL_LockMutex(p->lock);
    int n = p->n;
    SDL_UnlockMutex(p->lock);
    if (!p->now && p->current < 0 && n > 0 && SDL_GetAtomicInt(&p->tools->state) == 1) {  // tracks, and yt-dlp ready: start
        player_play(p, 0);
        return;
    }
    if (p->now && gs_stream_finished(p->now->stream)) {
        if (SDL_GetAtomicInt(&p->now->failed)) set_status(p, "could not play %s; skipping", p->queue[p->current].title);
        if (p->current + 1 < n) player_play(p, p->current + 1);
        else stop_playback(p);
    }
}

// ---- Setup and display ----

player *player_new(tools *t, gs_jobs *jobs, bool audio) {
    player *p = calloc(1, sizeof *p);
    p->tools = t, p->jobs = jobs, p->audio = audio;
    p->lock = SDL_CreateMutex();
    p->queue = calloc(MAX_TRACKS, sizeof *p->queue);
    p->current = -1;
    return p;
}

void player_free(player *p) {
    if (!p) return;
    stop_playback(p);
    gs_jobs_wait(p->jobs);
    SDL_DestroyMutex(p->lock);
    free(p->queue);
    free(p);
}

int player_queue(player *p, track *out, int max, int *current) {
    SDL_LockMutex(p->lock);
    int n = p->n < max ? p->n : max;
    memcpy(out, p->queue, sizeof *out * (size_t)n);
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
