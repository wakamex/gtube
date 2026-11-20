// Whether two songs in the queue are one song under different videos (a radio skips such a song).
// Two songs that differ must never count as one.
#pragma once
#include <stdbool.h>

#include "player.h"

bool same_song(const track *x, const track *y);
