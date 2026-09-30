# gtube

A native YouTube Music player with a music visualizer, written in C on [gesso](https://github.com/wakamex/gesso).

A music player shouldn't need a copy of a web browser. Desktop players are usually web apps shipped inside Electron or a WebView, which bring a whole Chromium along. gtube is one executable of about 4 MB for Windows or Linux, drawing with SDL and playing Opus audio directly, in about 50 MB of RAM.

Search, liked music, your playlists, radio and likes come from YouTube Music's web API, the same one its website uses; yt-dlp streams the audio. gtube is not affiliated with Google, and a change on YouTube's side can break it until gtube catches up.

## Build and run

There are no prebuilt downloads yet. gtube builds from source with [Zig](https://ziglang.org) 0.16, which fetches gesso, SDL and the rest on the first build; nothing else needs installing to build it.

```sh
git clone https://github.com/wakamex/gtube
cd gtube
zig build run --release=fast
```

Give it a link to start playing at once:

```sh
zig build run --release=fast -- "https://music.youtube.com/playlist?list=..."
```

The executable lands in `zig-out/bin`. To build the Windows version, on Windows or from Linux:

```sh
zig build --release=fast -Dtarget=x86_64-windows-gnu -p zig-out/windows
```

Keep `gtube.gpu` (or `gtube.exe.gpu`) next to the executable when you move it; the visualizer's GPU effects load it from there.

On Linux gtube needs `curl` and `tar` on the system and an X11 or Wayland desktop. A Vulkan driver is optional (see Visualizer).

## First run

On first run gtube downloads its own copies of yt-dlp and Deno (the JavaScript runtime yt-dlp needs for YouTube) into its data folder, and keeps them current, so the first song takes a little longer. If Deno is already on your PATH, it uses that one.

Signed out, search, links and radio work. Your liked music, your playlists and liking songs need you to sign in, and YouTube sometimes asks for a signed-in session before it will play a track; the status line says so when it does.

## Sign-in

On Windows, press s. It opens Google's sign-in page in a small WebView2 window. Once it reaches YouTube signed in, the cookies are saved encrypted for your Windows user (DPAPI), and the session is renewed over plain HTTPS at launch and every 10 minutes.

On Linux the sign-in window isn't built yet, so s only points you here. Instead, sign in to music.youtube.com in a browser, export its cookies with a cookies.txt extension (Netscape format, including the youtube.com and google.com cookies), and run:

```sh
zig build run --release=fast -- --import-cookies ~/Downloads/cookies.txt
```

gtube keeps its own copy and renews it like on Windows, so you can delete the exported file afterwards; it holds your Google session.

## Keys

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
| s | Sign in (Windows; see Sign-in for Linux) |
| F1 | Performance overlay |
| f, Alt+Enter or F11 | Full screen |

Long playlists load in full, not page by page. The window's size and position and the queue are kept between runs; a restored queue waits for Space or Enter, and a restored radio keeps loading more as it plays.

## Visualizer

View 4 draws what you're hearing (a 64-band spectrum, the waveform, bass, mids, treble and beats) through seven effects, at the screen's full resolution:

- Spectrum: Winamp-style LED bars with a scope
- Tunnel: a bending tunnel of textured rings
- Feedback: Milkdrop-style, each frame the last one seen through a warp mesh
- Fire, fed by the spectrum
- Stars and bobs: a warp starfield around a sphere of bobs pushed out by the bands
- Bend plasma and Bend tree, written in [Bend](https://github.com/bendlang/bend)

| Key | Action |
|---|---|
| Up and Down | Change the effect |
| Enter | Auto mode on or off: a new effect every 40 s and with each track |
| t | Show or hide the scroller with the track's name |
| g | Run a Bend effect on the GPU or the CPU |

The two Bend effects draw every pixel on a thread of their own. With a Vulkan GPU they draw straight into the player's textures: at 4K on an RTX 3080 the plasma runs at over 3,000 frames a second with `--uncapped`. Without one they draw on the CPU. Loading the Vulkan driver the first time a Bend effect is shown brings gtube to about 140 MB of RAM.

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

Titles render in Latin, Cyrillic, Japanese, Chinese, Korean, Devanagari and Arabic with the system fonts, but Arabic has no joining or right-to-left order yet. First audio arrives about 4 s after a link, most of it yt-dlp's extraction. Not built yet: lyrics, cover art, seeking, editing playlists, and the sign-in window on Linux.

## Development

To work on gesso alongside gtube, build against a local checkout with `zig build --fork=../gesso`. `build.zig.zon` pins a gesso commit; `zig fetch --save=gesso git+https://github.com/wakamex/gesso#main` moves it to the latest.

The Bend effects live in `src/bend/viz.bend`. The generated C (`src/bend/viz_bend.c`) and its GPU program (`src/bend/viz.gpu`) are committed; regenerating them with `src/bend/build.sh` needs a Bend compiler with the Vulkan backend.
