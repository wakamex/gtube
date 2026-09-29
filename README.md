# gtube

A native YouTube Music player with a music visualizer, written in C on [gesso](https://github.com/wakamex/gesso).

A music player shouldn't need a copy of a web browser. Desktop players are usually web apps shipped inside Electron or a WebView, which bring a whole Chromium along. gtube is one executable of about 4 MB for Windows or Linux, drawing with SDL and playing Opus audio directly, and it runs in about 120 to 160 MB of RAM.

Search, liked music, your playlists, radio and likes come from YouTube Music's own web API; yt-dlp streams the audio.

## Build and run

gtube builds with [Zig](https://ziglang.org) 0.16 and expects a gesso checkout next to it:

```sh
git clone https://github.com/wakamex/gesso
git clone https://github.com/wakamex/gtube
cd gtube
zig build run --release=fast
```

Give it a link to start playing at once:

```sh
zig build run --release=fast -- "https://music.youtube.com/playlist?list=..."
```

On first run it downloads its own copies of yt-dlp and Deno (the JavaScript runtime yt-dlp needs for YouTube) into its data folder, and keeps them current. Lists come over the system's curl.

## Using it

| Key | Action |
|---|---|
| 1 to 4, or Left and Right | Queue, liked music, your playlists, visualizer |
| / or Ctrl+F | Search songs, albums and playlists |
| Enter or click | Play a song and the rest of its list, or open an album or playlist |
| Esc or Backspace | Leave search, or go back from an album or playlist |
| Space | Pause or resume |
| n, p (or the media keys) | Next and previous track |
| r | Start a radio from the selected or playing song |
| l | Like or unlike the selected song |
| Ctrl+V, or drop a link | Play a link: a track, an album or a playlist |
| - and + | Volume |
| s | Sign in |
| F1 | Performance overlay |
| f, Alt+Enter or F11 | Full screen |

Every list loads all its pages. The window's size and position and the queue are kept between runs; a restored queue waits for Space or Enter, and a restored radio keeps loading more as it plays.

## Visualizer

View 4 draws what you're hearing (a 64-band spectrum, the waveform, bass, mids, treble and beats) through seven effects, at the screen's full resolution:

- Spectrum: Winamp-style LED bars with a scope
- Tunnel: a bending tunnel of textured rings
- Feedback: Milkdrop-style, each frame the last one seen through a warp mesh
- Fire, fed by the spectrum
- Stars and bobs: a warp starfield around a sphere of bobs pushed out by the bands
- Bend plasma and Bend tree, written in [Bend](https://github.com/bendlang/bend)

Up and Down change the effect, Enter turns auto mode on or off (a new effect every 40 s and with each track), and t toggles the scroller that carries the track's name.

The two Bend effects (`bend/viz.bend`) run on a thread of their own and draw every pixel, up to 4096 x 4096. `g` moves the same Bend function between the GPU and the CPU's threads while it runs. On the GPU they draw straight into the player's textures through Vulkan, so at 4K on an RTX 3080 the plasma runs at over 3,000 frames a second with `--uncapped`. The generated C (`bend/viz_bend.c`) and its GPU program (`bend/viz.gpu`, installed beside the executable) are committed; regenerating them with `bend/build.sh` needs a Bend compiler with the Vulkan backend. Without a Vulkan GPU the effects draw on the CPU.

## Sign-in

On Windows, s opens Google's sign-in page in a small WebView2 window. Once it reaches YouTube signed in, the cookies are saved encrypted for your Windows user (DPAPI), and the session is renewed over plain HTTPS at launch and every 10 minutes. On other systems, `--import-cookies FILE` takes a cookies.txt exported from a browser.

## Options

| Option | Effect |
|---|---|
| `--search QUERY`, `--radio VIDEO_ID` | Start with a search or a radio |
| `--view 1-4`, `--effect N` | Start on a view or visualizer effect |
| `--full` | Start full screen |
| `--uncapped` | No vsync or frame cap, to see how fast the visualizer can go |
| `--demo` | Fill the queue with sample titles and play a built-in test signal |
| `--sign-in`, `--sign-out`, `--refresh` | Manage the saved session |
| `--import-cookies FILE` | Sign in from a browser's cookies.txt |
| `--data DIR` | Keep the tools and session elsewhere (default: `%APPDATA%\wakamex\gesso-gtube` on Windows, `~/.local/share/wakamex/gesso-gtube` on Linux) |
| `--wav F.wav --seconds S URL` | Render the first track to a file |
| `--shot F.png --at S` | Render one frame to a file |
| `--api search\|albums\|playlists\|browse\|radio ARG` | Print one API answer |
| `--tools [--probe URL]` | Only get or update yt-dlp |

## Limits

Titles render in Latin, Cyrillic, Japanese, Chinese, Korean, Devanagari and Arabic with the system fonts, but Arabic has no joining or right-to-left order yet. First audio arrives about 4 s after a link, most of it yt-dlp's extraction. Not built yet: lyrics, cover art, seeking and editing playlists.
