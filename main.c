// A native YouTube Music mini player on gesso: paste or pass a link (a track, an album, a playlist)
// and it plays, streaming the audio with its own yt-dlp.
//   gtube [URL...]                             play
//   gtube --wav F.wav --seconds S URL          render the first track offline (a test of decoding)
//   gtube --tools [--probe URL]                just get or update yt-dlp and a JavaScript runtime
//   --data DIR                                  where tools live (default: the user's app data folder)
//   --demo                                      a queue of sample titles in many scripts, nothing played
//   --shot F.png [--at S]                       render one frame at S seconds, headless, and quit
// Keys: Ctrl+V pastes a link, Space pauses, Left/Right previous/next, Up/Down and Enter or a click
// pick a track, -/+ volume, F1 performance overlay, Esc quits.
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gs_jobs.h"
#include "gs_mix.h"
#include "gs_pace.h"
#include "gs_stats.h"
#include "gs_text.h"
#include "stb_image_write.h"
#include "player.h"
#include "tools.h"

#define RATE 48000

typedef struct {
    tools tools;
    gs_jobs *jobs;
    player *player;
    SDL_Window *win;
    SDL_Renderer *ren;
    gs_glyphs *glyphs;
    gs_fontset *fonts;
    gs_pace pace;
    gs_stats stats;
    bool show_stats, audio, demo;
    const char *shot;
    double shot_at;
    uint64_t started;
    float volume;
    int selected, scroll;
    SDL_FRect rows[64];
    int row_track[64], nrows;
} app;

static void data_dir(const char *data, char *out, size_t size) {
#ifdef _WIN32
    const char *slash = "\\";
#else
    const char *slash = "/";
#endif
    if (data) {
        size_t n = strlen(data);
        snprintf(out, size, "%s%s", data, n && (data[n - 1] == '/' || data[n - 1] == '\\') ? "" : slash);
    } else {
        char *pref = SDL_GetPrefPath("wakamex", "gesso-gtube");
        snprintf(out, size, "%s", pref ? pref : "");
        SDL_free(pref);
    }
}

