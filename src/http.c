#include "http.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"

#ifdef _WIN32
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

// ---- The curl program ----

static int with_program(const http_request *r, char **body, size_t *len) {
    char curl[512];
    tools_program("curl", curl, sizeof curl);
    const char *args[48];
    int n = 0;
    args[n++] = curl, args[n++] = "-sS";
    if (r->follow) args[n++] = "-L";
    if (r->compressed) args[n++] = "--compressed";
    if (r->agent) args[n++] = "-A", args[n++] = r->agent;
    for (const char *const *h = r->headers; h && *h && n < 36; h++) args[n++] = "-H", args[n++] = *h;
    if (r->cookies) args[n++] = "-b", args[n++] = r->cookies;
    if (r->cookies && r->save_cookies) args[n++] = "-c", args[n++] = r->cookies;
    if (r->body) args[n++] = "--data-binary", args[n++] = r->body;
    // The body goes to stdout or a file, with the status after it.
    args[n++] = "-o", args[n++] = r->to ? r->to : body ? "-" : NULL_DEVICE;
    args[n++] = "-w", args[n++] = "\n%{http_code}";
    args[n++] = r->url, args[n] = NULL;

    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process *proc = tools_spawn(p);
    SDL_DestroyProperties(p);
    size_t got = 0;
    int code = -1;
    char *out = proc ? SDL_ReadProcess(proc, &got, &code) : NULL;
    SDL_DestroyProcess(proc);
    char *last = out ? strrchr(out, '\n') : NULL;
    int status = last && code == 0 ? atoi(last + 1) : 0;
    if (body && status) *last = 0, *body = out, *len = (size_t)(last - out);
    else SDL_free(out);
    return status;
}

// Streaming through the program: curl writes the response headers (-D -) and then the body to its
// output, so the status is read from the headers without needing a newer curl's %{stderr}.
static int stream_with_program(const http_request *r, long long from, http_take take, void *user, SDL_AtomicInt *stop) {
    char curl[512], range[32];
    tools_program("curl", curl, sizeof curl);
    const char *args[24];
    int n = 0;
    args[n++] = curl, args[n++] = "-sS", args[n++] = "-L", args[n++] = "-D", args[n++] = "-";
    if (r->agent) args[n++] = "-A", args[n++] = r->agent;
    if (from > 0) snprintf(range, sizeof range, "%lld-", from), args[n++] = "-r", args[n++] = range;
    args[n++] = "-o", args[n++] = "-", args[n++] = r->url, args[n] = NULL;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process *proc = tools_spawn(p);
    SDL_DestroyProperties(p);
    if (!proc) return 0;
    SDL_IOStream *out = SDL_GetProcessOutput(proc);
    char head[8192];  // the headers of each response, a redirect's then the final one's
    size_t nhead = 0;
    int status = 0;
    bool body = false, more = true;
    uint8_t buf[65536];
    while (more && !SDL_GetAtomicInt(stop)) {
        size_t got = SDL_ReadIO(out, buf, sizeof buf);
        if (!got) {
            if (SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY) break;
            SDL_Delay(5);
            continue;
        }
        size_t at = 0;
        while (!body && at < got) {  // header lines up to the blank line ending the final response's
            if (nhead < sizeof head - 1) head[nhead++] = (char)buf[at];
            at++;
            if (nhead >= 4 && !memcmp(head + nhead - 4, "\r\n\r\n", 4)) {
                head[nhead] = 0;
                const char *sp = strchr(head, ' ');
                status = sp ? atoi(sp + 1) : 0;
                nhead = 0;
                if (status >= 200 && status < 300) body = true;
                else if (status != 100 && (status < 300 || status >= 400)) more = false;  // (a redirect's response comes next)
            }
        }
        if (more && body && at < got && !take(user, buf + at, got - at)) more = false;
    }
    SDL_KillProcess(proc, true);
    SDL_WaitProcess(proc, true, NULL);
    SDL_DestroyProcess(proc);
    return status;
}

// ---- libcurl ----

#ifndef _WIN32
#include <dlfcn.h>

// The few libcurl functions and option numbers used; both are part of its stable ABI.
enum {
    URL = 10002, HTTPHEADER = 10023, POSTFIELDS = 10015, POSTFIELDSIZE = 60, COOKIEFILE = 10031,
    COOKIEJAR = 10082, FOLLOWLOCATION = 52, ACCEPT_ENCODING = 10102, USERAGENT = 10018,
    WRITEFUNCTION = 20011, WRITEDATA = 10001, NOSIGNAL = 99, RESPONSE_CODE = 0x200002, RANGE = 10007,
    NOPROGRESS = 43, XFERINFOFUNCTION = 20219, XFERINFODATA = 10057,
};
static struct {
    int (*global_init)(long);
    void *(*easy_init)(void);
    int (*easy_setopt)(void *, int, ...);
    int (*easy_perform)(void *);
    int (*easy_getinfo)(void *, int, ...);
    void (*easy_cleanup)(void *);
    void *(*slist_append)(void *, const char *);
    void (*slist_free_all)(void *);
} lib;

// libcurl.so.4, or the same API built on GnuTLS that Ubuntu also installs (all 18.04 has).
static bool load_libcurl(void) {
    static SDL_InitState state;
    static bool ok;
    if (!SDL_ShouldInit(&state)) return ok;
    void *so = dlopen("libcurl.so.4", RTLD_NOW);
    if (!so) so = dlopen("libcurl-gnutls.so.4", RTLD_NOW);
    const struct { const char *name; void **fn; } fns[] = {
        { "curl_global_init", (void **)&lib.global_init }, { "curl_easy_init", (void **)&lib.easy_init },
        { "curl_easy_setopt", (void **)&lib.easy_setopt }, { "curl_easy_perform", (void **)&lib.easy_perform },
        { "curl_easy_getinfo", (void **)&lib.easy_getinfo }, { "curl_easy_cleanup", (void **)&lib.easy_cleanup },
        { "curl_slist_append", (void **)&lib.slist_append }, { "curl_slist_free_all", (void **)&lib.slist_free_all },
    };
    ok = so != NULL;
    for (size_t i = 0; ok && i < SDL_arraysize(fns); i++) ok = (*fns[i].fn = dlsym(so, fns[i].name)) != NULL;
    ok = ok && lib.global_init(3 /* CURL_GLOBAL_DEFAULT */) == 0;
    SDL_SetInitialized(&state, true);
    return ok;
}

