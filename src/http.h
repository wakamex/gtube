// HTTPS requests through curl: libcurl, loaded at run time, where the system has it (Ubuntu, Debian,
// Fedora, Arch, openSUSE and Mint desktops all do, though Ubuntu's has no curl program), and
// otherwise the curl program (Windows 10 and 11 ship one in System32).
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *url;
    const char *agent;             // the User-Agent, or NULL for curl's
    const char *const *headers;    // more "Name: value" headers, NULL-terminated, or NULL
    const char *body;              // POST data, or NULL for a GET
    const char *cookies;           // a Netscape cookie file whose cookies are sent, or NULL
    bool save_cookies;             // and the cookies the server sets saved back to it
    bool follow;                   // follow redirects
    bool compressed;               // accept a compressed response
    const char *to;                // save the response's body to this file
} http_request;

// The response's HTTP status, or 0 when there was no response. Its body goes to r->to, or into
// *body (SDL_free it; zero-terminated, *len bytes) when body is not NULL, or is dropped.
int http_fetch(const http_request *r, char **body, size_t *len);

// Streams a GET's body to `take` as it arrives, from byte `from` on (a range request) when from > 0.
// `take` returns false to end the transfer, and so does setting *stop, which is checked at least
// every 100 ms while waiting on the network. Returns the HTTP status, or 0 when there was no
// response; the body stops short of the whole when the connection breaks, which only `take` sees.
typedef bool (*http_take)(void *user, const uint8_t *data, size_t len);
int http_stream(const http_request *r, long long from, http_take take, void *user, SDL_AtomicInt *stop);