static bool write_wav(const char *path, const float *lr, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, rate = RATE, bytes = RATE * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bytes, 4, 1, f);
    fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    for (int i = 0; i < 2 * frames; i++) {
        float x = lr[i] < -1 ? -1 : lr[i] > 1 ? 1 : lr[i];
        int16_t s = (int16_t)lrintf(x * 32767);
        fwrite(&s, 2, 1, f);
    }
    return fclose(f) == 0;
}

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
    app *a = SDL_calloc(1, sizeof *a);
    *state = a;
    const char *data = NULL, *probe = NULL, *wav = NULL, *urls[16];
    int nurls = 0;
    double seconds = 30;
    bool tools_only = false, demo = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) probe = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = SDL_atof(argv[++i]);
        else if (!strcmp(argv[i], "--tools")) tools_only = true;
        else if (!strcmp(argv[i], "--demo")) demo = a->demo = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) a->shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) a->shot_at = SDL_atof(argv[++i]);
        else if (nurls < 16) urls[nurls++] = argv[i];
    }
    char dir[1024];
    data_dir(data, dir, sizeof dir);
    tools_init(&a->tools, dir);
    a->jobs = gs_jobs_new(2);

    if (tools_only) {
        uint64_t start = SDL_GetTicks();
        tools_prepare(&a->tools);
        printf("state %d (%s) in %.1f s\nyt-dlp %s\njs %s\n", SDL_GetAtomicInt(&a->tools.state), a->tools.status,
               (SDL_GetTicks() - start) / 1000.0, a->tools.ytdlp, a->tools.js);
        if (SDL_GetAtomicInt(&a->tools.state) != 1) return SDL_APP_FAILURE;
        if (probe) {
            const char *args[] = { "-f", "251", "--print", "%(title)s | %(artist,uploader)s | format %(format_id)s %(ext)s %(acodec)s %(abr)s kbit/s | %(duration)s s", probe, NULL };
            SDL_Process *p = tools_ytdlp(&a->tools, args);
            size_t n;
            int code;
            char *out = p ? SDL_ReadProcess(p, &n, &code) : NULL;
            printf("probe (exit %d): %s", p ? code : -1, out ? out : "\n");
            SDL_free(out);
            SDL_DestroyProcess(p);
        }
        return SDL_APP_SUCCESS;
    }

    gs_jobs_add(a->jobs, tools_prepare, &a->tools);
    if (wav) {  // offline: the first track, as fast as it arrives
        a->player = player_new(&a->tools, a->jobs, false);
        if (nurls) player_add(a->player, urls[0]);
        int frames = (int)(seconds * RATE), done = 0;
        float *lr = SDL_calloc((size_t)frames * 2, sizeof *lr);
        uint64_t start = SDL_GetTicks(), first = 0;
        while (done < frames && SDL_GetTicks() - start < 120000) {
            player_update(a->player);
            gs_stream *s = player_stream(a->player);
            if (!s || (gs_stream_buffered(s) < 0.5 && !gs_stream_finished(s))) { SDL_Delay(5); continue; }
            if (!first) first = SDL_GetTicks();
            int k = frames - done < 4800 ? frames - done : 4800;
            gs_stream_render(s, lr + 2 * done, k);
            done += k;
            if (gs_stream_finished(s)) break;
        }
        char status[256];
        player_status(a->player, status, sizeof status);
        printf("%s; first audio after %.1f s; %.1f s rendered\n", status, first ? (first - start) / 1000.0 : -1.0, done / (double)RATE);
        bool ok = done > 0 && write_wav(wav, lr, done);
        SDL_free(lr);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (a->shot) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"), SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO | (a->shot ? 0 : SDL_INIT_AUDIO))) return SDL_Log("SDL_Init: %s", SDL_GetError()), SDL_APP_FAILURE;
    if (!SDL_CreateWindowAndRenderer("gesso gtube", 720, 560, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &a->win, &a->ren))
        return SDL_Log("window: %s", SDL_GetError()), SDL_APP_FAILURE;
    gs_pace_set(&a->pace, a->win, a->ren, true, 30);
    a->audio = !a->shot && gs_mix_open(RATE);
    a->started = SDL_GetTicks();
    a->volume = 1;
    a->player = player_new(&a->tools, a->jobs, a->audio);
    a->glyphs = gs_glyphs_new(a->ren, 1024);
    a->fonts = gs_fontset_system();
    for (int i = 0; i < nurls; i++) player_add(a->player, urls[i]);
    if (demo) {
        static const track samples[] = {
            { "d1", "\xE3\x82\xA2\xE3\x82\xA4\xE3\x83\x89\xE3\x83\xAB", "YOASOBI", 213 },                                 // アイドル
            { "d2", "\xEB\xB4\x84\xEB\xB0\xA4 (Spring Day)", "BTS", 274 },
            { "d3", "\xE6\x9C\x88\xE4\xBA\xAE\xE4\xBB\xA3\xE8\xA1\xA8\xE6\x88\x91\xE7\x9A\x84\xE5\xBF\x83", "\xE9\x84\xA7\xE9\xBA\x97\xE5\x90\x9B", 212 },  // 月亮代表我的心, 鄧麗君
            { "d4", "\xD9\x84\xD9\x85 \xD8\xA3\xD9\x83\xD9\x86", "\xD8\xA3\xD9\x85 \xD9\x83\xD9\x84\xD8\xAB\xD9\x88\xD9\x85", 2700 },  // Arabic
            { "d5", "Chaiyya Chaiyya (\xE0\xA4\x9B\xE0\xA4\xAF\xE0\xA5\x8D\xE0\xA4\xAF\xE0\xA4\xBE)", "A. R. Rahman", 413 },
            { "d6", "Caf\xC3\xA9 del Mar \xE2\x80\x94 \xC3\x89t\xC3\xA9 \xE2\x99\xAA", "Energy 52", 457 },
            { "d7", "\xD0\x9A\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xBD\xD0\xBA\xD0\xB0", "\xD0\xA5\xD0\xBE\xD1\x80", 190 },  // Калинка, Хор
            { "d8", "A very long title that goes on well past the width of the window so that it has to be cut short with an ellipsis", "Someone", 300 },
        };
        for (size_t i = 0; i < sizeof samples / sizeof *samples; i++) player_add_track(a->player, &samples[i]);
    }
    return SDL_APP_CONTINUE;
}

