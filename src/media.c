#include "media.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { HK_PLAY_PAUSE = 1, HK_NEXT = 2, HK_PREV = 3 };

static HWND hwnd;
static player *target;

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && target) {
        switch (wp) {
        case HK_PLAY_PAUSE: player_toggle_pause(target); break;
        case HK_NEXT: player_next(target); break;
        case HK_PREV: player_previous(target); break;
        }
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void media_keys_open(player *p) {
    if (hwnd || !p) return;
    target = p;
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"gtube-media-keys";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        SDL_Log("media keys: RegisterClassW failed (%lu)", GetLastError());
        target = NULL;
        return;
    }
    // Message-only: SDL's PeekMessage on this thread still dispatches to it.
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
    if (!hwnd) {
        SDL_Log("media keys: CreateWindowExW failed (%lu)", GetLastError());
        target = NULL;
        return;
    }
    if (!RegisterHotKey(hwnd, HK_PLAY_PAUSE, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE) ||
        !RegisterHotKey(hwnd, HK_NEXT, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK) ||
        !RegisterHotKey(hwnd, HK_PREV, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK)) {
        SDL_Log("media keys: RegisterHotKey failed (%lu)", GetLastError());
        media_keys_close();
    }
}

void media_keys_close(void) {
    if (hwnd) {
        UnregisterHotKey(hwnd, HK_PLAY_PAUSE);
        UnregisterHotKey(hwnd, HK_NEXT);
        UnregisterHotKey(hwnd, HK_PREV);
        DestroyWindow(hwnd);
        hwnd = NULL;
    }
    target = NULL;
}

#else

void media_keys_open(player *p) { (void)p; }
void media_keys_close(void) {}

#endif
