#define _DEFAULT_SOURCE  // readlink, which -std=c11 leaves out
#include "signin.h"

#include <stdio.h>
#include <string.h>

#define SIGNIN_URL "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F"

// ---- The cookies, as a Netscape file ----

typedef struct { char *text; size_t len, cap; bool youtube_session; } jar;

static jar jar_new(void) {
    jar j = { SDL_malloc(65536), 0, 65536, false };
    j.len = (size_t)snprintf(j.text, j.cap, "# Netscape HTTP Cookie File\n");
    return j;
}

// Adds a cookie; `expires` 0 for a session cookie. The account keeps only the session's domains.
static void jar_add(jar *j, const char *domain, const char *path, bool secure, bool http_only, long long expires,
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

#ifdef _WIN32

// WebView2 without Microsoft's loader DLL: the runtime's registry entry names its folder, and its
// EmbeddedBrowserWebView.dll exports the function the loader calls. COM from C, with hand-made
// handler objects (each a vtable pointer; they live inside the signin struct, so no reference counts).
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include "vendor/webview2/WebView2.h"

// Device-bound sessions would tie the cookies to this machine's TPM and to this browser, so they
// could not be refreshed over plain HTTPS afterwards.
#define BROWSER_ARGS L"--disable-features=DeviceBoundSessions,EnableBoundSessionCredentials,DeviceBoundSessionCredentials"

typedef HRESULT(STDMETHODCALLTYPE *create_env_fn)(BOOL check_running, int runtime_type, PCWSTR user_data,
    IUnknown *options, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *done);

typedef struct { ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl *v; signin *s; } env_done;
typedef struct { ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl *v; signin *s; } controller_done;
typedef struct { ICoreWebView2NavigationCompletedEventHandlerVtbl *v; signin *s; } navigated;
typedef struct { ICoreWebView2GetCookiesCompletedHandlerVtbl *v; signin *s; } cookies_done;
typedef struct { ICoreWebView2EnvironmentOptionsVtbl *v; signin *s; } options;

struct signin {
    SDL_Window *window;
    HWND hwnd;
    HMODULE dll;
    ICoreWebView2Controller *controller;
    ICoreWebView2 *view;
    EventRegistrationToken token;
    env_done on_env;
    controller_done on_controller;
    navigated on_navigated;
    cookies_done on_cookies;
    options opts;
    bool asking;  // a cookie request is out
    bool closed;  // the window is gone; callbacks still on their way clean up after themselves
    int result;
    char *jar;
    char why[200];  // what failed
};

// ---- Shared IUnknown parts ----

static HRESULT STDMETHODCALLTYPE query(void *self, REFIID riid, void **out) {
    // Every handler answers to IUnknown and its own interface; the runtime asks for nothing else.
    *out = self;
    (void)riid;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE add_ref(void *self) { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE release(void *self) { (void)self; return 1; }

static WCHAR *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = SDL_malloc(sizeof *w * (size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void utf8(const WCHAR *w, char *out, int size) {
    out[0] = 0;
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL);
}

static void fail(signin *s, const char *what, HRESULT hr) {
    SDL_Log("sign-in: %s (0x%08lx)", what, (unsigned long)hr);
    snprintf(s->why, sizeof s->why, "Sign-in failed: %s", what);
    s->result = -1;
}

// ---- Environment options: the browser arguments ----

static LPWSTR task_string(const WCHAR *w) {
    size_t n = (wcslen(w) + 1) * sizeof *w;
    LPWSTR out = CoTaskMemAlloc(n);
    if (out) memcpy(out, w, n);
    return out;
}

static HRESULT STDMETHODCALLTYPE options_query(ICoreWebView2EnvironmentOptions *self, REFIID riid, void **out) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2EnvironmentOptions)) { *out = self; return S_OK; }
    *out = NULL;  // the newer option sets: their defaults are fine
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE get_args(ICoreWebView2EnvironmentOptions *self, LPWSTR *v) { (void)self; *v = task_string(BROWSER_ARGS); return S_OK; }
static HRESULT STDMETHODCALLTYPE get_language(ICoreWebView2EnvironmentOptions *self, LPWSTR *v) { (void)self; *v = task_string(L""); return S_OK; }
static HRESULT STDMETHODCALLTYPE get_target(ICoreWebView2EnvironmentOptions *self, LPWSTR *v) { (void)self; *v = task_string(L"120.0.2210.55"); return S_OK; }
static HRESULT STDMETHODCALLTYPE get_sso(ICoreWebView2EnvironmentOptions *self, BOOL *v) { (void)self; *v = FALSE; return S_OK; }
static HRESULT STDMETHODCALLTYPE put_string(ICoreWebView2EnvironmentOptions *self, LPCWSTR v) { (void)self, (void)v; return S_OK; }
static HRESULT STDMETHODCALLTYPE put_sso(ICoreWebView2EnvironmentOptions *self, BOOL v) { (void)self, (void)v; return S_OK; }

static ICoreWebView2EnvironmentOptionsVtbl options_vtbl = {
    options_query, (void *)add_ref, (void *)release, get_args, put_string, get_language, put_string, get_target, put_string, get_sso, put_sso,
};

static HRESULT STDMETHODCALLTYPE cookies_invoke(ICoreWebView2GetCookiesCompletedHandler *self, HRESULT hr, ICoreWebView2CookieList *list) {
    signin *s = ((cookies_done *)self)->s;
    s->asking = false;
    if (s->closed) return S_OK;
    UINT count = 0;
    if (FAILED(hr) || !list || FAILED(ICoreWebView2CookieList_get_Count(list, &count))) return S_OK;
    jar j = jar_new();
    for (UINT i = 0; i < count; i++) {
        ICoreWebView2Cookie *c = NULL;
        if (FAILED(ICoreWebView2CookieList_GetValueAtIndex(list, i, &c)) || !c) continue;
        LPWSTR wn = NULL, wv = NULL, wd = NULL, wp = NULL;
        double expires = 0;
        BOOL http_only = FALSE, secure = FALSE, session = FALSE;
        ICoreWebView2Cookie_get_Name(c, &wn);
        ICoreWebView2Cookie_get_Value(c, &wv);
        ICoreWebView2Cookie_get_Domain(c, &wd);
        ICoreWebView2Cookie_get_Path(c, &wp);
        ICoreWebView2Cookie_get_Expires(c, &expires);
        ICoreWebView2Cookie_get_IsHttpOnly(c, &http_only);
        ICoreWebView2Cookie_get_IsSecure(c, &secure);
        ICoreWebView2Cookie_get_IsSession(c, &session);
        char name[256], value[4096], domain[256], path[512];
        utf8(wn, name, sizeof name), utf8(wv, value, sizeof value), utf8(wd, domain, sizeof domain), utf8(wp, path, sizeof path);
        CoTaskMemFree(wn), CoTaskMemFree(wv), CoTaskMemFree(wd), CoTaskMemFree(wp);
        ICoreWebView2Cookie_Release(c);
        jar_add(&j, domain, path, secure, http_only, session || expires < 0 ? 0LL : (long long)expires, name, value);
    }
    SDL_Log("sign-in: %u cookies, %s", count, j.youtube_session ? "signed in to YouTube" : "not signed in yet");
    if (j.youtube_session && !s->jar) s->jar = j.text, s->result = 1;
    else SDL_free(j.text);
    return S_OK;
}

static ICoreWebView2GetCookiesCompletedHandlerVtbl cookies_vtbl = { (void *)query, (void *)add_ref, (void *)release, cookies_invoke };

// ---- Watching the page ----

static HRESULT STDMETHODCALLTYPE navigated_invoke(ICoreWebView2NavigationCompletedEventHandler *self, ICoreWebView2 *view, ICoreWebView2NavigationCompletedEventArgs *args) {
    signin *s = ((navigated *)self)->s;
    (void)args;
    LPWSTR wurl = NULL;
    char url[1024];
    ICoreWebView2_get_Source(view, &wurl);
    utf8(wurl, url, sizeof url);
    CoTaskMemFree(wurl);
    SDL_Log("sign-in: at %.120s", url);
    // Signed in once Google sends the page on to YouTube.
    if (s->asking || s->result || (strncmp(url, "https://music.youtube.com/", 26) && strncmp(url, "https://www.youtube.com/", 24))) return S_OK;
    ICoreWebView2_2 *view2 = NULL;
    ICoreWebView2CookieManager *cookies = NULL;
    HRESULT hr = ICoreWebView2_QueryInterface(view, &IID_ICoreWebView2_2, (void **)&view2);
    if (SUCCEEDED(hr)) hr = ICoreWebView2_2_get_CookieManager(view2, &cookies);
    if (SUCCEEDED(hr)) {
        s->asking = true;
        hr = ICoreWebView2CookieManager_GetCookies(cookies, L"", (ICoreWebView2GetCookiesCompletedHandler *)&s->on_cookies);
        if (FAILED(hr)) s->asking = false;
    }
    if (cookies) ICoreWebView2CookieManager_Release(cookies);
    if (view2) ICoreWebView2_2_Release(view2);
    if (FAILED(hr)) fail(s, "this WebView2 runtime cannot read cookies", hr);
    return S_OK;
}

static ICoreWebView2NavigationCompletedEventHandlerVtbl navigated_vtbl = { (void *)query, (void *)add_ref, (void *)release, navigated_invoke };

// ---- Setting up ----

static void fit(signin *s) {
    RECT r;
    GetClientRect(s->hwnd, &r);
    if (s->controller) ICoreWebView2Controller_put_Bounds(s->controller, r);
}

static HRESULT STDMETHODCALLTYPE controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self, HRESULT hr, ICoreWebView2Controller *controller) {
    signin *s = ((controller_done *)self)->s;
    if (s->closed) {
        if (controller) ICoreWebView2Controller_Close(controller);
        return S_OK;
    }
    if (FAILED(hr) || !controller) { fail(s, "could not create the browser view", hr); return S_OK; }
    ICoreWebView2Controller_AddRef(controller);
    s->controller = controller;
    ICoreWebView2Controller_get_CoreWebView2(controller, &s->view);
    fit(s);
    ICoreWebView2_add_NavigationCompleted(s->view, (ICoreWebView2NavigationCompletedEventHandler *)&s->on_navigated, &s->token);
    ICoreWebView2_Navigate(s->view, L"" SIGNIN_URL);
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl controller_vtbl = { (void *)query, (void *)add_ref, (void *)release, controller_invoke };

static HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self, HRESULT hr, ICoreWebView2Environment *env) {
    signin *s = ((env_done *)self)->s;
    if (s->closed) return S_OK;
    if (FAILED(hr) || !env) { fail(s, "could not start WebView2", hr); return S_OK; }
    hr = ICoreWebView2Environment_CreateCoreWebView2Controller(env, s->hwnd, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)&s->on_controller);
    if (FAILED(hr)) fail(s, "could not create the browser view", hr);
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl env_vtbl = { (void *)query, (void *)add_ref, (void *)release, env_invoke };

// The installed WebView2 runtime's folder, from its updater's registry entry.
static bool runtime_folder(WCHAR *out, DWORD size) {
    static const WCHAR *key = L"SOFTWARE\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (int i = 0; i < 2; i++) {
        DWORD n = size * sizeof *out;
        if (RegGetValueW(roots[i], key, L"EBWebView", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, NULL, out, &n) == ERROR_SUCCESS && out[0]) return true;
    }
    return false;
}