typedef struct { FILE *file; char *data; size_t len, cap; } sink;

static size_t take(const char *data, size_t size, size_t count, void *user) {
    sink *s = user;
    size_t n = size * count;
    if (s->file) return fwrite(data, 1, n, s->file);
    if (s->len + n + 1 > s->cap) {
        size_t cap = (s->len + n + 1) * 2;
        char *grown = SDL_realloc(s->data, cap);
        if (!grown) return 0;
        s->data = grown, s->cap = cap;
    }
    memcpy(s->data + s->len, data, n);
    s->len += n;
    return n;
}

static int with_libcurl(const http_request *r, char **body, size_t *len) {
    void *h = lib.easy_init();
    if (!h) return 0;
    void *headers = NULL;
    for (const char *const *x = r->headers; x && *x; x++) headers = lib.slist_append(headers, *x);
    sink s = { 0 };
    if (r->to && !(s.file = fopen(r->to, "wb"))) return lib.easy_cleanup(h), 0;
    lib.easy_setopt(h, URL, r->url);
    lib.easy_setopt(h, NOSIGNAL, 1L);  // (requests run on worker threads)
    if (r->agent) lib.easy_setopt(h, USERAGENT, r->agent);
    if (headers) lib.easy_setopt(h, HTTPHEADER, headers);
    if (r->body) lib.easy_setopt(h, POSTFIELDS, r->body), lib.easy_setopt(h, POSTFIELDSIZE, (long)strlen(r->body));
    if (r->cookies) lib.easy_setopt(h, COOKIEFILE, r->cookies);
    if (r->cookies && r->save_cookies) lib.easy_setopt(h, COOKIEJAR, r->cookies);  // (written at cleanup)
    if (r->follow) lib.easy_setopt(h, FOLLOWLOCATION, 1L);
    if (r->compressed) lib.easy_setopt(h, ACCEPT_ENCODING, "");  // (every encoding this libcurl has)
    lib.easy_setopt(h, WRITEFUNCTION, take);
    lib.easy_setopt(h, WRITEDATA, &s);
    long status = 0;
    if (lib.easy_perform(h) == 0) lib.easy_getinfo(h, RESPONSE_CODE, &status);
    lib.easy_cleanup(h);
    lib.slist_free_all(headers);
    if (s.file && fclose(s.file) != 0) status = 0;
    if (body && status) *body = s.data ? s.data : SDL_strdup(""), *len = s.len, (*body)[s.len] = 0;
    else SDL_free(s.data);
    return (int)status;
}
#endif

#ifndef _WIN32
typedef struct { void *h; http_take take; void *user; SDL_AtomicInt *stop; } stream_sink;

static size_t pass(const char *data, size_t size, size_t count, void *user) {
    stream_sink *s = user;
    long status = 0;
    lib.easy_getinfo(s->h, RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) return 0;  // (an error's body is not the stream)
    return s->take(s->user, (const uint8_t *)data, size * count) ? size * count : 0;
}

// Called about once a second at least, and as data moves; nonzero ends the transfer.
static int check_stop(void *user, long long dltotal, long long dlnow, long long ultotal, long long ulnow) {
    (void)dltotal, (void)dlnow, (void)ultotal, (void)ulnow;
    return SDL_GetAtomicInt(((stream_sink *)user)->stop);
}

static int stream_with_libcurl(const http_request *r, long long from, http_take take, void *user, SDL_AtomicInt *stop) {
    void *h = lib.easy_init();
    if (!h) return 0;
    stream_sink s = { h, take, user, stop };
    char range[32];
    lib.easy_setopt(h, URL, r->url);
    lib.easy_setopt(h, NOSIGNAL, 1L);
    lib.easy_setopt(h, FOLLOWLOCATION, 1L);
    if (r->agent) lib.easy_setopt(h, USERAGENT, r->agent);
    if (from > 0) snprintf(range, sizeof range, "%lld-", from), lib.easy_setopt(h, RANGE, range);
    lib.easy_setopt(h, WRITEFUNCTION, pass);
    lib.easy_setopt(h, WRITEDATA, &s);
    lib.easy_setopt(h, NOPROGRESS, 0L);
    lib.easy_setopt(h, XFERINFOFUNCTION, check_stop);
    lib.easy_setopt(h, XFERINFODATA, &s);
    lib.easy_perform(h);  // (a break or a stop ends it early; what arrived has gone to take)
    long status = 0;
    lib.easy_getinfo(h, RESPONSE_CODE, &status);
    lib.easy_cleanup(h);
    return (int)status;
}
#endif

int http_stream(const http_request *r, long long from, http_take take, void *user, SDL_AtomicInt *stop) {
#ifndef _WIN32
    if (load_libcurl()) return stream_with_libcurl(r, from, take, user, stop);
#endif
    return stream_with_program(r, from, take, user, stop);
}

int http_fetch(const http_request *r, char **body, size_t *len) {
    size_t ignored;
    if (!len) len = &ignored;
#ifndef _WIN32
    if (load_libcurl()) return with_libcurl(r, body, len);
#endif
    return with_program(r, body, len);
}
