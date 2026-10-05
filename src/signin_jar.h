// The sign-in page, and the session as a Netscape cookie file, shared by each system's sign-in window
// (signin.c for Windows and Linux, signin_mac.m for macOS).
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SIGNIN_URL "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F"

// ---- The cookies, as a Netscape file ----

typedef struct { char *text; size_t len, cap; bool youtube_session; } jar;

static inline jar jar_new(void) {
    jar j = { SDL_malloc(65536), 0, 65536, false };
    j.len = (size_t)snprintf(j.text, j.cap, "# Netscape HTTP Cookie File\n");
    return j;
}

// Adds a cookie; `expires` 0 for a session cookie. The account keeps only the session's domains.
static inline void jar_add(jar *j, const char *domain, const char *path, bool secure, bool http_only, long long expires,
                    const char *name, const char *value) {
    if (!name[0]) return;
    if (strstr(domain, "youtube.com") && (!strcmp(name, "SAPISID") || !strcmp(name, "__Secure-3PAPISID"))) j->youtube_session = true;
    char line[5200];
    int n = snprintf(line, sizeof line, "%s%s\t%s\t%s\t%s\t%lld\t%s\t%s\n", http_only ? "#HttpOnly_" : "", domain,
                     domain[0] == '.' ? "TRUE" : "FALSE", path[0] ? path : "/", secure ? "TRUE" : "FALSE", expires, name, value);
    if (n <= 0 || n >= (int)sizeof line) return;
    if (j->len + (size_t)n + 1 > j->cap) j->text = SDL_realloc(j->text, j->cap = (j->cap + (size_t)n) * 2);
    memcpy(j->text + j->len, line, (size_t)n + 1);
    j->len += (size_t)n;
}