signin *signin_open(const char *data_dir, char *why, size_t size) {
    WCHAR folder[MAX_PATH], dll_path[MAX_PATH + 64];
    if (!runtime_folder(folder, MAX_PATH)) {
        SDL_strlcpy(why, "the WebView2 runtime is not installed (get it from Microsoft's WebView2 page)", size);
        return NULL;
    }
    _snwprintf(dll_path, sizeof dll_path / sizeof *dll_path, L"%ls\\EBWebView\\%ls\\EmbeddedBrowserWebView.dll", folder,
               sizeof(void *) == 8 ? L"x64" : L"x86");
    dll_path[sizeof dll_path / sizeof *dll_path - 1] = 0;
    HMODULE dll = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    create_env_fn create = dll ? (create_env_fn)(void *)GetProcAddress(dll, "CreateWebViewEnvironmentWithOptionsInternal") : NULL;
    if (!create) {
        if (dll) FreeLibrary(dll);
        SDL_strlcpy(why, "could not load the WebView2 runtime", size);
        return NULL;
    }
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        FreeLibrary(dll);
        SDL_strlcpy(why, "could not start COM", size);
        return NULL;
    }

    signin *s = SDL_calloc(1, sizeof *s);
    s->dll = dll;
    s->on_env = (env_done){ &env_vtbl, s };
    s->on_controller = (controller_done){ &controller_vtbl, s };
    s->on_navigated = (navigated){ &navigated_vtbl, s };
    s->on_cookies = (cookies_done){ &cookies_vtbl, s };
    s->opts = (options){ &options_vtbl, s };
    s->window = SDL_CreateWindow("Sign in to YouTube Music", 520, 720, SDL_WINDOW_RESIZABLE);
    s->hwnd = s->window ? SDL_GetPointerProperty(SDL_GetWindowProperties(s->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
    if (!s->hwnd) {
        signin_close(s);
        SDL_strlcpy(why, "could not open the sign-in window", size);
        return NULL;
    }
    // The browser profile stays in the data folder, apart from Edge's.
    char profile[1200];
    snprintf(profile, sizeof profile, "%swebview", data_dir);
    WCHAR *wprofile = wide(profile);
    hr = create(TRUE, 0 /* the installed runtime */, wprofile, (IUnknown *)&s->opts,
                (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)&s->on_env);
    SDL_free(wprofile);
    if (FAILED(hr)) {
        signin_close(s);
        snprintf(why, size, "could not start WebView2 (0x%08lx)", (unsigned long)hr);
        return NULL;
    }
    return s;
}

void signin_event(signin *s, const SDL_Event *e) {
    if (!s || !s->window || e->window.windowID != SDL_GetWindowID(s->window)) return;
    if (e->type == SDL_EVENT_WINDOW_RESIZED || e->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) fit(s);
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && !s->result) s->result = -1;
}

