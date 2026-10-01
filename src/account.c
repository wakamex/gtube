#include "account.h"

#include <stdio.h>
#include <string.h>

#include "tools.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>
#define SLASH "\\"
#define NULL_DEVICE "NUL"
#else
#include <sys/stat.h>
#define SLASH "/"
#define NULL_DEVICE "/dev/null"
#endif

// A current desktop browser, so Google's endpoints answer as they would to one.
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36"
#define PASSIVE_SIGNIN "https://accounts.google.com/ServiceLogin?service=youtube&passive=true&continue=" \
    "https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue%26app%3Ddesktop%26next%3D%252F"

static void store_path(account *a, char *out, size_t size) {
#ifdef _WIN32
    snprintf(out, size, "%saccount" SLASH "cookies.enc", a->dir);
#else
    snprintf(out, size, "%saccount" SLASH "cookies.txt", a->dir);
#endif
}

static void say(account *a, const char *msg) {
    SDL_LockMutex(a->lock);
    SDL_strlcpy(a->status, msg, sizeof a->status);
    SDL_UnlockMutex(a->lock);
    SDL_Log("account: %s", msg);
}

// ---- At rest: DPAPI on Windows ----

static bool save_bytes(account *a, const char *text) {
    char dir[1200], path[1200];
    snprintf(dir, sizeof dir, "%saccount", a->dir);
    SDL_CreateDirectory(dir);
    store_path(a, path, sizeof path);
#ifdef _WIN32
    DATA_BLOB in = { (DWORD)strlen(text), (BYTE *)text }, out = { 0 };
    if (!CryptProtectData(&in, L"gesso gtube session", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    bool ok = SDL_SaveFile(path, out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
#else
    bool ok = SDL_SaveFile(path, text, strlen(text));
    chmod(path, 0600);
    return ok;
#endif
}

static char *load_bytes(account *a) {
    char path[1200];
    store_path(a, path, sizeof path);
    size_t n;
    void *data = SDL_LoadFile(path, &n);
    if (!data) return NULL;
#ifdef _WIN32
    DATA_BLOB in = { (DWORD)n, data }, out = { 0 };
    bool ok = CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out);
    SDL_free(data);
    if (!ok) return NULL;
    char *text = SDL_malloc(out.cbData + 1);
    memcpy(text, out.pbData, out.cbData);
    text[out.cbData] = 0;
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return text;
#else
    return data;  // SDL_LoadFile adds a terminating zero
#endif
}

// ---- Throwaway copies for yt-dlp and curl ----

typedef struct { const char *dir; SDL_Time before; } sweep_ctx;

static SDL_EnumerationResult SDLCALL sweep_one(void *user, const char *dir, const char *name) {
    sweep_ctx *c = user;
    size_t n = strlen(name);
    if (strncmp(name, "run-", 4) || n < 8 || strcmp(name + n - 4, ".txt")) return SDL_ENUM_CONTINUE;
    char path[1300];
    SDL_PathInfo info;
    snprintf(path, sizeof path, "%s%s", dir, name);
    if (SDL_GetPathInfo(path, &info) && info.modify_time < c->before) SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

// Removes copies last written more than `age_s` ago. Each copy is deleted when its run ends, but
// yt-dlp's release build is a launcher and a child: stopping a track ends the launcher, and the
// child exits a moment later, saving the cookie file again after it was deleted. yt-dlp reads its
// copy within seconds of starting, so an old copy is never still needed.
static void sweep(account *a, double age_s) {
    char dir[1200];
    snprintf(dir, sizeof dir, "%saccount", a->dir);
    sweep_ctx c = { dir, 0 };
    SDL_GetCurrentTime(&c.before);
    c.before -= (SDL_Time)(age_s * 1e9);
    SDL_EnumerateDirectory(dir, sweep_one, &c);
}

// ---- The session ----

static bool has_session(const char *jar) {
    // YouTube's signed-in cookies, as yt-dlp needs them.
    return jar && strstr(jar, ".youtube.com") && (strstr(jar, "\tSAPISID\t") || strstr(jar, "\t__Secure-3PAPISID\t"));
}

void account_init(account *a, const char *data_dir) {
    memset(a, 0, sizeof *a);
    SDL_strlcpy(a->dir, data_dir, sizeof a->dir);
    a->lock = SDL_CreateMutex();
    sweep(a, 0);  // anything a previous run left
    char *jar = load_bytes(a);
    if (has_session(jar)) a->jar = jar, say(a, "signed in");
    else SDL_free(jar), say(a, "not signed in");
}

bool account_signed_in(account *a) {
    SDL_LockMutex(a->lock);
    bool in = a->jar != NULL;
    SDL_UnlockMutex(a->lock);
    return in;
}

// Whether a cookie's domain is google.com, youtube.com, or one of theirs (".music.youtube.com").
static bool session_domain(const char *d, size_t n) {
    static const char *const sites[] = { "google.com", "youtube.com" };
    for (size_t i = 0; i < SDL_arraysize(sites); i++) {
        size_t m = strlen(sites[i]);
        if (n >= m && !memcmp(d + n - m, sites[i], m) && (n == m || d[n - m - 1] == '.')) return true;
    }
    return false;
}

// The google.com and youtube.com cookies of a Netscape cookie file, the session's own: a browser's
// export can hold every site's.
static char *session_cookies(const char *jar) {
    static const char header[] = "# Netscape HTTP Cookie File\n";
    char *out = SDL_malloc(sizeof header + strlen(jar)), *o = out;
    memcpy(o, header, sizeof header - 1), o += sizeof header - 1;
    for (const char *line = jar; *line;) {
        size_t len = strcspn(line, "\n");
        const char *d = !strncmp(line, "#HttpOnly_", 10) ? line + 10 : line;
        size_t n = d < line + len && *d != '#' ? strcspn(d, "\t\n") : 0;
        size_t keep = len - (len && line[len - 1] == '\r');  // (a Windows export ends lines in CR LF)
        if (n && d[n] == '\t' && session_domain(d, n)) memcpy(o, line, keep), o += keep, *o++ = '\n';
        line += len + (line[len] == '\n');
    }
    *o = 0;
    return out;
}

bool account_store(account *a, const char *jar) {
    char *kept = session_cookies(jar);
    if (!has_session(kept)) return SDL_free(kept), say(a, "that sign-in has no YouTube session"), false;
    if (!save_bytes(a, kept)) return SDL_free(kept), say(a, "could not save the session"), false;
    SDL_LockMutex(a->lock);
    SDL_free(a->jar);
    a->jar = kept;
    a->last_refresh = SDL_GetTicks();
    SDL_UnlockMutex(a->lock);
    say(a, "signed in");
    return true;
}

void account_sign_out(account *a) {
    char path[1200];
    store_path(a, path, sizeof path);
    SDL_RemovePath(path);
    SDL_LockMutex(a->lock);
    SDL_free(a->jar);
    a->jar = NULL;
    SDL_UnlockMutex(a->lock);
    say(a, "signed out");
}

bool account_import(account *a, const char *file) {
    char *text = SDL_LoadFile(file, NULL);
    bool ok = text && account_store(a, text);
    SDL_free(text);
    return ok;
}

bool account_jar_file(account *a, char *path, size_t size) {
    static SDL_AtomicInt counter;
    SDL_LockMutex(a->lock);
    bool ok = a->jar != NULL;
    if (ok) {
        sweep(a, 60);
        char dir[1200];
        snprintf(dir, sizeof dir, "%saccount", a->dir);
        SDL_CreateDirectory(dir);
        snprintf(path, size, "%s" SLASH "run-%d-%d.txt", dir, (int)(SDL_GetTicks() % 100000), SDL_AddAtomicInt(&counter, 1));
        ok = SDL_SaveFile(path, a->jar, strlen(a->jar));
#ifndef _WIN32
        chmod(path, 0600);
#endif
    }
    SDL_UnlockMutex(a->lock);
    return ok;
}

void account_jar_done(const char *path) { SDL_RemovePath(path); }

bool account_needed(const char *e) {
    return e && (strstr(e, "Sign in to confirm") || strstr(e, "--cookies-from-browser or --cookies") || strstr(e, "LOGIN_REQUIRED"));
}

// ---- Keeping it alive ----

// Runs curl with a cookie jar it reads and rewrites; returns the HTTP status, or 0.
static int curl_status(const char *const *args) {
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process *proc = tools_spawn(p);
    SDL_DestroyProperties(p);
    if (!proc) return 0;
    size_t n;
    int code = -1;
    char *out = SDL_ReadProcess(proc, &n, &code);
    SDL_DestroyProcess(proc);
    int status = out && code == 0 ? SDL_atoi(out) : 0;
    SDL_free(out);
    return status;
}

// The value of a cookie in Netscape text, for noticing that a refresh renewed it.
static void cookie_value(const char *jar, const char *domain_suffix, const char *name, char *out, size_t size) {
    out[0] = 0;
    char needle[80];
    snprintf(needle, sizeof needle, "\t%s\t", name);
    for (const char *p = jar; p && (p = strstr(p, needle));) {
        const char *line = p;
        while (line > jar && line[-1] != '\n') line--;
        const char *tab = strchr(line, '\t');
        if (tab && (size_t)(tab - line) >= strlen(domain_suffix) && !strncmp(tab - strlen(domain_suffix), domain_suffix, strlen(domain_suffix))) {
            const char *v = p + strlen(needle), *e = strpbrk(v, "\r\n");
            snprintf(out, size, "%.*s", (int)(e ? e - v : (long)strlen(v)), v);
            return;
        }
        p += strlen(needle);
    }
}

bool account_cookie(account *a, const char *name, char *out, size_t size) {
    SDL_LockMutex(a->lock);
    cookie_value(a->jar, "youtube.com", name, out, size);
    SDL_UnlockMutex(a->lock);
    return out[0] != 0;
}

bool account_refresh(account *a) {
    char jar[1200], curl[512];
    tools_program("curl", curl, sizeof curl);
    if (!account_jar_file(a, jar, sizeof jar)) return false;
    char before[512], after_google[512], after_youtube[512];
    SDL_LockMutex(a->lock);
    cookie_value(a->jar, "youtube.com", "__Secure-3PSIDTS", before, sizeof before);
    SDL_UnlockMutex(a->lock);

    // 1. Renew the session's short-lived cookies at accounts.google.com.
    const char *rotate[] = { curl, "-sS", "-o", NULL_DEVICE, "-w", "%{http_code}", "-b", jar, "-c", jar, "-A", USER_AGENT,
        "-H", "Content-Type: application/json", "-H", "Origin: https://accounts.google.com",
        "--data", "[000,\"-0000000000000000000\"]", "https://accounts.google.com/RotateCookies", NULL };
    int status = curl_status(rotate);
    // 2. Carry the renewed session over to youtube.com (the redirect chain ends at accounts.youtube.com/SetSID).
    const char *carry[] = { curl, "-sS", "-L", "-o", NULL_DEVICE, "-w", "%{http_code}", "-b", jar, "-c", jar, "-A", USER_AGENT, PASSIVE_SIGNIN, NULL };
    int status2 = status == 200 ? curl_status(carry) : 0;

    char *text = SDL_LoadFile(jar, NULL);
    account_jar_done(jar);
    if (text) cookie_value(text, "google.com", "__Secure-1PSIDTS", after_google, sizeof after_google);
    if (text) cookie_value(text, "youtube.com", "__Secure-3PSIDTS", after_youtube, sizeof after_youtube);
    char msg[160];
    if (status == 401 || status == 403) {
        SDL_free(text);
        account_sign_out(a);
        say(a, "Google ended the session; sign in again");
        return false;
    }
    if (status != 200 || !has_session(text)) {
        SDL_free(text);
        snprintf(msg, sizeof msg, "could not refresh the session (HTTP %d); will try again", status);
        say(a, msg);
        return true;  // keep the old cookies: the network may just be down
    }
    save_bytes(a, text);
    SDL_LockMutex(a->lock);
    SDL_free(a->jar);
    a->jar = text;
    a->last_refresh = SDL_GetTicks();
    SDL_UnlockMutex(a->lock);
    snprintf(msg, sizeof msg, "session refreshed (google %s, youtube %s, redirect HTTP %d)", after_google[0] ? "renewed" : "no new cookie",
             strcmp(before, after_youtube) ? "renewed" : "unchanged", status2);
    say(a, msg);
    return true;
}

void account_refresh_job(void *p) {
    account *a = p;
    if (SDL_CompareAndSwapAtomicInt(&a->refreshing, 0, 1)) {
        if (account_signed_in(a)) account_refresh(a);
        SDL_SetAtomicInt(&a->refreshing, 0);
    }
}
