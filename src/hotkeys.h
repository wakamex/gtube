// Keys that reach gtube while another program has the keyboard: Ctrl+Alt+L likes the playing song and
// Ctrl+Alt+D dislikes it. The system's media controls have no like, so these stand in for one; a
// keyboard's spare key can be set to send them. Windows only for now.
#pragma once
#include <SDL3/SDL.h>

enum { HOTKEY_LIKE = 1, HOTKEY_DISLIKE };

// The event type the keys come as, with the key in event.user.code; 0 where there are none (another
// program has the combination, or not Windows). Call on the thread that pumps SDL's events.
Uint32 hotkeys_init(void);
void hotkeys_quit(void);