int signin_poll(signin *s, char **jar, char *why, size_t size) {
    if (!s) return -1;
    if (s->result == 1 && jar) *jar = s->jar, s->jar = NULL;
    if (s->result == -1) SDL_strlcpy(why, s->why[0] ? s->why : "Sign-in closed", size);
    return s->result;
}

void signin_close(signin *s) {
    if (!s) return;
    if (s->view) {
        ICoreWebView2_remove_NavigationCompleted(s->view, s->token);
        ICoreWebView2_Release(s->view);
        s->view = NULL;
    }
    if (s->controller) {
        ICoreWebView2Controller_Close(s->controller);
        ICoreWebView2Controller_Release(s->controller);
        s->controller = NULL;
    }
    if (s->window) SDL_DestroyWindow(s->window);
    s->window = NULL;
    SDL_free(s->jar);
    s->jar = NULL;
    // The struct itself stays (a few hundred bytes per sign-in): WebView2 may still call the
    // handlers inside it. The runtime DLL stays loaded for the same reason.
    s->closed = true;
}

#else

// WebKitGTK, GTK 3's or GTK 4's, loaded when the window opens like WebView2 on Windows, so nothing
// is needed to build and a system without it only loses this window. It runs in a process of its
// own (signin_window), which keeps GTK and its main loop out of the player's, and all of it goes
// when the window closes.
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

