#include "tools.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "gs_sha256.h"
#include "gs_http.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _WIN32
#define EXE ".exe"
#define SLASH "\\"
#define PATHSEP ';'
#define YTDLP_ASSET "yt-dlp_win.zip"
#define YTDLP_EXE "yt-dlp.exe"
#define DENO_TARGET "x86_64-pc-windows-msvc"
#elif defined(__APPLE__)
#define EXE ""
#define SLASH "/"
#define PATHSEP ':'
#define YTDLP_ASSET "yt-dlp_macos.zip"
#define YTDLP_EXE "yt-dlp_macos"
#define DENO_TARGET "aarch64-apple-darwin"
#else
#define EXE ""
#define SLASH "/"
#define PATHSEP ':'
#define YTDLP_ASSET "yt-dlp_linux.zip"
#define YTDLP_EXE "yt-dlp_linux"
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
// Started one at a time with gs_http's own curl processes (gs_http_spawn), so none inherits another's pipes.
SDL_Process *tools_spawn(SDL_PropertiesID props) { return gs_http_spawn(props); }

#ifdef _WIN32
#define HANDLE_PROP "gtube.process.handle"

static void SDLCALL close_handle(void *user, void *value) {
    (void)user;
    CloseHandle(value);
}

// Every program the app starts joins one job, which Windows ends when the app's last handle to it
// closes: when the app exits, however it exits (Task Manager included), they end with it.
static HANDLE children(void) {
    static HANDLE job;
    static SDL_InitState once;
    if (SDL_ShouldInit(&once)) {
        job = CreateJobObjectW(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = { 0 };
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit)) CloseHandle(job), job = NULL;
        SDL_SetInitialized(&once, true);
    }
    return job;
}
#endif

// yt-dlp, Deno and tar are console programs: on Windows each runs without a window of its own (SDL's
// background flag, CREATE_NO_WINDOW), joins the app's job, and keeps a handle of the app's own
// from which tools_exit_code reads its exit code, since SDL reports 0 for a background process.
static SDL_Process *start(const char *const *args, bool pipe, bool errors) {
    SDL_Environment *env = trimmed_env();
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, pipe ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, errors ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_INHERITED);
#ifdef _WIN32
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    SDL_Process *proc = tools_spawn(p);
    SDL_DestroyProperties(p);
    SDL_DestroyEnvironment(env);
#ifdef _WIN32
    if (proc) {
        DWORD pid = (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_PID_NUMBER, 0);
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
        if (h) {
            if (children()) AssignProcessToJobObject(children(), h);
            SDL_SetPointerPropertyWithCleanup(SDL_GetProcessProperties(proc), HANDLE_PROP, h, close_handle, NULL);
        }
    }
#endif
    return proc;
}

int tools_exit_code(SDL_Process *proc, int code) {
#ifdef _WIN32
    HANDLE h = proc ? SDL_GetPointerProperty(SDL_GetProcessProperties(proc), HANDLE_PROP, NULL) : NULL;
    DWORD real;
    if (h && GetExitCodeProcess(h, &real) && real != STILL_ACTIVE) return (int)real;
#else
    (void)proc;
#endif
    return code;
}

