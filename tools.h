// The player's own yt-dlp and JavaScript runtime, as YTubic manages them: downloaded on first run
// from their GitHub releases with the system's curl and checked against the published SHA-256
// checksums, yt-dlp updated at most once a day with its own -U. A JavaScript runtime already on the
// PATH (Deno, Node or Bun) is used when there is one; otherwise Deno is downloaded the same way.
// yt-dlp always runs with a PATH of system folders only, so no stray PATH entry can break it.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct {
    char dir[1024];     // the app's data folder (tools go in bin/ inside it)
    char ytdlp[1200];   // yt-dlp, once ready
    char js[1300];      // yt-dlp's --js-runtimes value, such as "deno:C:\...\deno.exe"
    char status[256];   // what it is doing, for the display and logs
    SDL_AtomicInt state;  // 0 working, 1 ready, -1 failed (status says why)
} tools;

// Every program the app starts goes through here. On Windows a child inherits every inheritable
// handle open at that moment, including another thread's half-made pipes, which then never report
// end of output; starting one process at a time closes that window.
SDL_Process *tools_spawn(SDL_PropertiesID props);

void tools_init(tools *t, const char *dir);
void tools_prepare(void *t);  // blocking; a gs_job_fn
// Starts yt-dlp with `args` (NULL-terminated, without the program), signed in with a Netscape
// `cookies` file if not NULL. Its output and error output are piped to the caller.
SDL_Process *tools_ytdlp(tools *t, const char *const *args, const char *cookies);
// Appends what yt-dlp has written to its error output so far to out[used..], which stays a string
// (the rest is drained and dropped when full, so the pipe never blocks yt-dlp). Returns the new length.
size_t tools_errors(SDL_Process *proc, char *out, size_t size, size_t used);