// ---- The window's process ----

typedef struct glist { void *data; struct glist *next, *prev; } glist;

// The GLib, libsoup, GTK and WebKitGTK functions used, looked up at run time. Both WebKitGTK APIs
// have these (get_all_cookies needs 2.42 or newer):
static unsigned long (*g_signal_connect_data)(void *, const char *, void *, void *, void *, int);
static void *(*g_main_loop_new)(void *, int);
static void (*g_main_loop_run)(void *);
static void (*g_main_loop_quit)(void *);
static void (*g_list_free_full)(glist *, void (*)(void *));
static long long (*g_date_time_to_unix)(void *);
static const char *(*soup_cookie_get_name)(void *);
static const char *(*soup_cookie_get_value)(void *);
static const char *(*soup_cookie_get_domain)(void *);
static const char *(*soup_cookie_get_path)(void *);
static void *(*soup_cookie_get_expires)(void *);
static int (*soup_cookie_get_secure)(void *);
static int (*soup_cookie_get_http_only)(void *);
static void (*soup_cookie_free)(void *);
static void (*gtk_window_set_title)(void *, const char *);
static void (*gtk_window_set_default_size)(void *, int, int);
static void (*webkit_web_view_load_uri)(void *, const char *);
static const char *(*webkit_web_view_get_uri)(void *);
static void (*webkit_cookie_manager_get_all_cookies)(void *, void *, void *, void *);
static glist *(*webkit_cookie_manager_get_all_cookies_finish)(void *, void *, void **);
// GTK 3's WebKitGTK (libwebkit2gtk-4.1) and GTK 4's (libwebkitgtk-6.0) differ in making the
// window and its ephemeral browser session:
static int (*gtk3_init_check)(int *, char ***);
static void *(*gtk3_window_new)(int);
static void (*gtk3_container_add)(void *, void *);
static void (*gtk3_widget_show_all)(void *);
static void *(*webkit41_web_context_new_ephemeral)(void);
static void *(*webkit41_web_context_get_cookie_manager)(void *);
static void *(*webkit41_web_view_new_with_context)(void *);
static int (*gtk4_init_check)(void);
static void *(*gtk4_window_new)(void);
static void (*gtk4_window_set_child)(void *, void *);
static void (*gtk4_window_present)(void *);
static void *(*webkit60_network_session_new_ephemeral)(void);
static void *(*webkit60_network_session_get_cookie_manager)(void *);
static unsigned long (*webkit60_web_view_get_type)(void);
static void *(*g_object_new)(unsigned long, const char *, ...);

