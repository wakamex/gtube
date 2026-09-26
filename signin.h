// Signing in to YouTube once, in a small window showing Google's own sign-in page (WebView2, the
// Edge engine every Windows 10 and 11 install has). When the page lands on YouTube Music signed in,
// its cookies are handed to the account and the window closes; the browser engine is not used again
// (account.h keeps the session alive over plain HTTPS). Windows only for now; elsewhere, import a
// cookies.txt with --import-cookies.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct signin signin;

// Opens the window; NULL (with a reason in `why`) if this system cannot.
signin *signin_open(const char *data_dir, char *why, size_t size);
void signin_event(signin *s, const SDL_Event *e);  // pass every event: resizing and closing
// 0 still signing in; 1 signed in (*jar is the session as a Netscape cookie file, SDL_free it);
// -1 closed or failed.
int signin_poll(signin *s, char **jar);
void signin_close(signin *s);
