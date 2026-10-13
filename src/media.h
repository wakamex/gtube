// The keyboard's media keys while another window is focused. On Windows they are registered as
// hotkeys so play/pause, next and previous reach gtube the same way they do when it has focus
// (SDL's SDLK_MEDIA_*). Elsewhere this does nothing; the in-window keys still work.
#pragma once
#include "player.h"

void media_keys_open(player *p);
void media_keys_close(void);