typedef struct { const char *name; void **fn; } symbol;
#define FN(f) { #f, (void **)&f }
#define AS(f, name) { name, (void **)&f }
static const symbol common[] = {
    FN(g_signal_connect_data), FN(g_main_loop_new), FN(g_main_loop_run), FN(g_main_loop_quit), FN(g_list_free_full),
    FN(g_date_time_to_unix), FN(soup_cookie_get_name), FN(soup_cookie_get_value), FN(soup_cookie_get_domain),
    FN(soup_cookie_get_path), FN(soup_cookie_get_expires), FN(soup_cookie_get_secure), FN(soup_cookie_get_http_only),
    FN(soup_cookie_free), FN(gtk_window_set_title), FN(gtk_window_set_default_size), FN(webkit_web_view_load_uri),
    FN(webkit_web_view_get_uri), FN(webkit_cookie_manager_get_all_cookies), FN(webkit_cookie_manager_get_all_cookies_finish),
};
static const symbol gtk3_api[] = {
    AS(gtk3_init_check, "gtk_init_check"), AS(gtk3_window_new, "gtk_window_new"), AS(gtk3_container_add, "gtk_container_add"),
    AS(gtk3_widget_show_all, "gtk_widget_show_all"), AS(webkit41_web_context_new_ephemeral, "webkit_web_context_new_ephemeral"),
    AS(webkit41_web_context_get_cookie_manager, "webkit_web_context_get_cookie_manager"),
    AS(webkit41_web_view_new_with_context, "webkit_web_view_new_with_context"),
};
static const symbol gtk4_api[] = {
    AS(gtk4_init_check, "gtk_init_check"), AS(gtk4_window_new, "gtk_window_new"), AS(gtk4_window_set_child, "gtk_window_set_child"),
    AS(gtk4_window_present, "gtk_window_present"), AS(webkit60_network_session_new_ephemeral, "webkit_network_session_new_ephemeral"),
    AS(webkit60_network_session_get_cookie_manager, "webkit_network_session_get_cookie_manager"),
    AS(webkit60_web_view_get_type, "webkit_web_view_get_type"), FN(g_object_new),
};

// The first one installed is used: GTK 3 and 4 cannot share a process, so once one is loaded the
// other is not tried.
static const struct { const char *lib; const symbol *fns; size_t n; } engines[] = {
    { "libwebkit2gtk-4.1.so.0", gtk3_api, SDL_arraysize(gtk3_api) },
    { "libwebkitgtk-6.0.so.4", gtk4_api, SDL_arraysize(gtk4_api) },
};

static const char *load(void *lib, const symbol *fns, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!(*fns[i].fn = dlsym(lib, fns[i].name))) return fns[i].name;
    return NULL;
}

static struct {
    const char *file;
    void *cookies;  // the cookie manager
    void *loop;
    bool asking;    // a cookie request is out
    bool done;      // the session is in the file
} win;

