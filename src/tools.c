#include "tools.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "gs_sha256.h"

#ifdef _WIN32
#define EXE ".exe"
#define SLASH "\\"
#define PATHSEP ';'
#define YTDLP_ASSET "yt-dlp.exe"
#define DENO_TARGET "x86_64-pc-windows-msvc"
#elif defined(__APPLE__)
#define EXE ""
#define SLASH "/"
#define PATHSEP ':'
#define YTDLP_ASSET "yt-dlp_macos"
#define DENO_TARGET "aarch64-apple-darwin"
#else
#define EXE ""
#define SLASH "/"
#define PATHSEP ':'
#define YTDLP_ASSET "yt-dlp_linux"
#define DENO_TARGET "x86_64-unknown-linux-gnu"
#endif
#define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/"
#define DENO_URL "https://github.com/denoland/deno/releases/latest/download/deno-" DENO_TARGET ".zip"
#define UPDATE_SECONDS (24 * 3600)

static void say(tools *t, const char *msg) {
    SDL_strlcpy(t->status, msg, sizeof t->status);
    SDL_Log("tools: %s", msg);
}

static bool fail(tools *t, const char *msg) {
    say(t, msg);
    SDL_SetAtomicInt(&t->state, -1);
    return false;
}

void tools_init(tools *t, const char *dir) {
    memset(t, 0, sizeof *t);
    SDL_strlcpy(t->dir, dir, sizeof t->dir);
}

// The PATH variable, whatever its case (Windows calls it "Path"; SDL looks names up case-sensitively).
static const char *path_variable(SDL_Environment *env, char *name, size_t size) {
    char **vars = SDL_GetEnvironmentVariables(env);
    const char *found = NULL;
    for (int i = 0; vars && vars[i]; i++)
        if (!SDL_strncasecmp(vars[i], "path=", 5)) {
            snprintf(name, size, "%.4s", vars[i]);
            found = SDL_GetEnvironmentVariable(env, name);
        }
    SDL_free(vars);
    return found;
}

// System folders only: enough for curl, tar and yt-dlp's own needs.
static SDL_Environment *trimmed_env(void) {
    SDL_Environment *env = SDL_CreateEnvironment(true);
    char name[8];
    while (path_variable(env, name, sizeof name)) SDL_UnsetEnvironmentVariable(env, name);
#ifdef _WIN32
    const char *root = SDL_getenv("SystemRoot");
    char path[512];
    snprintf(path, sizeof path, "%s\\System32;%s", root ? root : "C:\\Windows", root ? root : "C:\\Windows");
    SDL_SetEnvironmentVariable(env, "PATH", path, true);
#else
    SDL_SetEnvironmentVariable(env, "PATH", "/usr/local/bin:/usr/bin:/bin", true);
#endif
    return env;
}

// Never set SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING: the Linux release runs on glibc 2.27
// and 2.28, which cannot start a process in another directory (see src/glibc_compat.c).
SDL_Process *tools_spawn(SDL_PropertiesID props) {
    static SDL_SpinLock lock;
    SDL_LockSpinlock(&lock);
    SDL_Process *proc = SDL_CreateProcessWithProperties(props);
    SDL_UnlockSpinlock(&lock);
    return proc;
}

static SDL_Process *start(const char *const *args, bool pipe, bool errors) {
    SDL_Environment *env = trimmed_env();
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, pipe ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, errors ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_INHERITED);
    SDL_Process *proc = tools_spawn(p);
    SDL_DestroyProperties(p);
    SDL_DestroyEnvironment(env);
    return proc;
}

// Runs a program to completion; its output (if wanted) is returned malloc'd. True on exit code 0.
static bool run(const char *const *args, char **out) {
    SDL_Process *proc = start(args, true, false);
    if (!proc) return false;
    size_t n;
    int code = -1;
    char *data = SDL_ReadProcess(proc, &n, &code);
    SDL_DestroyProcess(proc);
    if (out) *out = data; else SDL_free(data);
    return code == 0;
}

