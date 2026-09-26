// A native YouTube Music mini player on gesso (in progress). For now:
//   gtube --tools [--data DIR]              get or update our own yt-dlp and JavaScript runtime
//   gtube --probe URL [--data DIR]          what yt-dlp says about a track
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

#include "tools.h"

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
    static tools t;
    *state = &t;
    const char *data = NULL, *probe = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) probe = argv[++i];
    }
    char *pref = data ? NULL : SDL_GetPrefPath("wakamex", "gesso-gtube");
    char dir[1024];
#ifdef _WIN32
    const char *slash = "\\";
#else
    const char *slash = "/";
#endif
    snprintf(dir, sizeof dir, "%s%s", data ? data : pref, data && data[strlen(data) - 1] != '/' && data[strlen(data) - 1] != '\\' ? slash : "");
    SDL_free(pref);
    tools_init(&t, dir);
    uint64_t start = SDL_GetTicks();
    tools_prepare(&t);
    printf("state %d (%s) in %.1f s\nyt-dlp %s\njs %s\n", SDL_GetAtomicInt(&t.state), t.status, (SDL_GetTicks() - start) / 1000.0, t.ytdlp, t.js);
    if (SDL_GetAtomicInt(&t.state) != 1) return SDL_APP_FAILURE;
    if (probe) {
        const char *args[] = { "-f", "251", "--print", "%(title)s | %(artist,uploader)s | format %(format_id)s %(ext)s %(acodec)s %(abr)s kbit/s | %(duration)s s", probe, NULL };
        SDL_Process *p = tools_ytdlp(&t, args);
        size_t n;
        int code;
        char *out = p ? SDL_ReadProcess(p, &n, &code) : NULL;
        printf("probe (exit %d): %s", p ? code : -1, out ? out : "\n");
        SDL_free(out);
        SDL_DestroyProcess(p);
    }
    return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void *s, SDL_Event *e) { return SDL_APP_CONTINUE; }
SDL_AppResult SDL_AppIterate(void *s) { return SDL_APP_SUCCESS; }
void SDL_AppQuit(void *s, SDL_AppResult r) {}