// Runs a program to completion; its output (if wanted) is returned malloc'd. True on exit code 0.
static bool run(const char *const *args, char **out) {
    SDL_Process *proc = start(args, true, false);
    if (!proc) return false;
    size_t n;
    int code = -1;
    char *data = SDL_ReadProcess(proc, &n, &code);
    code = tools_exit_code(proc, code);
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
    gs_http_request r = { .url = url, .follow = true, .to = to };
    for (int attempt = 0; attempt < 3; attempt++)
        if (gs_http_fetch(&r, NULL, NULL) == 200) return true;
    SDL_RemovePath(to);
    return false;
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

// The checksum that `sums_url` publishes for `name` (NULL: the only one), or an error message.
static const char *published_hash(const char *sums_url, const char *name, const char *scratch, char want[65]) {
    if (!download(sums_url, scratch)) return "could not download the checksums (is there a connection?)";
    char *sums = SDL_LoadFile(scratch, NULL);
    SDL_RemovePath(scratch);
    bool found = sums && find_hash(sums, name, want);
    SDL_free(sums);
    return found ? NULL : "the release publishes no checksum for this file";
}

// Downloads `url` to `to` if it matches the checksum `want`; NULL, or an error message.
static const char *download_checked(const char *url, const char *want, const char *to) {
    char tmp[1300], got[65];
    snprintf(tmp, sizeof tmp, "%s.download", to);
    if (!download(url, tmp)) return "download failed";
    if (!gs_sha256_file(tmp, got) || strcmp(got, want)) {
        SDL_RemovePath(tmp);
        return "the download does not match its published checksum; not using it";
    }
    SDL_RemovePath(to);
    return SDL_RenamePath(tmp, to) ? NULL : "could not move the download into place";
}

// Unpacks a zip into dir, with the system's own tar (Windows 10 and later read zip with it) or unzip.
static bool unzip(const char *zip, const char *dir) {
    SDL_CreateDirectory(dir);
#ifdef _WIN32
    const char *args[] = { system_program("tar"), "-xf", zip, "-C", dir, NULL };
#else
    const char *args[] = { system_program("unzip"), "-o", "-q", zip, "-d", dir, NULL };
#endif
    return run(args, NULL);
}

static SDL_EnumerationResult SDLCALL remove_one(void *user, const char *dir, const char *name) {
    (void)user;
    char path[1400];
    SDL_PathInfo info;
    snprintf(path, sizeof path, "%s%s", dir, name);
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path, remove_one, NULL);
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

// Removes a file, or a folder and everything in it.
static void remove_all(const char *path) {
    SDL_EnumerateDirectory(path, remove_one, NULL);
    SDL_RemovePath(path);
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

// yt-dlp is kept unpacked, in bin/yt-dlp-<the first 12 digits of its zip's SHA-256>, and
// bin/yt-dlp.txt names the copy in use and when it was last checked ("<unix time> <SHA-256>").
// The unpacked build starts in about 0.4 s; the single-file build takes about 1.1 s, unpacking
// itself into a temporary folder on every run. Unpacked builds cannot update themselves (-U), so a
// newer release goes into a folder of its own, and copies no longer in use are removed at launch.
static void ytdlp_folder(const char *bin, const char *hash, char *out, size_t size) {
    snprintf(out, size, "%s" SLASH "yt-dlp-%.12s", bin, hash);
}

static void use_ytdlp(tools *t, const char *bin, const char *hash) {
    char dir[1200];
    ytdlp_folder(bin, hash, dir, sizeof dir);
    SDL_LockSpinlock(&t->lock);
    snprintf(t->ytdlp, sizeof t->ytdlp, "%s" SLASH YTDLP_EXE, dir);
    SDL_UnlockSpinlock(&t->lock);
}

static void stamp_ytdlp(const char *bin, const char *hash) {
    char stamp[1200], text[100];
    snprintf(stamp, sizeof stamp, "%s" SLASH "yt-dlp.txt", bin);
    snprintf(text, sizeof text, "%.0f %s", now_seconds(), hash);
    SDL_SaveFile(stamp, text, strlen(text));
}

// Installs the latest yt-dlp unless the copy with checksum `have` (or "") is it. NULL, or an error.
static const char *install_ytdlp(tools *t, const char *bin, const char *have) {
    char scratch[1200], zip[1200], dir[1200], part[1300], exe[1400], want[65];
    snprintf(scratch, sizeof scratch, "%s" SLASH "yt-dlp.sums", bin);
    const char *err = published_hash(YTDLP_URL "SHA2-256SUMS", YTDLP_ASSET, scratch, want);
    if (err) return err;
    if (!strcmp(want, have)) return stamp_ytdlp(bin, have), NULL;
    say(t, have[0] ? "updating yt-dlp" : "downloading yt-dlp");
    snprintf(zip, sizeof zip, "%s" SLASH "yt-dlp.zip", bin);
    if ((err = download_checked(YTDLP_URL YTDLP_ASSET, want, zip))) return err;
    ytdlp_folder(bin, want, dir, sizeof dir);
    snprintf(part, sizeof part, "%s.part", dir);
    remove_all(part);
    bool ok = unzip(zip, part);
    SDL_RemovePath(zip);
    snprintf(exe, sizeof exe, "%s" SLASH YTDLP_EXE, part);
    if (!ok || !exists(exe)) return remove_all(part), "could not unpack yt-dlp";
    make_executable(exe);
    remove_all(dir);
    if (!SDL_RenamePath(part, dir)) return remove_all(part), "could not move yt-dlp into place";
    use_ytdlp(t, bin, want);
    stamp_ytdlp(bin, want);
    SDL_Log("tools: yt-dlp %.12s installed", want);
    return NULL;
}

typedef struct { const char *bin, *keep; } sweep_ctx;

// Removes yt-dlp copies other than the one in use, and the single-file build used before.
static SDL_EnumerationResult SDLCALL sweep_one(void *user, const char *dir, const char *name) {
    sweep_ctx *c = user;
    char path[1300];
    snprintf(path, sizeof path, "%s%s", dir, name);
    bool copy = !strncmp(name, "yt-dlp-", 7);
    if ((copy && (strlen(name) != 19 || strncmp(name + 7, c->keep, 12))) || !strcmp(name, "yt-dlp" EXE) || !strcmp(name, "last-update"))
        remove_all(path);
    return SDL_ENUM_CONTINUE;
}

void tools_prepare(void *arg) {
    tools *t = arg;
    char bin[1100], stamp[1200], have[65] = "", exe[1400];
    snprintf(bin, sizeof bin, "%sbin", t->dir);
    SDL_CreateDirectory(bin);
    snprintf(stamp, sizeof stamp, "%s" SLASH "yt-dlp.txt", bin);
    char *text = SDL_LoadFile(stamp, NULL);
    double checked = text ? SDL_atof(text) : 0;
    const char *space = text ? strchr(text, ' ') : NULL;
    if (space && strlen(space + 1) >= 64) snprintf(have, sizeof have, "%.64s", space + 1);
    SDL_free(text);
    if (have[0]) {
        use_ytdlp(t, bin, have);
        SDL_strlcpy(exe, t->ytdlp, sizeof exe);
        if (!exists(exe)) have[0] = 0;
    }
    sweep_ctx sweep = { bin, have };
    char bin_slash[1200];
    snprintf(bin_slash, sizeof bin_slash, "%s" SLASH, bin);
    SDL_EnumerateDirectory(bin_slash, sweep_one, &sweep);

    if (!have[0]) {  // our own copy, checked
        const char *err = install_ytdlp(t, bin, "");
        if (err) { fail(t, err); return; }
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
        char scratch[1200], want[65];
        snprintf(scratch, sizeof scratch, "%s" SLASH "deno.sums", bin);
        const char *err = published_hash(DENO_URL ".sha256sum", NULL, scratch, want);
        if (!err) err = download_checked(DENO_URL, want, zip);
        if (err) { fail(t, err); return; }
        bool ok = unzip(zip, bin);
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

    // Kept current: checked at most once a day, after the app is ready, so playing never waits on it.
    if (have[0] && now_seconds() - checked > UPDATE_SECONDS) {
        const char *err = install_ytdlp(t, bin, have);
        if (err) SDL_Log("tools: yt-dlp update: %s", err);
        SDL_strlcpy(t->status, "ready", sizeof t->status);
    }
}

SDL_Process *tools_ytdlp(tools *t, const char *const *args, const char *cookies) {
    const char *all[64];
    int n = 0;
    char exe[sizeof t->ytdlp];
    SDL_LockSpinlock(&t->lock);
    SDL_strlcpy(exe, t->ytdlp, sizeof exe);
    SDL_UnlockSpinlock(&t->lock);
    all[n++] = exe;
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