static bool exists(const char *path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

void tools_program(const char *name, char *out, size_t size) {
#ifdef _WIN32
    const char *root = SDL_getenv("SystemRoot");
    snprintf(out, size, "%s\\System32\\%s.exe", root ? root : "C:\\Windows", name);
#else
    static const char *const dirs[] = { "/usr/local/bin", "/usr/bin", "/bin" };
    for (int i = 0; i < 3; i++) {
        snprintf(out, size, "%s/%s", dirs[i], name);
        if (exists(out)) return;
    }
    snprintf(out, size, "%s", name);
#endif
}

// (for tools_prepare's own steps, which run one at a time)
static const char *system_program(const char *name) {
    static char p[4][512];
    static int k;
    char *out = p[k++ % 4];
    tools_program(name, out, 512);
    return out;
}

static bool download(const char *url, const char *to) {
    const char *args[] = { system_program("curl"), "-fsSL", "--retry", "2", "-o", to, url, NULL };
    return run(args, NULL);
}

// The first 64-hex-digit token in text that follows `name` on its line (or anywhere, if name is NULL).
static bool find_hash(const char *text, const char *name, char hex[65]) {
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[512];
        snprintf(buf, sizeof buf, "%.*s", (int)(len < sizeof buf - 1 ? len : sizeof buf - 1), line);
        if (!name || (strstr(buf, name) && (strlen(buf) >= strlen(name)) && !strcmp(buf + strlen(buf) - strlen(name), name))) {
            for (char *p = buf; *p; p++) {
                int k = 0;
                while (k < 64 && SDL_isxdigit((unsigned char)p[k])) k++;
                if (k == 64 && !SDL_isxdigit((unsigned char)p[64])) {
                    for (int i = 0; i < 64; i++) hex[i] = (char)SDL_tolower((unsigned char)p[i]);
                    hex[64] = 0;
                    return true;
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

// Downloads `url` to `to`, checked against the checksum in `sums_url` for `name` (NULL: the only one).
static bool fetch_checked(tools *t, const char *url, const char *sums_url, const char *name, const char *to) {
    char tmp[1300], sums_path[1300], want[65], got[65];
    snprintf(tmp, sizeof tmp, "%s.download", to);
    snprintf(sums_path, sizeof sums_path, "%s.sums", to);
    if (!download(sums_url, sums_path)) return fail(t, "could not download the checksums (is there a connection?)");
    char *sums = SDL_LoadFile(sums_path, NULL);
    SDL_RemovePath(sums_path);
    bool found = sums && find_hash(sums, name, want);
    SDL_free(sums);
    if (!found) return fail(t, "the release publishes no checksum for this file");
    if (!download(url, tmp)) return fail(t, "download failed");
    if (!gs_sha256_file(tmp, got) || strcmp(got, want)) {
        SDL_RemovePath(tmp);
        return fail(t, "the download does not match its published checksum; not using it");
    }
    SDL_RemovePath(to);
    if (!SDL_RenamePath(tmp, to)) return fail(t, "could not move the download into place");
    return true;
}

static void make_executable(const char *path) {
#ifndef _WIN32
    chmod(path, 0755);
#else
    (void)path;
#endif
}

// A program on the PATH (the user's, not the trimmed one), or false.
static bool on_path(const char *name, char *out, size_t size) {
    char var[8];
    const char *path = path_variable(SDL_GetEnvironment(), var, sizeof var);
    if (!path) return false;
    while (*path) {
        const char *end = strchr(path, PATHSEP);
        size_t len = end ? (size_t)(end - path) : strlen(path);
        size_t dir = len;
        while (dir && (path[dir - 1] == '/' || path[dir - 1] == '\\')) dir--;  // "C:\\Program Files\\nodejs\\"
        if (dir) {
            snprintf(out, size, "%.*s" SLASH "%s%s", (int)dir, path, name, EXE);
            if (exists(out)) return true;
        }
        path = end ? end + 1 : path + len;
    }
    return false;
}

static double now_seconds(void) {
    SDL_Time t;
    return SDL_GetCurrentTime(&t) ? t / 1e9 : 0;
}

void tools_prepare(void *arg) {
    tools *t = arg;
    char bin[1100], stamp[1200];
    snprintf(bin, sizeof bin, "%sbin", t->dir);
    SDL_CreateDirectory(bin);
    snprintf(t->ytdlp, sizeof t->ytdlp, "%s" SLASH "yt-dlp%s", bin, EXE);
    snprintf(stamp, sizeof stamp, "%s" SLASH "last-update", bin);

    // yt-dlp: our own copy, checked; then kept current.
    if (!exists(t->ytdlp)) {
        say(t, "downloading yt-dlp");
        if (!fetch_checked(t, YTDLP_URL YTDLP_ASSET, YTDLP_URL "SHA2-256SUMS", YTDLP_ASSET, t->ytdlp)) return;
        make_executable(t->ytdlp);
        char when[32];
        snprintf(when, sizeof when, "%.0f", now_seconds());
        SDL_SaveFile(stamp, when, strlen(when));
    } else {
        char *last = SDL_LoadFile(stamp, NULL);
        double age = now_seconds() - (last ? SDL_atof(last) : 0);
        SDL_free(last);
        if (age > UPDATE_SECONDS) {
            say(t, "updating yt-dlp");
            const char *args[] = { t->ytdlp, "-U", NULL };
            char *out = NULL;
            bool ok = run(args, &out);
            SDL_Log("tools: yt-dlp -U %s: %s", ok ? "ran" : "failed", out ? out : "");
            SDL_free(out);
            char when[32];
            snprintf(when, sizeof when, "%.0f", now_seconds());
            SDL_SaveFile(stamp, when, strlen(when));
        }
    }

    // A JavaScript runtime for yt-dlp: our Deno, else one on the PATH, else download Deno.
    char path[1100], deno[1200];
    snprintf(deno, sizeof deno, "%s" SLASH "deno%s", bin, EXE);
    if (exists(deno)) snprintf(t->js, sizeof t->js, "deno:%s", deno);
    else if (on_path("deno", path, sizeof path)) snprintf(t->js, sizeof t->js, "deno:%s", path);
    else if (on_path("node", path, sizeof path)) snprintf(t->js, sizeof t->js, "node:%s", path);
    else if (on_path("bun", path, sizeof path)) snprintf(t->js, sizeof t->js, "bun:%s", path);
    else {
        say(t, "downloading Deno (yt-dlp's JavaScript runtime)");
        char zip[1200];
        snprintf(zip, sizeof zip, "%s" SLASH "deno.zip", bin);
        if (!fetch_checked(t, DENO_URL, DENO_URL ".sha256sum", NULL, zip)) return;
#ifdef _WIN32
        const char *args[] = { system_program("tar"), "-xf", zip, "-C", bin, NULL };  // Windows 10's tar reads zip
#else
        const char *args[] = { system_program("unzip"), "-o", "-q", zip, "-d", bin, NULL };
#endif
        bool ok = run(args, NULL);
        SDL_RemovePath(zip);
        if (!ok || !exists(deno)) {
            fail(t, "could not unpack Deno");
            return;
        }
        make_executable(deno);
        snprintf(t->js, sizeof t->js, "deno:%s", deno);
    }
    say(t, "ready");
    SDL_SetAtomicInt(&t->state, 1);
}

SDL_Process *tools_ytdlp(tools *t, const char *const *args, const char *cookies) {
    const char *all[64];
    int n = 0;
    all[n++] = t->ytdlp;
    all[n++] = "--js-runtimes";
    all[n++] = t->js;
    all[n++] = "--no-warnings";
    if (cookies) all[n++] = "--cookies", all[n++] = cookies;
    for (int i = 0; args[i] && n < 63; i++) all[n++] = args[i];
    all[n] = NULL;
    return start(all, true, true);
}

size_t tools_errors(SDL_Process *proc, char *out, size_t size, size_t used) {
    SDL_IOStream *err = proc ? SDL_GetPointerProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_STDERR_POINTER, NULL) : NULL;
    char scratch[4096];
    for (size_t n; err && (n = SDL_ReadIO(err, used + 1 < size ? out + used : scratch, used + 1 < size ? size - 1 - used : sizeof scratch));)
        if (used + 1 < size) used += n;
    if (size) out[used] = 0;
    return used;
}
