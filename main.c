// A native YouTube Music mini player on gesso: paste or pass a link (a track, an album, a playlist)
// and it plays, streaming the audio with its own yt-dlp.
//   gtube [URL...]                             play
//   gtube --wav F.wav --seconds S URL          render the first track offline (a test of decoding)
//   gtube --tools [--probe URL]                just get or update yt-dlp and a JavaScript runtime
//   --data DIR                                  where tools live (default: the user's app data folder)
//   --demo                                      a queue of sample titles in many scripts, nothing played
//   --shot F.png [--at S]                       render one frame at S seconds, headless, and quit
//   --import-cookies FILE                       sign in with a cookies.txt exported from a browser
//   --refresh                                   renew the saved session once, report, and quit
//   --api search|albums|playlists|browse|radio ARG   print one YouTube Music API answer (every page for browse)
//   --view 1-4, --search QUERY, --radio VIDEO   start on a view, with a search, or playing a radio
//   --effect N                                  start the visualizer on its Nth effect
//   --sign-in                                   open the sign-in window at start
//   --sign-out                                  forget the saved session
// Views: 1 queue, 2 liked music, 3 playlists, 4 visualizer (Up/Down effect, Enter auto, t scroller,
// f full screen); / or Ctrl+F opens search with its box ready to type. Alt+Enter or F11 toggles full
// screen in any view. Up/Down, Page
// Up/Down, Home/End and Enter or a click pick; a song plays its whole list from there, an album or
// playlist opens (Esc or Backspace goes back). r starts a radio from the selected song, l likes or
// unlikes it. Left/Right switch views. Space pauses (or starts), n and p or the media keys next and
// previous track, Ctrl+V pastes a link, -/+ volume, s signs in,
// F1 performance overlay. Closing the window quits.
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
#include "account.h"
#include "api.h"
#include "library.h"
#include "player.h"
#include "signin.h"
#include "state.h"
#include "viz.h"
#include "tools.h"

#define RATE 48000
#define REFRESH_MS (10 * 60 * 1000)  // how often a signed-in session is renewed

enum { V_QUEUE, V_LIKED, V_PLAYLISTS, V_VIZ, V_SEARCH, V_OPEN, VIEWS };  // the tabs come before V_SEARCH

typedef struct {
    tools tools;
    library library;
    int view, back;  // the view shown, and the one an opened album or playlist goes back to
    int selected[VIEWS], scroll[VIEWS], visible;
    uint64_t wheel_until;
    bool typing;     // the search box has the keyboard
    char query[256];
    bool radio;      // the queue is a radio, extended as it plays
    int radio_gen, radio_taken;
    bool radio_replace;  // the radio's first tracks become the queue (a restored radio appends instead)
    SDL_FRect tabs[4], header;
    viz *viz;
    bool fullscreen;
    double pace_cap;
    bool uncapped;  // --uncapped: no vsync and no frame cap
    float audio_buf[2048 * 2];
    double last_frame;
    account account;
    signin *signin;
    char dir[1024];
    uint64_t next_refresh;
    char note[160], seen[160];  // a message for a few seconds, and the account status last shown
    uint64_t note_until;
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
    SDL_FRect rows[64];
    int row_index[64], nrows;
    bool persist;         // remembers the window and queue (not in test modes)
    window_state window;
    bool window_changed;
    int saved_version;    // the queue as last saved
    uint64_t next_save;
} app;

static void sign_in(app *a);

// Whether a saved window position is still on a connected display (monitors come and go).
static bool on_a_display(int x, int y) {
    if (x == (int)SDL_WINDOWPOS_CENTERED) return false;
    int n;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    bool ok = false;
    for (int i = 0; i < n && !ok; i++) {
        SDL_Rect r;
        ok = SDL_GetDisplayBounds(ids[i], &r) && x + 40 >= r.x && x + 40 < r.x + r.w && y + 10 >= r.y && y + 10 < r.y + r.h;
    }
    SDL_free(ids);
    return ok;
}
static void start_radio(app *a);

