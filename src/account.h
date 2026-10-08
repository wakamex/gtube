// The signed-in YouTube session: Google's cookies, captured once by signing in (signin.h) and kept
// alive without a browser by refreshing them over plain HTTPS (gs_http.h): a POST to
// accounts.google.com/RotateCookies renews the short-lived session cookies, and a passive YouTube
// sign-in redirect copies the renewed session to youtube.com. Stored encrypted for the user with
// DPAPI on Windows (a plain file readable only by the user elsewhere). yt-dlp gets a throwaway
// copy per run, because it rewrites its cookie file on exit.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct {
    char dir[1100];
    SDL_Mutex *lock;
    char *jar;              // Netscape cookie file text, or NULL when signed out
    SDL_AtomicInt refreshing;
    uint64_t last_refresh;  // SDL_GetTicks() of the last refresh
    char status[160];
} account;

void account_init(account *a, const char *data_dir);
bool account_signed_in(account *a);
bool account_store(account *a, const char *jar);  // saves (encrypted) and uses a new session
void account_sign_out(account *a);
bool account_import(account *a, const char *netscape_file);  // a cookies.txt exported elsewhere

// A temporary plaintext copy of the cookies for one run of yt-dlp or curl; delete it with
// account_jar_done. False when signed out.
bool account_jar_file(account *a, char *path, size_t size);
void account_jar_done(const char *path);

// A youtube.com cookie's value (such as SAPISID, for signing API requests); false if absent.
bool account_cookie(account *a, const char *name, char *out, size_t size);

// Renews the session (blocking; run it as a job). False, and signed out, if Google refused it.
bool account_refresh(account *a);
void account_refresh_job(void *a);  // a gs_job_fn that skips if a refresh is already running

// Whether yt-dlp's error output means YouTube wants a signed-in session.
bool account_needed(const char *ytdlp_errors);
