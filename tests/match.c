// same_song on pairs of songs that are one song, and pairs that must stay apart. The titles,
// artists and lengths are made up for the test, after the shapes YouTube Music's radio gives.
#include <stdio.h>

#include "match.h"

static int failed;

static void check(bool want, const char *title_a, const char *artist_a, double length_a, const char *title_b, const char *artist_b, double length_b) {
    track a = { .duration = length_a }, b = { .duration = length_b };
    SDL_strlcpy(a.id, "aaaaaaaaaaa", sizeof a.id), SDL_strlcpy(b.id, "bbbbbbbbbbb", sizeof b.id);
    SDL_strlcpy(a.title, title_a, sizeof a.title), SDL_strlcpy(a.artist, artist_a, sizeof a.artist);
    SDL_strlcpy(b.title, title_b, sizeof b.title), SDL_strlcpy(b.artist, artist_b, sizeof b.artist);
    if (same_song(&a, &b) == want && same_song(&b, &a) == want) return;
    failed++;
    printf("FAIL: \"%s\" by %s and \"%s\" by %s should be %s\n", title_a, artist_a, title_b, artist_b, want ? "one song" : "two");
}

int main(void) {
    // One song: another upload, a video and its audio, remasters.
    check(true, "Apelislin - Stay In The Quiet", "Apelislin", 200, "Stay In The Quiet", "Apelislin", 201);
    check(true, "IN MY OWN TIME \xE2\x80\x94 FLORAY & BOHDAN | Official Music Video", "FLORAY & BOHDAN", 180, "IN MY OWN TIME", "FLORAY & BOHDAN", 182);
    check(true, "Never Gonna Give You Up (Official Video)", "Rick Astley", 213, "Never Gonna Give You Up", "Rick Astley", 212);
    check(true, "Here Comes the Sun - Remastered 2009", "The Beatles", 185, "Here Comes the Sun (2019 Remaster)", "The Beatles", 186);
    check(true, "Song (Official Lyric Video)", "Artist", 200, "Song (Audio)", "Artist", 200);
    check(true, "Caribbean Queen (No More Love on the Run) (Official HD Video)", "Billy Ocean", 240, "Caribbean Queen (No More Love on the Run)", "Billy Ocean", 241);

    // Two songs: label uploads, parts, reprises, soundtrack cues, other versions, other artists.
    check(false, "Pegboard Nerds - Hero", "Monstercat Uncaged", 200, "Pegboard Nerds - Rocktronik", "Monstercat Uncaged", 201);
    check(false, "Another Brick in the Wall (Part 1)", "Pink Floyd", 200, "Another Brick in the Wall (Part 2)", "Pink Floyd", 200);
    check(false, "Interstellar - Cornfield Chase", "Hans Zimmer", 126, "Interstellar - Mountains", "Hans Zimmer", 124);
    check(false, "Sgt. Pepper's Lonely Hearts Club Band", "The Beatles", 122, "Sgt. Pepper's Lonely Hearts Club Band (Reprise)", "The Beatles", 120);
    check(false, "Layla", "Eric Clapton", 284, "Layla (Acoustic)", "Eric Clapton", 284);
    check(false, "Song", "Artist", 200, "Song (Live)", "Artist", 200);
    check(false, "Song", "Artist", 200, "Song - Live", "Artist", 200);
    check(false, "Song (Version)", "Artist", 200, "Song", "Artist", 200);
    check(false, "Caribbean Queen (No More Love on the Run) (Official HD Video)", "Billy Ocean", 240, "Caribbean Queen", "Billy Ocean", 240);
    check(false, "Song", "Sia", 200, "Song", "Asia", 200);
    check(false, "Song", "Tyler, The Creator", 200, "Song", "Bonnie Tyler", 200);
    check(false, "Home", "Various Artists", 200, "Home", "Various Artists", 200);

    // Two songs whatever the title: a generic title, lengths apart or unknown.
    check(false, "Intro", "J. Cole", 129, "Intro", "J. Cole", 130);
    check(false, "Song", "Artist", 200, "Song", "Artist", 210);
    check(false, "Song", "Artist", 0, "Song", "Artist", 200);

    // The same video is one song, whatever else differs.
    track a = { .id = "dQw4w9WgXcQ", .title = "x" }, b = { .id = "dQw4w9WgXcQ", .title = "y" };
    if (!same_song(&a, &b)) failed++, printf("FAIL: the same video should be one song\n");

    printf("%s\n", failed ? "same_song: failed" : "same_song: all pairs as expected");
    return failed != 0;
}
