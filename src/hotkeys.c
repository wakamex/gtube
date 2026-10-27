#include "hotkeys.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static Uint32 event;

// Every message SDL takes off the thread's queue passes here; WM_HOTKEY has no window, so SDL
// would drop it.
static bool SDLCALL hook(void *user, MSG *msg) {
    (void)user;
    if (msg->message == WM_HOTKEY && (msg->wParam == HOTKEY_LIKE || msg->wParam == HOTKEY_DISLIKE)) {
        SDL_Event e = { .user = { .type = event, .code = (Sint32)msg->wParam } };
        SDL_PushEvent(&e);
    }
    return true;
}

Uint32 hotkeys_init(void) {
    if (event) return event;
    bool like = RegisterHotKey(NULL, HOTKEY_LIKE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L');
    bool dislike = RegisterHotKey(NULL, HOTKEY_DISLIKE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'D');
    if (!like) SDL_Log("hotkeys: Ctrl+Alt+L is taken by another program");
    if (!dislike) SDL_Log("hotkeys: Ctrl+Alt+D is taken by another program");
    if (!like && !dislike) return 0;
    event = SDL_RegisterEvents(1);
    SDL_SetWindowsMessageHook(hook, NULL);
    return event;
}

void hotkeys_quit(void) {
    if (!event) return;
    SDL_SetWindowsMessageHook(NULL, NULL);
    UnregisterHotKey(NULL, HOTKEY_LIKE), UnregisterHotKey(NULL, HOTKEY_DISLIKE);
    event = 0;
}
#else
Uint32 hotkeys_init(void) { return 0; }
void hotkeys_quit(void) {}
#endif
