#include "signin.h"

#ifndef _WIN32

signin *signin_open(const char *data_dir, char *why, size_t size) {
    (void)data_dir;
    SDL_strlcpy(why, "the sign-in window is Windows-only for now; use --import-cookies FILE", size);
    return NULL;
}
void signin_event(signin *s, const SDL_Event *e) { (void)s, (void)e; }
int signin_poll(signin *s, char **jar) { (void)s, (void)jar; return -1; }
void signin_close(signin *s) { (void)s; }

#else

// WebView2 without Microsoft's loader DLL: the runtime's registry entry names its folder, and its
// EmbeddedBrowserWebView.dll exports the function the loader calls. COM from C, with hand-made
// handler objects (each a vtable pointer; they live inside the signin struct, so no reference counts).
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

#include "vendor/webview2/WebView2.h"

#define SIGNIN_URL L"https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F"
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

// ---- The cookies, as a Netscape file ----

static bool wanted_domain(const char *d) {
    size_t n = strlen(d);
    return (n >= 10 && !strcmp(d + n - 10, "google.com")) || (n >= 11 && !strcmp(d + n - 11, "youtube.com"));
}

static HRESULT STDMETHODCALLTYPE cookies_invoke(ICoreWebView2GetCookiesCompletedHandler *self, HRESULT hr, ICoreWebView2CookieList *list) {
    signin *s = ((cookies_done *)self)->s;
    s->asking = false;
    if (s->closed) return S_OK;
    UINT count = 0;
    if (FAILED(hr) || !list || FAILED(ICoreWebView2CookieList_get_Count(list, &count))) return S_OK;
    size_t cap = 65536, len = 0;
    char *jar = SDL_malloc(cap);
    len += (size_t)snprintf(jar, cap, "# Netscape HTTP Cookie File\n");
    bool youtube_session = false;
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
        if (!wanted_domain(domain) || !name[0]) continue;
        if (strstr(domain, "youtube.com") && (!strcmp(name, "SAPISID") || !strcmp(name, "__Secure-3PAPISID"))) youtube_session = true;
        char line[5200];
        int n = snprintf(line, sizeof line, "%s%s\t%s\t%s\t%s\t%lld\t%s\t%s\n", http_only ? "#HttpOnly_" : "", domain,
                         domain[0] == '.' ? "TRUE" : "FALSE", path[0] ? path : "/", secure ? "TRUE" : "FALSE",
                         session || expires < 0 ? 0LL : (long long)expires, name, value);
        if (n <= 0 || n >= (int)sizeof line) continue;
        if (len + (size_t)n + 1 > cap) jar = SDL_realloc(jar, cap = (cap + (size_t)n) * 2);
        memcpy(jar + len, line, (size_t)n + 1);
        len += (size_t)n;
    }
    SDL_Log("sign-in: %u cookies, %s", count, youtube_session ? "signed in to YouTube" : "not signed in yet");
    if (youtube_session && !s->jar) s->jar = jar, s->result = 1;
    else SDL_free(jar);
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
    ICoreWebView2_Navigate(s->view, SIGNIN_URL);
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

int signin_poll(signin *s, char **jar) {
    if (!s) return -1;
    if (s->result == 1 && jar) *jar = s->jar, s->jar = NULL;
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

#endif