static bool api_test(account *acc, const char *kind, const char *arg) {
    static const char *const kinds[] = { "song", "album", "playlist", "heading" };
    page pg;
    char more[2048] = "";
    int total = 0, pages = 0;
    uint64_t start = SDL_GetTicks();
    do {
        bool ok = !strcmp(kind, "search") ? api_search(acc, arg, SEARCH_SONGS, &pg)
                : !strcmp(kind, "albums") ? api_search(acc, arg, SEARCH_ALBUMS, &pg)
                : !strcmp(kind, "playlists") ? api_search(acc, arg, SEARCH_PLAYLISTS, &pg)
                : !strcmp(kind, "radio") ? api_radio(acc, arg, more[0] ? more : NULL, &pg)
                : api_browse(acc, arg, more[0] ? more : NULL, &pg);
        if (!ok) { printf("failed: %s\n", pg.error); page_free(&pg); return false; }
        for (int i = 0; i < pg.n; i++, total++)
            if (total < 8 || i == pg.n - 1)
                printf("%4d %-8s %-24s %s | %s | %.0f s\n", total, kinds[pg.items[i].kind], pg.items[i].id, pg.items[i].title, pg.items[i].artist, pg.items[i].duration);
        SDL_strlcpy(more, !strcmp(kind, "browse") || (!strcmp(kind, "radio") && pages < 2) ? pg.more : "", sizeof more);
        pages++;
        page_free(&pg);
    } while (more[0]);
    printf("%d items in %d page%s, %.2f s\n", total, pages, pages == 1 ? "" : "s", (SDL_GetTicks() - start) / 1000.0);
    return true;
}

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
    const char *data = NULL, *probe = NULL, *wav = NULL, *import = NULL, *api_kind = NULL, *api_arg = NULL, *radio = NULL, *urls[16];
    int nurls = 0, effect = 1;
    double seconds = 30;
    bool tools_only = false, demo = false, refresh = false, sign_out = false, open_signin = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) probe = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = SDL_atof(argv[++i]);
        else if (!strcmp(argv[i], "--tools")) tools_only = true;
        else if (!strcmp(argv[i], "--demo")) demo = a->demo = true;
        else if (!strcmp(argv[i], "--uncapped")) a->uncapped = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) a->shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) a->shot_at = SDL_atof(argv[++i]);
        else if (!strcmp(argv[i], "--import-cookies") && i + 1 < argc) import = argv[++i];
        else if (!strcmp(argv[i], "--refresh")) refresh = true;
        else if (!strcmp(argv[i], "--sign-out")) sign_out = true;
        else if (!strcmp(argv[i], "--sign-in")) open_signin = true;
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) {
            int v = SDL_atoi(argv[++i]) - 1;  // (SDL_clamp is a macro that evaluates its argument more than once)
            a->view = SDL_clamp(v, 0, 3);
        }
        else if (!strcmp(argv[i], "--search") && i + 1 < argc) SDL_strlcpy(a->query, argv[++i], sizeof a->query), a->view = V_SEARCH;
        else if (!strcmp(argv[i], "--radio") && i + 1 < argc) radio = argv[++i];
        else if (!strcmp(argv[i], "--effect") && i + 1 < argc) effect = SDL_atoi(argv[++i]);
        else if (!strcmp(argv[i], "--api") && i + 2 < argc) api_kind = argv[++i], api_arg = argv[++i];
        else if (nurls < 16) urls[nurls++] = argv[i];
    }
    char *dir = a->dir;
    data_dir(data, dir, sizeof a->dir);
    tools_init(&a->tools, dir);
    account_init(&a->account, dir);
    a->jobs = gs_jobs_new(4);
    library_init(&a->library, &a->account, a->jobs);
    if (sign_out) account_sign_out(&a->account);
    if (import && !account_import(&a->account, import)) return printf("%s\n", a->account.status), SDL_APP_FAILURE;
    if (refresh) {
        bool ok = account_signed_in(&a->account) && account_refresh(&a->account);
        printf("%s\n", a->account.status);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (sign_out || import) return SDL_APP_SUCCESS;
    if (api_kind) return api_test(&a->account, api_kind, api_arg) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

    if (tools_only) {
        uint64_t start = SDL_GetTicks();
        tools_prepare(&a->tools);
        printf("state %d (%s) in %.1f s\nyt-dlp %s\njs %s\n", SDL_GetAtomicInt(&a->tools.state), a->tools.status,
               (SDL_GetTicks() - start) / 1000.0, a->tools.ytdlp, a->tools.js);
        if (SDL_GetAtomicInt(&a->tools.state) != 1) return SDL_APP_FAILURE;
        if (probe) {
            const char *args[] = { "-f", "251", "--print", "%(title)s | %(artist,uploader)s | format %(format_id)s %(ext)s %(acodec)s %(abr)s kbit/s | %(duration)s s", probe, NULL };
            char jar[1200];
            bool signed_in = account_jar_file(&a->account, jar, sizeof jar);
            SDL_Process *p = tools_ytdlp(&a->tools, args, signed_in ? jar : NULL);
            size_t n;
            int code;
            char *out = p ? SDL_ReadProcess(p, &n, &code) : NULL;
            char errors[2048];
            tools_errors(p, errors, sizeof errors, 0);
            printf("probe (exit %d%s): %s%s", p ? code : -1, signed_in ? ", signed in" : "", out ? out : "\n", errors);
            SDL_free(out);
            SDL_DestroyProcess(p);
            if (signed_in) account_jar_done(jar);
        }
        return SDL_APP_SUCCESS;
    }

    gs_jobs_add(a->jobs, tools_prepare, &a->tools);
    if (wav) {  // offline: the first track, as fast as it arrives
        a->player = player_new(&a->tools, &a->account, a->jobs, false);
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
    a->persist = !a->shot && !demo;
    a->window = (window_state){ SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 560, false };
    if (a->persist) state_load_window(dir, &a->window);
    if (!SDL_CreateWindowAndRenderer("gesso gtube", a->window.w, a->window.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN, &a->win, &a->ren))
        return SDL_Log("window: %s", SDL_GetError()), SDL_APP_FAILURE;
    if (on_a_display(a->window.x, a->window.y)) SDL_SetWindowPosition(a->win, a->window.x, a->window.y);
    if (a->window.maximized) SDL_MaximizeWindow(a->win);
    SDL_ShowWindow(a->win);
    gs_pace_set(&a->pace, a->win, a->ren, !a->uncapped, a->pace_cap = a->uncapped ? 0 : 30);
    a->viz = viz_new(a->ren, RATE);
    for (int i = 1; i < effect; i++) viz_step(a->viz, 1);
    a->audio = !a->shot && gs_mix_open(RATE);
    a->started = SDL_GetTicks();
    a->volume = 1;
    a->player = player_new(&a->tools, &a->account, a->jobs, a->audio);
    a->glyphs = gs_glyphs_new(a->ren, 1024);
    a->fonts = gs_fontset_system();
    if (a->persist && !nurls && !radio) {  // links given to play take its place
        radio_state r;
        state_load_queue(dir, a->player, &r);
        if (r.on) {  // a radio goes on where it left off
            a->radio_gen = library_radio_resume(&a->library, r.seed, r.title, r.more);
            a->radio = true, a->radio_taken = 0, a->radio_replace = false;
        }
        int cur;
        player_queue(a->player, NULL, 0, &cur);
        a->selected[V_QUEUE] = cur > 0 ? cur : 0;
    }
    a->saved_version = player_version(a->player);
    for (int i = 0; i < nurls; i++) player_add(a->player, urls[i]);
    if (!a->shot && !demo && account_signed_in(&a->account)) a->next_refresh = SDL_GetTicks();  // renew at launch
    if (open_signin) sign_in(a);
    if (!demo && account_signed_in(&a->account)) library_signed_in(&a->library);
    if (a->query[0]) library_search(&a->library, a->query);
    if (radio) {
        track seed = { 0 };
        SDL_strlcpy(seed.id, radio, sizeof seed.id);
        player_add_track(a->player, &seed);
        a->selected[V_QUEUE] = 0;
        start_radio(a);
    }
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

static void note(app *a, const char *msg) {
    SDL_strlcpy(a->note, msg, sizeof a->note);
    a->note_until = SDL_GetTicks() + 8000;
}

static void sign_in(app *a) {
    if (a->signin) return;
    char why[200];
    a->signin = signin_open(a->dir, why, sizeof why);
    note(a, a->signin ? "Sign in to YouTube Music in the new window" : why);
}

// ---- Views ----

static const int view_shelf[VIEWS] = { -1, SHELF_LIKED, SHELF_PLAYLISTS, -1, SHELF_SEARCH, SHELF_OPEN };
static track queue_copy[2000];

static void song_from_track(const track *t, item *out) {
    memset(out, 0, sizeof *out);
    out->kind = ITEM_SONG, out->duration = t->duration;
    SDL_strlcpy(out->id, t->id, sizeof out->id);
    SDL_strlcpy(out->title, t->title, sizeof out->title);
    SDL_strlcpy(out->artist, t->artist, sizeof out->artist);
}

static void track_from_song(const item *s, track *out) {
    memset(out, 0, sizeof *out);
    out->duration = s->duration;
    SDL_strlcpy(out->id, s->id, sizeof out->id);
    SDL_strlcpy(out->title, s->title, sizeof out->title);
    SDL_strlcpy(out->artist, s->artist, sizeof out->artist);
}

// The song selected in the current view, or else the one playing.
static bool chosen_song(app *a, item *out) {
    int cur, n = player_queue(a->player, queue_copy, 2000, &cur), sel = a->selected[a->view];
    if (a->view == V_QUEUE && sel < n) return song_from_track(&queue_copy[sel], out), true;
    if (view_shelf[a->view] >= 0) {
        library *l = &a->library;
        SDL_LockMutex(l->lock);
        shelf *s = &l->shelves[view_shelf[a->view]];
        bool ok = sel < s->n && s->items[sel].kind == ITEM_SONG;
        if (ok) *out = s->items[sel];
        SDL_UnlockMutex(l->lock);
        if (ok) return true;
    }
    if (cur >= 0 && cur < n) return song_from_track(&queue_copy[cur], out), true;
    return false;
}

static void show(app *a, int view) {
    a->view = view;
    a->typing = false;
    SDL_StopTextInput(a->win);
}

static void start_typing(app *a) {
    a->view = V_SEARCH, a->typing = true;
    SDL_StartTextInput(a->win);
}

// Enter or a click on row i: play a song (its whole list becomes the queue), or open an album or playlist.
static void activate(app *a, int i) {
    if (a->view == V_QUEUE) { player_play(a->player, i); return; }
    if (view_shelf[a->view] < 0) return;
    library *l = &a->library;
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[view_shelf[a->view]];
    if (i < 0 || i >= s->n) { SDL_UnlockMutex(l->lock); return; }
    item it = s->items[i];
    track *tracks = NULL;
    int n = 0, start = 0;
    if (it.kind == ITEM_SONG) {
        tracks = SDL_malloc(sizeof *tracks * (size_t)s->n);
        for (int k = 0; k < s->n; k++)
            if (s->items[k].kind == ITEM_SONG) {
                if (k == i) start = n;
                track_from_song(&s->items[k], &tracks[n++]);
            }
    }
    SDL_UnlockMutex(l->lock);
    if (tracks) {
        a->radio = false;
        player_set_queue(a->player, tracks, n, start);
        SDL_free(tracks);
    } else if (it.kind == ITEM_ALBUM || it.kind == ITEM_PLAYLIST) {
        if (a->view != V_OPEN) a->back = a->view;
        library_open(l, SHELF_OPEN, it.id, it.title);
        a->selected[V_OPEN] = a->scroll[V_OPEN] = 0;
        show(a, V_OPEN);
    }
}

static void start_radio(app *a) {
    item song;
    if (!chosen_song(a, &song)) { note(a, "Pick a song to start a radio from"); return; }
    library_radio(&a->library, song.id, song.title);
    SDL_LockMutex(a->library.lock);
    a->radio_gen = a->library.shelves[SHELF_RADIO].gen;
    SDL_UnlockMutex(a->library.lock);
    a->radio = true, a->radio_taken = 0, a->radio_replace = true;
    char msg[320];
    snprintf(msg, sizeof msg, "Radio from %s", song.title[0] ? song.title : "this song");
    note(a, msg);
    show(a, V_QUEUE);
}

static void toggle_like(app *a) {
    item song;
    if (!account_signed_in(&a->account)) { note(a, "Press s to sign in and like songs"); return; }
    if (!chosen_song(a, &song)) return;
    SDL_LockMutex(a->library.lock);
    bool liked = library_liked(&a->library, song.id);
    SDL_UnlockMutex(a->library.lock);
    library_like(&a->library, &song, !liked);
    char msg[320];
    snprintf(msg, sizeof msg, "%s %s", liked ? "Unliked" : "Liked", song.title);
    note(a, msg);
}

// Moves a radio's newly loaded tracks into the queue, and asks for more near its end.
static void follow_radio(app *a) {
    library *l = &a->library;
    SDL_LockMutex(l->lock);
    shelf *s = &l->shelves[SHELF_RADIO];
    int from = a->radio_taken, n = s->gen == a->radio_gen ? s->n : 0;
    track *fresh = n > from ? SDL_malloc(sizeof *fresh * (size_t)(n - from)) : NULL;
    for (int i = from; i < n; i++) track_from_song(&s->items[i], &fresh[i - from]);
    if (n && !s->title[0]) SDL_strlcpy(s->title, s->items[0].title, sizeof s->title);  // started from a bare link
    char error[160];
    SDL_strlcpy(error, s->gen == a->radio_gen && !s->loading ? s->error : "", sizeof error);
    SDL_UnlockMutex(l->lock);
    if (fresh) {
        if (a->radio_replace) player_set_queue(a->player, fresh, n, 0);
        else for (int i = 0; i < n - from; i++) player_add_track(a->player, &fresh[i]);
        a->radio_replace = false;
        a->radio_taken = n;
        SDL_free(fresh);
    }
    if (error[0]) {
        char msg[220];
        snprintf(msg, sizeof msg, "Radio stopped: %s", error);
        note(a, msg);
        a->radio = false;
        return;
    }
    int cur, len = player_queue(a->player, queue_copy, 2000, &cur);
    if (cur >= len - 5) library_radio_more(l);  // (does nothing while a page is loading)
}

static void save_queue(app *a) {
    radio_state r = { .on = a->radio };
    SDL_LockMutex(a->library.lock);
    shelf *s = &a->library.shelves[SHELF_RADIO];
    SDL_strlcpy(r.seed, s->source, sizeof r.seed);
    SDL_strlcpy(r.title, s->title, sizeof r.title);
    SDL_strlcpy(r.more, s->more, sizeof r.more);
    SDL_UnlockMutex(a->library.lock);
    state_save_queue(a->dir, a->player, &r);
}

static void paste_query(app *a, const char *text) {
    size_t n = strlen(a->query);
    for (; text && *text && n + 1 < sizeof a->query; text++)
        if (*text != '\n' && *text != '\r') a->query[n++] = *text;
    a->query[n] = 0;
}

static bool heading_at(app *a, int i) {
    if (view_shelf[a->view] < 0) return false;
    SDL_LockMutex(a->library.lock);
    shelf *s = &a->library.shelves[view_shelf[a->view]];
    bool h = i >= 0 && i < s->n && s->items[i].kind == ITEM_HEADING;
    SDL_UnlockMutex(a->library.lock);
    return h;
}

static bool inside(SDL_FRect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *e) {
    app *a = state;
    if (!a->ren) return SDL_APP_CONTINUE;
    signin_event(a->signin, e);
    if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST && e->window.windowID != SDL_GetWindowID(a->win)) return SDL_APP_CONTINUE;
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) && e->key.windowID != SDL_GetWindowID(a->win)) return SDL_APP_CONTINUE;
    SDL_ConvertEventToRenderCoordinates(a->ren, e);
    if (e->type == SDL_EVENT_QUIT || e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_WINDOW_MOVED || e->type == SDL_EVENT_WINDOW_RESIZED || e->type == SDL_EVENT_WINDOW_MAXIMIZED || e->type == SDL_EVENT_WINDOW_RESTORED) {
        bool max = SDL_GetWindowFlags(a->win) & SDL_WINDOW_MAXIMIZED;
        if (!max && !(SDL_GetWindowFlags(a->win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN))) {  // the size to come back to
            SDL_GetWindowPosition(a->win, &a->window.x, &a->window.y);
            SDL_GetWindowSize(a->win, &a->window.w, &a->window.h);
        }
        a->window.maximized = max;
        a->window_changed = true;
    }
    if (e->type == SDL_EVENT_DROP_TEXT) paste(a, e->drop.data);
    int *sel = &a->selected[a->view];
    if (e->type == SDL_EVENT_KEY_DOWN && ((e->key.key == SDLK_RETURN && (e->key.mod & SDL_KMOD_ALT)) || e->key.key == SDLK_F11)) {
        SDL_SetWindowFullscreen(a->win, a->fullscreen = !a->fullscreen);  // Alt+Enter or F11, in any view
        return SDL_APP_CONTINUE;
    }
    if (e->type == SDL_EVENT_TEXT_INPUT && a->typing) paste_query(a, e->text.text);
    if (e->type == SDL_EVENT_KEY_DOWN && a->typing) {  // the search box has the keyboard
        bool ctrl = e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        switch (e->key.key) {
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (a->query[0]) library_search(&a->library, a->query), *sel = a->scroll[V_SEARCH] = 0;
            show(a, V_SEARCH);
            break;
        case SDLK_ESCAPE: case SDLK_DOWN: show(a, V_SEARCH); break;
        case SDLK_BACKSPACE: {
            size_t n = strlen(a->query);
            while (n > 0 && (a->query[--n] & 0xC0) == 0x80) {}
            a->query[n] = 0;
            break;
        }
        case SDLK_V: if (ctrl) { char *t = SDL_GetClipboardText(); paste_query(a, t); SDL_free(t); } break;
        }
        return SDL_APP_CONTINUE;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && a->view == V_VIZ) {  // the visualizer's own keys
        switch (e->key.key) {
        case SDLK_UP: viz_step(a->viz, -1); return SDL_APP_CONTINUE;
        case SDLK_DOWN: viz_step(a->viz, 1); return SDL_APP_CONTINUE;
        case SDLK_RETURN: case SDLK_KP_ENTER: viz_set_auto(a->viz, !viz_get_auto(a->viz)); return SDL_APP_CONTINUE;
        case SDLK_T: viz_set_scroller(a->viz, !viz_get_scroller(a->viz)); return SDL_APP_CONTINUE;
        case SDLK_G: if (viz_is_bend(a->viz)) viz_bend_switch(a->viz); return SDL_APP_CONTINUE;
        case SDLK_F:
            if (!(e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) { SDL_SetWindowFullscreen(a->win, a->fullscreen = !a->fullscreen); return SDL_APP_CONTINUE; }
            break;
        case SDLK_ESCAPE: if (a->fullscreen) SDL_SetWindowFullscreen(a->win, a->fullscreen = false); return SDL_APP_CONTINUE;
        }
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        bool ctrl = e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        int page = a->visible > 1 ? a->visible - 1 : 1;
        switch (e->key.key) {
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (a->view == V_OPEN) show(a, a->back);
            else if (e->key.key == SDLK_ESCAPE && a->fullscreen) SDL_SetWindowFullscreen(a->win, a->fullscreen = false);
            break;
        case SDLK_V: if (ctrl) { char *t = SDL_GetClipboardText(); paste(a, t); SDL_free(t); } break;
        case SDLK_F: if (ctrl) start_typing(a); break;
        case SDLK_SLASH: start_typing(a); break;
        case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: show(a, (int)(e->key.key - SDLK_1)); break;
        case SDLK_SPACE: player_toggle_pause(a->player); break;
        case SDLK_RIGHT: case SDLK_LEFT: {  // the tabs, wrapping around; from search or an opened list, its neighbours
            int tab = a->view < V_SEARCH ? a->view : a->view == V_OPEN && a->back < V_SEARCH ? a->back : -1;
            bool right = e->key.key == SDLK_RIGHT;
            show(a, tab < 0 ? (right ? 0 : 3) : (tab + (right ? 1 : 3)) % 4);
            break;
        }
        case SDLK_N: case SDLK_MEDIA_NEXT_TRACK: if (!ctrl) player_next(a->player); break;
        case SDLK_P: case SDLK_MEDIA_PREVIOUS_TRACK: if (!ctrl) player_previous(a->player); break;
        case SDLK_MEDIA_PLAY_PAUSE: case SDLK_MEDIA_PLAY: case SDLK_MEDIA_PAUSE: player_toggle_pause(a->player); break;
        case SDLK_UP:
            if (*sel > 0) (*sel)--;
            if (heading_at(a, *sel)) {  // headings are passed over
                if (*sel > 0) (*sel)--;
                else if (a->view == V_SEARCH) start_typing(a);
            }
            break;
        case SDLK_DOWN: (*sel)++; break;
        case SDLK_PAGEUP: *sel = *sel > page ? *sel - page : 0; break;
        case SDLK_PAGEDOWN: *sel += page; break;
        case SDLK_HOME: *sel = 0; break;
        case SDLK_END: *sel = 1 << 30; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: activate(a, *sel); break;
        case SDLK_R: if (!ctrl) start_radio(a); break;
        case SDLK_L: if (!ctrl) toggle_like(a); break;
        case SDLK_MINUS: case SDLK_KP_MINUS: a->volume = fmaxf(0, a->volume - 0.1f), gs_mix_set_volume(a->volume); break;
        case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: a->volume = fminf(1.5f, a->volume + 0.1f), gs_mix_set_volume(a->volume); break;
        case SDLK_S: if (!ctrl) sign_in(a); break;
        case SDLK_F1: case SDLK_GRAVE: a->show_stats = !a->show_stats; break;
        }
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float x = e->button.x, y = e->button.y;
        for (int i = 0; i < 4; i++)
            if (inside(a->tabs[i], x, y)) {
                show(a, i);
                return SDL_APP_CONTINUE;
            }
        if (a->view == V_SEARCH && inside(a->header, x, y)) start_typing(a);
        else if (a->view == V_OPEN && inside(a->header, x, y)) show(a, a->back);
        for (int i = 0; i < a->nrows; i++)
            if (inside(a->rows[i], x, y)) *sel = a->row_index[i], activate(a, a->row_index[i]);
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        a->scroll[a->view] -= (int)e->wheel.y * 3;
        a->wheel_until = SDL_GetTicks() + 1500;  // the list stays where it was scrolled for a moment
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

#define HEART "\xE2\x99\xA5"

// The key help along the bottom: each key on a key cap, then what it does. Pairs that do not fit
// are left out rather than cut.
static void draw_keys(app *a, const char *const (*keys)[2], int count, float u, float x, float y, float w, SDL_FColor key, SDL_FColor text) {
    float px = 13 * u, pad = 4 * u, end = x + w;
    for (int i = 0; i < count; i++) {
        float kw = gs_fontset_width(a->fonts, px, keys[i][0]), tw = gs_fontset_width(a->fonts, px, keys[i][1]);
        float cap = SDL_max(kw + 2 * pad, px + 5 * u);  // a single letter gets a square cap
        if (x + cap + 5 * u + tw > end) break;
        SDL_SetRenderDrawColor(a->ren, 50, 47, 44, 255);
        SDL_RenderFillRect(a->ren, &(SDL_FRect){ x, y - px + 1 * u, cap, px + 5 * u });
        gs_fontset_draw(a->glyphs, a->fonts, px, x + (cap - kw) / 2, y, keys[i][0], key);
        x += cap + 5 * u;
        gs_fontset_draw(a->glyphs, a->fonts, px, x, y, keys[i][1], text);
        x += tw + 16 * u;
    }
}

// Feeds the visualizer what is heard now (or the test signal, in demo mode) and draws it.
static void viz_frame(app *a, SDL_FRect area, const track *t) {
    double now = SDL_GetTicks() / 1000.0, dt = a->last_frame ? now - a->last_frame : 0;
    a->last_frame = now;
    int n = 0;
    if (a->demo) viz_test_signal(now - 2048.0 / RATE, a->audio_buf, n = 2048, RATE);
    else if (a->audio) n = gs_mix_recent(a->audio_buf, 2048);
    viz_feed(a->viz, a->audio_buf, n, dt);
    viz_draw(a->viz, area, now, t ? t->title : "", t ? t->artist : "", a->glyphs, a->fonts);
}

SDL_AppResult SDL_AppIterate(void *state) {
    app *a = state;
    double cap = a->uncapped ? 0 : a->view == V_VIZ ? 60 : 30;  // smooth motion for the visualizer, less work elsewhere
    if (cap != a->pace_cap) gs_pace_set(&a->pace, a->win, a->ren, !a->uncapped, a->pace_cap = cap);
    gs_stats_frame_begin(&a->stats);
    if (!a->demo) player_update(a->player);
    if (a->signin) {  // signing in: done when the page reaches YouTube signed in, or the window is closed
        char *jar = NULL;
        int r = signin_poll(a->signin, &jar);
        if (r) {
            signin_close(a->signin);
            a->signin = NULL;
            if (r > 0 && account_store(&a->account, jar)) {
                a->next_refresh = SDL_GetTicks() + REFRESH_MS;
                library_signed_in(&a->library);
                int cur;
                player_queue(a->player, NULL, 0, &cur);
                if (!player_stream(a->player) && cur >= 0) player_play(a->player, cur);  // what YouTube refused, again
            }
            note(a, r > 0 ? a->account.status : "Sign-in closed");
            SDL_free(jar);
        }
    }
    if (a->next_refresh && SDL_GetTicks() >= a->next_refresh) {
        a->next_refresh = account_signed_in(&a->account) ? SDL_GetTicks() + REFRESH_MS : 0;
        gs_jobs_add(a->jobs, account_refresh_job, &a->account);
    }
    char account_status[160];
    SDL_LockMutex(a->account.lock);
    SDL_strlcpy(account_status, a->account.status, sizeof account_status);
    SDL_UnlockMutex(a->account.lock);
    if (strcmp(account_status, a->seen)) {
        if (a->seen[0] && strncmp(account_status, "session refreshed", 17)) note(a, account_status);  // problems, not routine renewals
        SDL_strlcpy(a->seen, account_status, sizeof a->seen);
    }
    if (a->radio) follow_radio(a);
    if (a->persist && SDL_GetTicks() >= a->next_save) {  // at most every 2 s, and only what changed
        a->next_save = SDL_GetTicks() + 2000;
        if (a->window_changed) state_save_window(a->dir, &a->window), a->window_changed = false;
        int v = player_version(a->player);
        if (v != a->saved_version) save_queue(a), a->saved_version = v;
    }
    library *l = &a->library;
    SDL_LockMutex(l->lock);
    if (l->note[0]) note(a, l->note), l->note[0] = 0;
    SDL_UnlockMutex(l->lock);

    int ow, oh;
    SDL_GetCurrentRenderOutputSize(a->ren, &ow, &oh);
    float u = oh / 560.0f;  // scale everything with the window
    if (u < 0.8f) u = 0.8f;
    const SDL_FColor ink = rgb(238, 232, 222), dim = rgb(150, 142, 130), accent = rgb(255, 92, 70), faint = rgb(95, 90, 84);
    SDL_SetRenderDrawColor(a->ren, 20, 19, 18, 255);
    SDL_RenderClear(a->ren);
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);

    int cur, qn = player_queue(a->player, queue_copy, 2000, &cur);
    if (a->demo && cur < 0 && qn) cur = 0;  // shown as playing
    float x = 24 * u, w = ow - 48 * u, y = 44 * u;
    char buf[400], status[256];
    player_status(a->player, status, sizeof status);
    if (a->fullscreen && a->view == V_VIZ) {  // the whole screen is the visualizer
        viz_frame(a, (SDL_FRect){ 0, 0, (float)ow, (float)oh }, cur >= 0 && cur < qn ? &queue_copy[cur] : NULL);
        goto drawn;
    }

    // Now playing.
    SDL_LockMutex(l->lock);
    if (cur >= 0 && cur < qn) {
        track *t = &queue_copy[cur];
        bool liked = library_liked(l, t->id);
        float hw = liked ? gs_fontset_width(a->fonts, 22 * u, HEART) + 10 * u : 0;
        fit(a, 26 * u, x, y, w - hw, t->title[0] ? t->title : "\xE2\x80\xA6", ink);
        if (liked) gs_fontset_draw(a->glyphs, a->fonts, 22 * u, x + w - hw + 10 * u, y, HEART, accent);
        fit(a, 17 * u, x, y + 30 * u, w, t->artist[0] ? t->artist : " ", dim);
        double pos = player_position(a->player), dur = player_duration(a->player);
        if (!dur) dur = t->duration;
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
        gs_fontset_draw(a->glyphs, a->fonts, 22 * u, x, y, player_loading(a->player) ? "Loading..." : "Nothing playing", ink);
        fit(a, 14 * u, x, y + 28 * u, w, a->tools.status[0] && SDL_GetAtomicInt(&a->tools.state) != 1 ? a->tools.status : "Search with /, open your liked music or playlists, or paste a link (Ctrl+V)", dim);
    }

    // Tabs.
    static const char *const tab_names[4] = { "Queue", "Liked", "Playlists", "Visualizer" };  // search opens with /
    float tx = x, ty = 150 * u;
    for (int i = 0; i < 4; i++) {
        bool on = a->view == i || (a->view == V_OPEN && a->back == i);
        snprintf(buf, sizeof buf, "%d  %s", i + 1, tab_names[i]);
        float tw = gs_fontset_width(a->fonts, 15 * u, buf);
        gs_fontset_draw(a->glyphs, a->fonts, 15 * u, tx, ty, buf, on ? ink : faint);
        if (on) {
            SDL_SetRenderDrawColor(a->ren, 255, 92, 70, 255);
            SDL_RenderFillRect(a->ren, &(SDL_FRect){ tx, ty + 8 * u, tw, 2 * u });
        }
        a->tabs[i] = (SDL_FRect){ tx - 6 * u, ty - 18 * u, tw + 12 * u, 30 * u };
        tx += tw + 28 * u;
    }

    if (a->view == V_VIZ) {
        float top = ty + 18 * u;
        viz_frame(a, (SDL_FRect){ 0, top, (float)ow, oh - 32 * u - top }, cur >= 0 && cur < qn ? &queue_copy[cur] : NULL);
        a->nrows = 0;
        goto listed;
    }

    // The view's header line.
    shelf *s = a->view == V_QUEUE ? NULL : &l->shelves[view_shelf[a->view]];
    bool signed_out = !account_signed_in(&a->account) && (a->view == V_LIKED || a->view == V_PLAYLISTS);
    float hy = 190 * u;
    a->header = (SDL_FRect){ x - 8 * u, hy - 20 * u, w + 16 * u, 28 * u };
    const char *more = s && s->loading ? ", loading\xE2\x80\xA6" : "";
    if (a->view == V_SEARCH) {
        SDL_SetRenderDrawColor(a->ren, a->typing ? 52 : 36, a->typing ? 48 : 34, a->typing ? 45 : 32, 255);
        SDL_RenderFillRect(a->ren, &a->header);
        bool blink = a->typing && SDL_GetTicks() / 500 % 2 == 0;
        snprintf(buf, sizeof buf, "%s%s", a->query, blink ? "|" : "");
        fit(a, 16 * u, x, hy, w, a->query[0] || a->typing ? buf : "Press / to search songs, albums and playlists", a->query[0] ? ink : dim);
    } else if (signed_out) {
        fit(a, 15 * u, x, hy, w, a->view == V_LIKED ? "Press s to sign in and see your liked music" : "Press s to sign in and see your playlists", dim);
    } else if (a->view == V_QUEUE) {
        snprintf(buf, sizeof buf, "%d track%s%s%s", qn, qn == 1 ? "" : "s", a->radio ? "   radio from " : "", a->radio ? l->shelves[SHELF_RADIO].title : "");
        fit(a, 14 * u, x, hy, w, buf, dim);
    } else {
        int songs = 0;
        for (int i = 0; i < s->n; i++) songs += s->items[i].kind == ITEM_SONG;
        if (a->view == V_OPEN) snprintf(buf, sizeof buf, "\xE2\x80\xB9  %s   %d song%s%s", s->title, songs, songs == 1 ? "" : "s", more);
        else if (a->view == V_LIKED) snprintf(buf, sizeof buf, "%d liked song%s%s", songs, songs == 1 ? "" : "s", more);
        else snprintf(buf, sizeof buf, "%d playlist%s%s", s->n, s->n == 1 ? "" : "s", more);
        fit(a, 14 * u, x, hy, w, s->error[0] ? s->error : buf, s->error[0] ? accent : dim);
    }

    // The list, scrolled to keep the selection in view (unless the wheel just moved it).
    int n = signed_out ? 0 : s ? s->n : qn, *sel = &a->selected[a->view], *scroll = &a->scroll[a->view];
    float qy = 226 * u, rh = 26 * u, bottom = oh - 40 * u;
    int visible = (int)((bottom - qy) / rh);
    if (visible < 1) visible = 1;
    a->visible = visible;
    if (*sel >= n) *sel = n ? n - 1 : 0;
    if (s && *sel + 1 < n && s->items[*sel].kind == ITEM_HEADING) (*sel)++;  // a heading is never selected
    if (SDL_GetTicks() > a->wheel_until) {
        if (*sel < *scroll) *scroll = *sel;
        if (*sel >= *scroll + visible) *scroll = *sel - visible + 1;
    }
    if (*scroll > n - visible) *scroll = n - visible;
    if (*scroll < 0) *scroll = 0;
    a->nrows = 0;
    for (int i = *scroll; i < n && a->nrows < visible && a->nrows < 64; i++) {
        float ry = qy + a->nrows * rh;
        SDL_FRect row = { x - 8 * u, ry - 18 * u, w + 16 * u, rh };
        item it;
        if (s) it = s->items[i];
        else song_from_track(&queue_copy[i], &it);
        if (i == *sel && !a->typing) {
            SDL_SetRenderDrawColor(a->ren, 44, 41, 38, 255);
            SDL_RenderFillRect(a->ren, &row);
        }
        a->rows[a->nrows] = row, a->row_index[a->nrows++] = i;
        if (it.kind == ITEM_HEADING) {
            gs_fontset_draw(a->glyphs, a->fonts, 13 * u, x, ry, it.title, accent);
            continue;
        }
        bool playing = cur >= 0 && cur < qn && (s ? !strcmp(it.id, queue_copy[cur].id) : i == cur);
        SDL_FColor c = playing ? accent : ink;
        char right[32] = "";
        if (it.kind == ITEM_SONG && it.duration > 0) clock_text(it.duration, right, sizeof right);
        if (it.kind != ITEM_SONG) SDL_strlcpy(right, "\xE2\x80\xBA", sizeof right);  // ›
        float rw = gs_fontset_width(a->fonts, 14 * u, right), hw = gs_fontset_width(a->fonts, 14 * u, HEART);
        bool liked = it.kind == ITEM_SONG && library_liked(l, it.id);
        char line[500];
        snprintf(line, sizeof line, "%s%s%s", it.title[0] ? it.title : it.id, it.artist[0] ? "  \xC2\xB7  " : "", it.artist);
        fit(a, 15 * u, x, ry, w - rw - hw - 28 * u, line, c);
        if (liked) gs_fontset_draw(a->glyphs, a->fonts, 14 * u, x + w - rw - hw - 12 * u, ry, HEART, accent);
        if (right[0]) gs_fontset_draw(a->glyphs, a->fonts, 14 * u, x + w - rw, ry, right, faint);
    }
listed:
    SDL_UnlockMutex(l->lock);
    // A message shares the bottom line with the key help, on the right, in at most half of it, so
    // the keys are always shown.
    const char *footer = SDL_GetTicks() < a->note_until ? a->note : status;
    float mw = footer[0] ? fminf(gs_fontset_width(a->fonts, 13 * u, footer), w / 2) : 0;
    if (footer[0]) fit(a, 13 * u, x + w - mw, oh - 16 * u, mw, footer, dim);
    float kw = footer[0] ? w - mw - 24 * u : w;
    if (a->view == V_VIZ) {
        const char *const keys[][2] = {
            { "\xE2\x86\x91 \xE2\x86\x93", viz_name(a->viz) }, { "Enter", viz_get_auto(a->viz) ? "auto: on" : "auto: off" }, { viz_is_bend(a->viz) ? "g" : "t", viz_is_bend(a->viz) ? "GPU / CPU" : "scroller" },
            { "f", "full screen" }, { "Space", "pause" }, { "n", "next" }, { "p", "previous" }, { "\xE2\x86\x90 \xE2\x86\x92", "views" },
        };
        draw_keys(a, keys, sizeof keys / sizeof *keys, u, x, oh - 16 * u, kw, ink, faint);
    } else {
        static const char *const keys[][2] = {
            { "/", "search" }, { "Enter", "play" }, { "r", "radio" }, { "l", "like" }, { "Space", "pause" },
            { "n", "next" }, { "p", "previous" }, { "\xE2\x86\x90 \xE2\x86\x92", "views" }, { "Ctrl+V", "link" },
            { "- +", "volume" }, { "s", "sign in" }, { "Alt+Enter", "full screen" }, { "F1", "stats" },
        };
        draw_keys(a, keys, sizeof keys / sizeof *keys, u, x, oh - 16 * u, kw, ink, faint);
    }

drawn:
    gs_stats_frame_end(&a->stats);
    if (a->show_stats) {
        char pacing[400];
        gs_pace_describe(&a->pace, pacing, sizeof pacing);
        char bend[240];
        if (viz_bend_stats(a->viz, bend, sizeof bend)) SDL_strlcat(pacing, "\n", sizeof pacing), SDL_strlcat(pacing, bend, sizeof pacing);
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
    if (a->persist && a->win) state_save_window(a->dir, &a->window);
    if (a->persist && a->player) save_queue(a);
    gs_mix_close();
    signin_close(a->signin);
    player_free(a->player);
    if (a->jobs) gs_jobs_wait(a->jobs), gs_jobs_free(a->jobs);
    library_free(&a->library);
    viz_free(a->viz);
    gs_fontset_free(a->fonts);
    gs_glyphs_free(a->glyphs);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_free(a);
}