static void paste(app *a, const char *text) {
    if (!text) return;
    char url[1024];
    SDL_strlcpy(url, text, sizeof url);
    char *s = url;
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
    for (char *e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r'); ) *--e = 0;
    if (strstr(s, "youtube.com/") || strstr(s, "youtu.be/")) player_add(a->player, s);
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *e) {
    app *a = state;
    if (!a->ren) return SDL_APP_CONTINUE;
    SDL_ConvertEventToRenderCoordinates(a->ren, e);
    if (e->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_DROP_TEXT) paste(a, e->drop.data);
    if (e->type == SDL_EVENT_KEY_DOWN) {
        bool ctrl = e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        switch (e->key.key) {
        case SDLK_ESCAPE: return SDL_APP_SUCCESS;
        case SDLK_V: if (ctrl) { char *t = SDL_GetClipboardText(); paste(a, t); SDL_free(t); } break;
        case SDLK_SPACE: player_toggle_pause(a->player); break;
        case SDLK_RIGHT: player_next(a->player); break;
        case SDLK_LEFT: player_previous(a->player); break;
        case SDLK_UP: if (a->selected > 0) a->selected--; break;
        case SDLK_DOWN: a->selected++; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: player_play(a->player, a->selected); break;
        case SDLK_MINUS: case SDLK_KP_MINUS: a->volume = fmaxf(0, a->volume - 0.1f), gs_mix_set_volume(a->volume); break;
        case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: a->volume = fminf(1.5f, a->volume + 0.1f), gs_mix_set_volume(a->volume); break;
        case SDLK_F1: case SDLK_GRAVE: a->show_stats = !a->show_stats; break;
        }
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        for (int i = 0; i < a->nrows; i++) {
            SDL_FRect r = a->rows[i];
            if (e->button.x >= r.x && e->button.x < r.x + r.w && e->button.y >= r.y && e->button.y < r.y + r.h)
                a->selected = a->row_track[i], player_play(a->player, a->row_track[i]);
        }
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        a->scroll -= (int)e->wheel.y * 3;
    }
    return SDL_APP_CONTINUE;
}

static SDL_FColor rgb(int r, int g, int b) { return (SDL_FColor){ r / 255.0f, g / 255.0f, b / 255.0f, 1 }; }

// Draws text cut to fit `max` pixels, with an ellipsis.
static void fit(app *a, float px, float x, float y, float max, const char *s, SDL_FColor c) {
    if (gs_fontset_width(a->fonts, px, s) <= max) { gs_fontset_draw(a->glyphs, a->fonts, px, x, y, s, c); return; }
    char buf[300];
    SDL_strlcpy(buf, s, sizeof buf);
    size_t n = strlen(buf);
    while (n > 0) {
        do n--; while (n > 0 && (buf[n] & 0xC0) == 0x80);  // whole characters
        memcpy(buf + n, "\xE2\x80\xA6", 4);  // …
        if (gs_fontset_width(a->fonts, px, buf) <= max) break;
    }
    gs_fontset_draw(a->glyphs, a->fonts, px, x, y, buf, c);
}

static void clock_text(double s, char *out, size_t size) {
    int t = (int)s;
    if (t >= 3600) snprintf(out, size, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(out, size, "%d:%02d", t / 60, t % 60);
}

SDL_AppResult SDL_AppIterate(void *state) {
    app *a = state;
    gs_stats_frame_begin(&a->stats);
    if (!a->demo) player_update(a->player);
    int ow, oh;
    SDL_GetCurrentRenderOutputSize(a->ren, &ow, &oh);
    float u = oh / 560.0f;  // scale everything with the window
    if (u < 0.8f) u = 0.8f;
    const SDL_FColor ink = rgb(238, 232, 222), dim = rgb(150, 142, 130), accent = rgb(255, 92, 70), faint = rgb(95, 90, 84);
    SDL_SetRenderDrawColor(a->ren, 20, 19, 18, 255);
    SDL_RenderClear(a->ren);
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);

    static track q[2000];
    int cur, n = player_queue(a->player, q, 2000, &cur);
    if (a->demo && cur < 0 && n) cur = 0;  // shown as playing
    if (a->selected >= n) a->selected = n ? n - 1 : 0;
    float x = 24 * u, w = ow - 48 * u, y = 44 * u;
    char buf[64], status[256];
    player_status(a->player, status, sizeof status);

    if (cur >= 0 && cur < n) {
        fit(a, 26 * u, x, y, w, q[cur].title[0] ? q[cur].title : "\xE2\x80\xA6", ink);
        fit(a, 17 * u, x, y + 30 * u, w, q[cur].artist[0] ? q[cur].artist : " ", dim);
        double pos = player_position(a->player), dur = player_duration(a->player);
        if (!dur) dur = q[cur].duration;
        float by = y + 52 * u;
        SDL_SetRenderDrawColor(a->ren, 60, 57, 54, 255);
        SDL_RenderFillRect(a->ren, &(SDL_FRect){ x, by, w, 4 * u });
        SDL_SetRenderDrawColor(a->ren, 255, 92, 70, 255);
        SDL_RenderFillRect(a->ren, &(SDL_FRect){ x, by, dur > 0 ? w * (float)fmin(1, pos / dur) : 0, 4 * u });
        char t1[16], t2[16];
        clock_text(pos, t1, sizeof t1), clock_text(dur, t2, sizeof t2);
        snprintf(buf, sizeof buf, "%s / %s%s%s", t1, t2, player_paused(a->player) ? "   paused" : "", player_loading(a->player) ? "   loading" : "");
        gs_fontset_draw(a->glyphs, a->fonts, 14 * u, x, by + 22 * u, buf, dim);
    } else {
        gs_fontset_draw(a->glyphs, a->fonts, 22 * u, x, y, player_loading(a->player) ? "Loading..." : "Paste a YouTube Music link (Ctrl+V)", ink);
        fit(a, 14 * u, x, y + 28 * u, w, a->tools.status[0] && SDL_GetAtomicInt(&a->tools.state) != 1 ? a->tools.status : "a track, an album or a playlist", dim);
    }

    // The queue, scrolled to keep the selected track in view.
    float qy = 150 * u, rh = 26 * u, bottom = oh - 40 * u;
    int visible = (int)((bottom - qy) / rh);
    if (visible < 1) visible = 1;
    if (a->selected < a->scroll) a->scroll = a->selected;
    if (a->selected >= a->scroll + visible) a->scroll = a->selected - visible + 1;
    if (a->scroll > n - visible) a->scroll = n - visible;
    if (a->scroll < 0) a->scroll = 0;
    a->nrows = 0;
    for (int i = a->scroll; i < n && a->nrows < visible && a->nrows < 64; i++) {
        float ry = qy + a->nrows * rh;
        SDL_FRect row = { x - 8 * u, ry - 18 * u, w + 16 * u, rh };
        if (i == a->selected) {
            SDL_SetRenderDrawColor(a->ren, 44, 41, 38, 255);
            SDL_RenderFillRect(a->ren, &row);
        }
        a->rows[a->nrows] = row, a->row_track[a->nrows++] = i;
        SDL_FColor c = i == cur ? accent : ink;
        clock_text(q[i].duration, buf, sizeof buf);
        float tw = gs_fontset_width(a->fonts, 14 * u, buf);
        char line[500];
        snprintf(line, sizeof line, "%s%s%s", q[i].title[0] ? q[i].title : q[i].id, q[i].artist[0] ? "  \xC2\xB7  " : "", q[i].artist);
        fit(a, 15 * u, x, ry, w - tw - 16 * u, line, c);
        if (q[i].duration > 0) gs_fontset_draw(a->glyphs, a->fonts, 14 * u, x + w - tw, ry, buf, faint);
    }
    fit(a, 13 * u, x, oh - 16 * u, w, status[0] ? status : "Ctrl+V link   Space pause   \xE2\x86\x90 \xE2\x86\x92 track   \xE2\x86\x91 \xE2\x86\x93 Enter pick   -/+ volume   F1 stats", faint);

    gs_stats_frame_end(&a->stats);
    if (a->show_stats) {
        char pacing[96];
        gs_pace_describe(&a->pace, pacing, sizeof pacing);
        gs_stats_draw(&a->stats, a->ren, -12, 12, pacing);
    }
    if (a->shot && SDL_GetTicks() - a->started >= a->shot_at * 1000) {
        SDL_Surface *s = SDL_RenderReadPixels(a->ren, NULL), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : NULL;
        bool ok = c && stbi_write_png(a->shot, c->w, c->h, 4, c->pixels, c->pitch);
        SDL_DestroySurface(c), SDL_DestroySurface(s);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    SDL_RenderPresent(a->ren);
    gs_pace_wait(&a->pace);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *state, SDL_AppResult result) {
    app *a = state;
    (void)result;
    if (!a) return;
    gs_mix_close();
    player_free(a->player);
    if (a->jobs) gs_jobs_wait(a->jobs), gs_jobs_free(a->jobs);
    gs_fontset_free(a->fonts);
    gs_glyphs_free(a->glyphs);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_free(a);
}
