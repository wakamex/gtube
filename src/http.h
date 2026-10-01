// HTTPS requests through curl: libcurl, loaded at run time, where the system has it (Ubuntu, Debian,
// Fedora, Arch, openSUSE and Mint desktops all do, though Ubuntu's has no curl program), and
// otherwise the curl program (Windows 10 and 11 ship one in System32).
#pragma once
#include <stdbool.h>
#include <stddef.h>

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