static void on_cookies(void *manager, void *result, void *data) {
    (void)data;
    win.asking = false;
    glist *list = webkit_cookie_manager_get_all_cookies_finish(manager, result, NULL);
    jar j = jar_new();
    for (glist *l = list; l; l = l->next) {
        void *expires = soup_cookie_get_expires(l->data);
        jar_add(&j, soup_cookie_get_domain(l->data), soup_cookie_get_path(l->data), soup_cookie_get_secure(l->data),
                soup_cookie_get_http_only(l->data), expires ? g_date_time_to_unix(expires) : 0,
                soup_cookie_get_name(l->data), soup_cookie_get_value(l->data));
    }
    g_list_free_full(list, soup_cookie_free);
    if (j.youtube_session) {
        int fd = open(win.file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        win.done = fd >= 0 && write(fd, j.text, j.len) == (ssize_t)j.len;
        if (fd >= 0) close(fd);
        if (win.done) g_main_loop_quit(win.loop);
    }
    SDL_free(j.text);
}

static void on_load(void *view, int event, void *data) {
    (void)data;
    const char *url = webkit_web_view_get_uri(view);
    // Signed in once Google sends the page on to YouTube (3 is WEBKIT_LOAD_FINISHED).
    if (event != 3 || win.asking || win.done || !url ||
        (strncmp(url, "https://music.youtube.com/", 26) && strncmp(url, "https://www.youtube.com/", 24))) return;
    win.asking = true;
    webkit_cookie_manager_get_all_cookies(win.cookies, NULL, (void *)on_cookies, NULL);
}

static void on_destroy(void *window, void *data) { (void)window, (void)data; g_main_loop_quit(win.loop); }

int signin_window(const char *file) {
    size_t e = 0;
    void *lib = NULL;
    while (e < SDL_arraysize(engines) && !(lib = dlopen(engines[e].lib, RTLD_NOW))) e++;
    if (!lib) return printf("WebKitGTK is not installed (libwebkit2gtk-4.1 or libwebkitgtk-6.0); use --import-cookies FILE\n"), 2;
    const char *missing = load(lib, common, SDL_arraysize(common));
    if (!missing) missing = load(lib, engines[e].fns, engines[e].n);
    if (missing) return printf("this WebKitGTK is too old (no %s); use --import-cookies FILE\n", missing), 2;
    bool gtk4 = engines[e].fns == gtk4_api;
    if (!(gtk4 ? gtk4_init_check() : gtk3_init_check(NULL, NULL))) return printf("GTK could not open a window\n"), 2;
    win.file = file;
    void *view, *window;
    if (gtk4) {  // nothing of the browser is kept but the cookies
        void *session = webkit60_network_session_new_ephemeral();
        win.cookies = webkit60_network_session_get_cookie_manager(session);
        view = g_object_new(webkit60_web_view_get_type(), "network-session", session, NULL);
        window = gtk4_window_new();
        gtk4_window_set_child(window, view);
    } else {
        void *context = webkit41_web_context_new_ephemeral();
        win.cookies = webkit41_web_context_get_cookie_manager(context);
        view = webkit41_web_view_new_with_context(context);
        window = gtk3_window_new(0);
        gtk3_container_add(window, view);
    }
    gtk_window_set_title(window, "Sign in to YouTube Music");
    gtk_window_set_default_size(window, 520, 720);
    win.loop = g_main_loop_new(NULL, 0);
    g_signal_connect_data(window, "destroy", (void *)on_destroy, NULL, NULL, 0);
    g_signal_connect_data(view, "load-changed", (void *)on_load, NULL, NULL, 0);
    webkit_web_view_load_uri(view, SIGNIN_URL);
    if (gtk4) gtk4_window_present(window);
    else gtk3_widget_show_all(window);
    g_main_loop_run(win.loop);
    return win.done ? 0 : 1;
}

// ---- The player's side ----

struct signin {
    SDL_Process *process;
    char file[1200];  // where the process leaves the session
};

signin *signin_open(const char *data_dir, char *why, size_t size) {
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return SDL_strlcpy(why, "could not find gtube's executable to open the sign-in window", size), NULL;
    exe[n] = 0;
    signin *s = SDL_calloc(1, sizeof *s);
    snprintf(s->file, sizeof s->file, "%saccount", data_dir);
    SDL_CreateDirectory(s->file);
    snprintf(s->file, sizeof s->file, "%saccount/signin.txt", data_dir);
    SDL_RemovePath(s->file);
    const char *args[] = { exe, "--signin-window", s->file, NULL };
    if (!(s->process = SDL_CreateProcess(args, true))) {
        snprintf(why, size, "could not open the sign-in window: %s", SDL_GetError());
        SDL_free(s);
        return NULL;
    }
    return s;
}

void signin_event(signin *s, const SDL_Event *e) { (void)s, (void)e; }  // the window is the other process's

int signin_poll(signin *s, char **jar, char *why, size_t size) {
    int code;
    if (!s || !SDL_WaitProcess(s->process, false, &code)) return s ? 0 : -1;
    if (code == 0 && (*jar = SDL_LoadFile(s->file, NULL))) return 1;
    char *out = code == 2 ? SDL_ReadProcess(s->process, NULL, NULL) : NULL;
    if (out) out[strcspn(out, "\n")] = 0;
    snprintf(why, size, "%s%s", out && out[0] ? "Sign-in: " : "Sign-in closed", out ? out : "");
    SDL_free(out);
    return -1;
}

void signin_close(signin *s) {
    if (!s) return;
    SDL_KillProcess(s->process, false);
    SDL_WaitProcess(s->process, true, NULL);
    SDL_DestroyProcess(s->process);
    SDL_RemovePath(s->file);
    SDL_free(s);
}

#endif
