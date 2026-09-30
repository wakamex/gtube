// Signing in to YouTube once, in a small window showing Google's own sign-in page: WebView2 (the
// Edge engine every Windows 10 and 11 install has) on Windows, WebKitGTK (GNOME's browser engine)
// elsewhere. When the page lands on YouTube Music signed in, its cookies are handed to the account
// and the window closes; the browser engine is not used again (account.h keeps the session alive
// over plain HTTPS). Without either engine, import a cookies.txt with --import-cookies.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct signin signin;

// Opens the window; NULL (with a reason in `why`) if this system cannot.
signin *signin_open(const char *data_dir, char *why, size_t size);
void signin_event(signin *s, const SDL_Event *e);  // pass every event: resizing and closing
// 0 still signing in; 1 signed in (*jar is the session as a Netscape cookie file, SDL_free it);
// -1 closed or failed, with what happened in `why`.
int signin_poll(signin *s, char **jar, char *why, size_t size);
void signin_close(signin *s);

#ifndef _WIN32
// The window's own process, gtube --signin-window FILE, so GTK never loads into the player's:
// exits 0 with the session in FILE, 1 when the window is closed first, 2 with a reason on stdout.
int signin_window(const char *file);
#endif
