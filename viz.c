#include "viz.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bend/bendviz.h"
#include "gs_dsp.h"

#define FFT_N 2048
#define BANDS 64
#define STARS 500
#define BOBS 180
#define AUTO_SECONDS 40

enum { FX_SPECTRUM, FX_PLASMA, FX_TUNNEL, FX_FEEDBACK, FX_FIRE, FX_BOBS, FX_BEND, FX_COUNT };
static const char *const fx_names[FX_COUNT] = { "Spectrum", "Plasma (C)", "Tunnel", "Feedback", "Fire", "Stars & bobs", "Bend plasma" };

typedef struct { float x, y, z; } vec3;

// Triangles gathered for one SDL_RenderGeometry call: everything is drawn by the GPU at the
// screen's own resolution, as meshes, lines made of quads, and textured sprites.
typedef struct {
    SDL_Vertex *v;
    int *i;
    int nv, ni, cv, ci;
} mesh;

// A picture worked out on the CPU and drawn smoothly scaled (for the effects that are grids).
typedef struct {
    SDL_Texture *tex;
    uint32_t *px;
    int w, h;
} canvas;

struct viz {
    SDL_Renderer *ren;
    int rate;
    // Analysis.
    double re[FFT_N], im[FFT_N], window[FFT_N];
    float wave[FFT_N];      // the latest samples, mono
    float band[BANDS], peak[BANDS], peak_speed[BANDS], peak_hold[BANDS];
    float raw[BANDS];
    float bass, mid, treble, level;
    float bass_avg, beat;   // beat: 1 on a beat, fading
    int beats;
    double since_beat;
    // Drawing.
    int fx;
    mesh m;
    SDL_Texture *pattern, *ball, *glow;  // the tunnel's wall, a lit sphere, a soft light
    SDL_Texture *feed[2];                // the feedback's last frame and the one being drawn
    int feed_w, feed_h;
    canvas fire, plasma;
    SDL_Texture *bend_tex;  // the Bend effect's newest frame
    int bend_w, bend_h;
    int bend_interop;       // frames on the device go straight into bend_tex: 1 yes, -1 no, 0 not known yet
    bool bend_on_device;    // the last frame did
    bool bend_on_gpu;       // where the Bend effect is asked to draw (g switches it)
    bool bend_drawn_gpu;    // where its newest frame was drawn, and how long that took
    double bend_ms;
    bool bend_started, bend_shown;
    bool vsync;                 // presents wait for the vertical blank (viz_set_vsync)
    int bend_count;             // new frames since bend_count_from (ns), and their rate over the last second
    uint64_t bend_count_from;
    double bend_fps;
    int bend_calls, bend_empty;        // this second: frames drawn, and those with no new Bend frame
    int bend_short, bend_long;         // and those starting under 8 ms or over 25 ms after the last
    uint64_t bend_last_call;
    double bend_take_ms, bend_take_max;  // and the time taking frames into the texture
    char bend_player[64];              // the last second's, as a stats line
    unsigned long long bend_drawn0, bend_dropped0, bend_faults0;  // Bend's totals at the second's start
    double bend_took0, bend_began0;
    uint8_t *heat;
    float *radius;          // the plasma's distance from the centre, per pixel
    float fuel[BANDS];      // the fire's fuel per band, following the spectrum slowly
    float sine[4096];       // a sine table over one turn
    float travel, turn;
    vec3 stars[STARS];
    float hue;              // drifts, and jumps on beats
    double t, dt, fx_since, name_until;
    char last_title[256];
    bool automatic, scroller;
    uint32_t seed;
};

// ---- Small helpers ----

static SDL_FColor hsv(float h, float s, float v, float a) {
    h = (h - floorf(h)) * 6;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    switch (i) {
    case 0: return (SDL_FColor){ v, u, p, a };
    case 1: return (SDL_FColor){ q, v, p, a };
    case 2: return (SDL_FColor){ p, v, u, a };
    case 3: return (SDL_FColor){ p, q, v, a };
    case 4: return (SDL_FColor){ u, p, v, a };
    default: return (SDL_FColor){ v, p, q, a };
    }
}

static SDL_FColor gray(float k, float a) { return (SDL_FColor){ k, k, k, a }; }

static float rnd(viz *v) {
    v->seed = v->seed * 1664525u + 1013904223u;
    return (v->seed >> 8) / 16777216.0f;
}

static int vert(mesh *m, float x, float y, SDL_FColor c, float u, float w) {
    if (m->nv == m->cv) m->v = realloc(m->v, sizeof *m->v * (size_t)(m->cv = m->cv ? m->cv * 2 : 4096));
    m->v[m->nv] = (SDL_Vertex){ { x, y }, c, { u, w } };
    return m->nv++;
}

static void tri(mesh *m, int a, int b, int c) {
    if (m->ni + 3 > m->ci) m->i = realloc(m->i, sizeof *m->i * (size_t)(m->ci = m->ci ? m->ci * 2 : 8192));
    m->i[m->ni++] = a, m->i[m->ni++] = b, m->i[m->ni++] = c;
}

static void quad(mesh *m, int a, int b, int c, int d) { tri(m, a, b, c), tri(m, a, c, d); }

static void rect(mesh *m, float x, float y, float w, float h, SDL_FColor top, SDL_FColor bottom) {
    quad(m, vert(m, x, y, top, 0, 0), vert(m, x + w, y, top, 1, 0), vert(m, x + w, y + h, bottom, 1, 1), vert(m, x, y + h, bottom, 0, 1));
}

// A line as a quad `width` wide, its colour running from c0 to c1.
static void seg(mesh *m, float x0, float y0, float x1, float y1, float width, SDL_FColor c0, SDL_FColor c1) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    // Stretched along the line by half a width at each end, so a polyline's joints close up.
    float ex = dx / len * width / 2, ey = dy / len * width / 2;
    quad(m, vert(m, x0 + nx - ex, y0 + ny - ey, c0, 0, 0), vert(m, x1 + nx + ex, y1 + ny + ey, c1, 1, 0),
         vert(m, x1 - nx + ex, y1 - ny + ey, c1, 1, 1), vert(m, x0 - nx - ex, y0 - ny - ey, c0, 0, 1));
}

static void sprite(mesh *m, float cx, float cy, float r, SDL_FColor c) { rect(m, cx - r, cy - r, 2 * r, 2 * r, c, c); }

static void draw(viz *v, SDL_Texture *tex, SDL_BlendMode blend) {
    if (!v->m.ni) return;
    if (tex) SDL_SetTextureBlendMode(tex, blend);
    else SDL_SetRenderDrawBlendMode(v->ren, blend);
    SDL_RenderGeometry(v->ren, tex, v->m.v, v->m.nv, v->m.i, v->m.ni);
    v->m.nv = v->m.ni = 0;
}

// Makes the canvas w by h (clearing it) if it is not already; returns whether it changed.
static bool fit(viz *v, canvas *c, int w, int h) {
    if (w == c->w && h == c->h) return false;
    free(c->px);
    c->px = calloc((size_t)(w * h), sizeof *c->px);
    if (c->tex) SDL_DestroyTexture(c->tex);
    c->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(c->tex, SDL_SCALEMODE_LINEAR);
    c->w = w, c->h = h;
    return true;
}

static void show(viz *v, canvas *c, SDL_FRect a) {
    SDL_UpdateTexture(c->tex, NULL, c->px, c->w * 4);
    SDL_SetTextureBlendMode(c->tex, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(v->ren, c->tex, NULL, &a);
}

static float fast_sin(const viz *v, float x) { return v->sine[(int)(x * (4096 / 6.2832f)) & 4095]; }

static uint32_t pack(SDL_FColor c) { return (uint32_t)(c.r * 255) << 16 | (uint32_t)(c.g * 255) << 8 | (uint32_t)(c.b * 255); }

// The waveform's last `n` samples across a rectangle, glowing: a wide faint stroke under a thin one.
static void scope(viz *v, float x, float y, float w, float amp, int n, float width, float hue) {
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n - 1; i++) {
            float s0 = v->wave[FFT_N - n + i], s1 = v->wave[FFT_N - n + i + 1];
            SDL_FColor c = hsv(hue + (float)i / n * 0.3f, pass ? 0.35f : 0.7f, 1, pass ? 1 : 0.28f);
            seg(&v->m, x + w * i / (n - 1), y - s0 * amp, x + w * (i + 1) / (n - 1), y - s1 * amp, pass ? width : width * 4, c, c);
        }
        draw(v, NULL, SDL_BLENDMODE_ADD);
    }
}

// ---- Analysis ----

void viz_feed(viz *v, const float *lr, int frames, double dt) {
    int n = frames < FFT_N ? frames : FFT_N;
    memmove(v->wave, v->wave + n, sizeof(float) * (FFT_N - n));
    for (int i = 0; i < n; i++) v->wave[FFT_N - n + i] = 0.5f * (lr[2 * (frames - n + i)] + lr[2 * (frames - n + i) + 1]);
    for (int i = 0; i < FFT_N; i++) v->re[i] = v->wave[i] * v->window[i], v->im[i] = 0;
    gs_fft(v->re, v->im, FFT_N, false);

    // 64 bands spaced evenly in pitch from 30 Hz to 16 kHz, in dB with a gentle lift toward the
    // treble (music has less energy up there than the ear gives it credit for).
    float lo = 30, hi = 16000, energy[3] = { 0 };
    for (int b = 0; b < BANDS; b++) {
        float f0 = lo * powf(hi / lo, (float)b / BANDS), f1 = lo * powf(hi / lo, (float)(b + 1) / BANDS);
        int k0 = (int)(f0 * FFT_N / v->rate), k1 = (int)(f1 * FFT_N / v->rate);
        if (k1 <= k0) k1 = k0 + 1;
        double m = 0;
        for (int k = k0; k < k1 && k < FFT_N / 2; k++) m = fmax(m, sqrt(v->re[k] * v->re[k] + v->im[k] * v->im[k]));
        float db = 20 * log10f((float)m / (FFT_N / 4) + 1e-9f) + 4 * log2f((f0 + f1) / 2 / 1000);
        float x = (db + 58) / 58;
        v->raw[b] = x < 0 ? 0 : x > 1 ? 1 : x;
        energy[b < 8 ? 0 : b < 36 ? 1 : 2] += v->raw[b];
        // Bars jump up and fall at a steady rate; peaks hold, then drop with gravity.
        v->band[b] = fmaxf(v->raw[b], v->band[b] - 1.6f * (float)dt);
        if (v->band[b] >= v->peak[b]) v->peak[b] = v->band[b], v->peak_speed[b] = 0, v->peak_hold[b] = 0.35f;
        else if ((v->peak_hold[b] -= (float)dt) < 0) v->peak_speed[b] += 2.5f * (float)dt, v->peak[b] -= v->peak_speed[b] * (float)dt;
    }
    v->bass = energy[0] / 8, v->mid = energy[1] / 28, v->treble = energy[2] / 28;
    float rms = 0;
    for (int i = FFT_N - 1024; i < FFT_N; i++) rms += v->wave[i] * v->wave[i];
    v->level = sqrtf(rms / 1024);

    // A beat: bass energy well above its recent average, at most every 0.22 s.
    v->since_beat += dt;
    if (v->bass > v->bass_avg * 1.25f + 0.04f && v->since_beat > 0.22) v->beat = 1, v->beats++, v->since_beat = 0, v->hue += 0.13f;
    v->bass_avg += (v->bass - v->bass_avg) * (float)fmin(1, dt * 3);
    v->beat *= expf(-(float)dt * 5);
}

// ---- Effects ----

// Winamp's spectrum: LED-segment bars from green through yellow to red, peaks that hold and fall,
// a reflection on the floor, and its oscilloscope above.
static void fx_spectrum(viz *v, SDL_FRect a) {
    float u = a.h / 200, base = a.y + a.h * 0.78f, top = a.y + a.h * 0.36f, span = base - top;
    for (float y = a.y + 4 * u; y < base; y += 6 * u)  // a faint dotted grid
        for (float x = a.x + 2 * u; x < a.x + a.w; x += 6 * u) rect(&v->m, x, y, u * 0.7f, u * 0.7f, gray(0.12f, 1), gray(0.12f, 1));
    draw(v, NULL, SDL_BLENDMODE_BLEND);
    int bars = 32;
    float slot = (a.w - 16 * u) / bars, bw = slot * 0.82f, x0 = a.x + 8 * u, led = 3 * u, gap = u;
    for (int b = 0; b < bars; b++) {
        float val = fmaxf(v->band[2 * b], v->band[2 * b + 1]), pk = fmaxf(v->peak[2 * b], v->peak[2 * b + 1]), x = x0 + b * slot;
        for (float y = 0; y < val * span; y += led + gap) {
            float f = y / span;
            SDL_FColor c = f < 0.5f ? (SDL_FColor){ 0.16f + f * 1.7f, 0.78f + f * 0.4f, 0.12f, 1 } : (SDL_FColor){ 1, 1 - (f - 0.5f) * 1.7f, 0.12f, 1 };
            rect(&v->m, x, base - y - led, bw, led, c, c);
            SDL_FColor r = c;  // the reflection, fading into the floor
            r.a = 0.22f * (1 - y / (a.y + a.h - base) / 2);
            if (r.a > 0 && y / 2 < a.y + a.h - base) rect(&v->m, x, base + 2 * u + y / 2, bw, led / 2, r, r);
        }
        rect(&v->m, x, base - pk * span - 2 * u, bw, 1.5f * u, gray(0.85f, 1), gray(0.85f, 1));
    }
    draw(v, NULL, SDL_BLENDMODE_BLEND);
    scope(v, a.x, a.y + a.h * 0.19f, a.w, a.h * 0.3f, 576, 1.2f * u, v->hue);
}

// A classic sine plasma at half the screen's height, smoothly scaled. Of its four waves, three are
// split into per-row and per-column tables (sin(a + b) = sin a cos b + cos a sin b), so each pixel
// costs a few multiplications and one table lookup.
static void fx_plasma(viz *v, SDL_FRect a) {
    int h = (int)(a.h / 2);
    h = h < 100 ? 100 : h > 540 ? 540 : h;
    int w = (int)(h * a.w / a.h);
    canvas *c = &v->plasma;
    if (fit(v, c, w, h)) {
        free(v->radius);
        v->radius = malloc(sizeof *v->radius * (size_t)(w * h));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float dx = (x - w / 2.0f) * 200 / h, dy = (y - h / 2.0f) * 200 / h;
                v->radius[y * w + x] = sqrtf(dx * dx + dy * dy);
            }
    }
    float t = (float)v->t, zoom = 0.045f * (1 + v->bass * 0.5f), unit = 200.0f / h * zoom;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        float f = i / 256.0f;
        pal[i] = pack(hsv(v->hue + f * 0.6f + t * 0.03f, 0.75f, fminf(1, 0.35f + 0.5f * (0.5f + 0.5f * sinf(f * 12.566f + t)) + v->beat * 0.25f), 1));
    }
    float *col = malloc(sizeof *col * (size_t)w * 3), *row = malloc(sizeof *row * (size_t)h * 3);
    for (int x = 0; x < w; x++) {
        float dx = (x - w / 2.0f) * unit;
        col[3 * x] = sinf(dx + t), col[3 * x + 1] = sinf(dx * 0.7f + t * 0.7f), col[3 * x + 2] = cosf(dx * 0.7f + t * 0.7f);
    }
    for (int y = 0; y < h; y++) {
        float dy = (y - h / 2.0f) * unit;
        row[3 * y] = sinf(dy * 1.3f + t * 1.1f), row[3 * y + 1] = cosf(dy * 0.7f), row[3 * y + 2] = sinf(dy * 0.7f);
    }
    float rz = zoom * 1.5f, phase = t * 2 + v->mid * 3, shift = t * 40;
    for (int y = 0; y < h; y++) {
        const float *r = row + 3 * y, *rad = v->radius + y * w;
        uint32_t *out = c->px + y * w;
        for (int x = 0; x < w; x++) {
            const float *k = col + 3 * x;
            float s = k[0] + r[0] + k[1] * r[1] + k[2] * r[2] + fast_sin(v, rad[x] * rz - phase);
            out[x] = pal[(int)((s + 4) * 32 + shift) & 255];
        }
    }
    free(col), free(row);
    show(v, c, a);
    scope(v, a.x, a.y + a.h / 2, a.w, a.h * 0.6f, 1024, a.h / 200 * 1.2f, v->hue + 0.5f);
}

// A tunnel of textured rings that bends as it goes, the texture flowing toward you with the bass.
static void fx_tunnel(viz *v, SDL_FRect a) {
    const int rings = 60, sides = 48;
    const float dz = 0.22f, focal = a.h * 0.5f;
    float t = (float)v->t, cx = a.x + a.w / 2, cy = a.y + a.h / 2;
    v->travel += (float)v->dt * (0.9f + v->bass * 5);
    v->turn += (float)v->dt * (0.05f + (v->mid - v->treble) * 0.6f);
    float first = dz - fmodf(v->travel, dz);  // the nearest ring moves toward you, then the next takes its place
    SDL_FColor tint = hsv(v->hue, 0.45f, 1, 1);
    for (int k = rings - 1; k >= 0; k--) {  // far to near
        float z0 = first + k * dz, z1 = z0 + dz;
        float w0 = v->travel + z0, w1 = v->travel + z1;  // depth in the world, for the bends and the texture
        float bx0 = (sinf(w0 * 0.9f + t * 0.3f) - sinf(v->travel * 0.9f + t * 0.3f)) * 0.8f, by0 = (cosf(w0 * 0.7f) - cosf(v->travel * 0.7f)) * 0.6f;
        float bx1 = (sinf(w1 * 0.9f + t * 0.3f) - sinf(v->travel * 0.9f + t * 0.3f)) * 0.8f, by1 = (cosf(w1 * 0.7f) - cosf(v->travel * 0.7f)) * 0.6f;
        float fog0 = fminf(1, powf(fmaxf(0, 1 - z0 / (rings * dz)), 1.6f) * 1.4f + v->beat * 0.2f), fog1 = fminf(1, powf(fmaxf(0, 1 - z1 / (rings * dz)), 1.6f) * 1.4f + v->beat * 0.2f);
        SDL_FColor c0 = { tint.r * fog0, tint.g * fog0, tint.b * fog0, 1 }, c1 = { tint.r * fog1, tint.g * fog1, tint.b * fog1, 1 };
        for (int s = 0; s < sides; s++) {
            float a0 = s * 6.2832f / sides, a1 = (s + 1) * 6.2832f / sides, u0 = (float)s / sides * 4 + v->turn, u1 = (float)(s + 1) / sides * 4 + v->turn;
            quad(&v->m, vert(&v->m, cx + (bx1 + cosf(a0)) / z1 * focal, cy + (by1 + sinf(a0)) / z1 * focal, c1, u0, w1),
                 vert(&v->m, cx + (bx1 + cosf(a1)) / z1 * focal, cy + (by1 + sinf(a1)) / z1 * focal, c1, u1, w1),
                 vert(&v->m, cx + (bx0 + cosf(a1)) / z0 * focal, cy + (by0 + sinf(a1)) / z0 * focal, c0, u1, w0),
                 vert(&v->m, cx + (bx0 + cosf(a0)) / z0 * focal, cy + (by0 + sinf(a0)) / z0 * focal, c0, u0, w0));
        }
    }
    draw(v, v->pattern, SDL_BLENDMODE_NONE);
    // The light at the end, where the far rings meet.
    float zf = first + (rings - 1) * dz, wf = v->travel + zf;
    float bx = (sinf(wf * 0.9f + t * 0.3f) - sinf(v->travel * 0.9f + t * 0.3f)) * 0.8f, by = (cosf(wf * 0.7f) - cosf(v->travel * 0.7f)) * 0.6f;
    sprite(&v->m, cx + bx / zf * focal, cy + by / zf * focal, a.h * (0.05f + v->bass * 0.12f + v->beat * 0.05f), hsv(v->hue + 0.5f, 0.3f, 1, 1));
    draw(v, v->glow, SDL_BLENDMODE_ADD);
}

// Milkdrop's feedback: each frame is the last one seen through a warp mesh (zoomed in, turned and
// rippled, a little darker), with a ring scope drawn on top, so everything leaves trails.
static void fx_feedback(viz *v, SDL_FRect a) {
    int w = (int)a.w, h = (int)a.h;
    if (w != v->feed_w || h != v->feed_h) {
        for (int i = 0; i < 2; i++) {
            if (v->feed[i]) SDL_DestroyTexture(v->feed[i]);
            v->feed[i] = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
            SDL_SetRenderTarget(v->ren, v->feed[i]);
            SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
            SDL_RenderClear(v->ren);
        }
        SDL_SetRenderTarget(v->ren, NULL);
        v->feed_w = w, v->feed_h = h;
    }
    SDL_Texture *last = v->feed[0], *next = v->feed[1];
    SDL_Rect clip;
    bool clipped = SDL_GetRenderClipRect(v->ren, &clip) && !SDL_RectEmpty(&clip);
    SDL_SetRenderTarget(v->ren, next);
    float t = (float)v->t, zoom = 1.02f + v->beat * 0.06f + v->bass * 0.01f, rot = 0.006f + (v->mid - v->treble) * 0.02f;
    float cs = cosf(rot) / zoom, sn = sinf(rot) / zoom, cx = w / 2.0f, cy = h / 2.0f, u = h / 200.0f;
    const int gx = 48, gy = 27;
    SDL_FColor fade = gray(0.955f, 1);
    for (int j = 0; j <= gy; j++)
        for (int i = 0; i <= gx; i++) {
            float x = (float)w * i / gx, y = (float)h * j / gy, dx = x - cx, dy = y - cy;
            float sx = cx + dx * cs - dy * sn + sinf(y / u * 0.06f + t * 1.7f) * 0.6f * u, sy = cy + dx * sn + dy * cs + cosf(x / u * 0.05f + t * 1.3f) * 0.6f * u;
            vert(&v->m, x, y, fade, sx / w, sy / h);
        }
    for (int j = 0; j < gy; j++)
        for (int i = 0; i < gx; i++) {
            int k = j * (gx + 1) + i;
            quad(&v->m, k, k + 1, k + gx + 2, k + gx + 1);
        }
    SDL_SetRenderTextureAddressMode(v->ren, SDL_TEXTURE_ADDRESS_CLAMP, SDL_TEXTURE_ADDRESS_CLAMP);
    draw(v, last, SDL_BLENDMODE_NONE);
    SDL_SetRenderTextureAddressMode(v->ren, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
    // A ring scope: the waveform wrapped around a circle that swells with the bass.
    float r = h * (0.18f + v->bass * 0.12f), px0 = 0, py0 = 0;
    int n = 360;
    for (int i = 0; i <= n; i++) {
        float ang = i * 6.2832f / n + t * 0.4f, s = v->wave[FFT_N - 1440 + (i % n) * 4] * h * 0.35f;
        float x = cx + cosf(ang) * (r + s), y = cy + sinf(ang) * (r + s);
        SDL_FColor c = hsv(v->hue + (float)i / n, 0.8f, 1, 1);
        if (i) seg(&v->m, px0, py0, x, y, 1.3f * u, c, c);
        px0 = x, py0 = y;
    }
    if (v->beat > 0.9f)  // sparks on the beat
        for (int i = 0; i < 40; i++) sprite(&v->m, rnd(v) * w, rnd(v) * h, u, gray(1, 1));
    draw(v, NULL, SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(v->ren, NULL);
    if (clipped) SDL_SetRenderClipRect(v->ren, &clip);
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(v->ren, next, NULL, &a);
    v->feed[0] = next, v->feed[1] = last;
}

// Fire fed by the spectrum. A cellular effect, so it is worked out on a grid a third of the height
// and drawn smoothly scaled.
static void fx_fire(viz *v, SDL_FRect a) {
    int h = (int)(a.h / 3), w;
    h = h < 120 ? 120 : h > 360 ? 360 : h;
    w = (int)(h * a.w / a.h);
    if (fit(v, &v->fire, w, h)) free(v->heat), v->heat = calloc((size_t)(w * (h + 2)), 1);
    uint8_t *heat = v->heat;
    // Fuel follows each band over about a quarter of a second, so the columns do not all flare on
    // every kick at once (which rises as horizontal stripes).
    for (int b = 0; b < BANDS; b++) v->fuel[b] += (v->band[b] - v->fuel[b]) * fminf(1, (float)v->dt * 4);
    for (int x = 0; x < w; x++) {
        // Embers on or off at random, more often where it is louder: flames rather than stripes.
        float s = v->fuel[x * BANDS / w], chance = 0.15f + 0.85f * s + v->beat * 0.1f;
        heat[(h + 1) * w + x] = heat[h * w + x] = rnd(v) < chance ? 255 : 0;
    }
    // Heat rises, averaged from below and sampled a step to either side at random so the flames
    // lick, and cools a little each row (scaled so the flames reach as high at any grid size).
    float cool = 200.0f / h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int j = x + (int)(rnd(v) * 3) - 1, l = j > 0 ? j - 1 : 0, c = j < 0 ? 0 : j >= w ? w - 1 : j, r = c < w - 1 ? c + 1 : c;
            int sum = heat[(y + 1) * w + l] + heat[(y + 1) * w + c] + heat[(y + 1) * w + r] + heat[(y + 2) * w + c];
            int k = (sum + (int)(rnd(v) * 4)) / 4 - (rnd(v) < 0.5f * cool) - (rnd(v) < 0.5f * cool);
            heat[y * w + x] = (uint8_t)(k < 0 ? 0 : k);
        }
    for (int i = 0; i < w * h; i++) {
        float f = heat[i] / 255.0f;  // black, red, orange, yellow, white
        float r = fminf(1, f * 3), g = fminf(1, fmaxf(0, (f - 0.33f) * 2.2f)), b = fminf(1, fmaxf(0, (f - 0.7f) * 3.3f));
        v->fire.px[i] = (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
    }
    show(v, &v->fire, a);
}

// Destroys the Bend effect's texture, which CUDA may have registered.
static void bend_release(viz *v) {
#ifdef _WIN32
    if (v->bend_interop == 1) bendviz_release_d3d11();
#endif
    if (v->bend_tex) SDL_DestroyTexture(v->bend_tex);
    v->bend_tex = NULL;
}

// Counts over whole seconds: frames the player drew, how many brought a new Bend frame (fresh), and
// the time taking them into the texture.
static void bend_count_frame(viz *v, bool fresh, double take_ms) {
    uint64_t now = SDL_GetTicksNS();
    if (!v->bend_count_from) v->bend_count_from = now;
    v->bend_calls++, v->bend_count += fresh, v->bend_empty += !fresh;
    if (v->bend_last_call) {
        uint64_t gap = now - v->bend_last_call;
        v->bend_short += gap < 8 * SDL_NS_PER_MS, v->bend_long += gap > 25 * SDL_NS_PER_MS;
    }
    v->bend_last_call = now;
    v->bend_take_ms += take_ms, v->bend_take_max = SDL_max(v->bend_take_max, take_ms);
    if (now - v->bend_count_from >= SDL_NS_PER_SECOND) {
        double secs = (now - v->bend_count_from) / 1e9;
        v->bend_fps = v->bend_count / secs;
        unsigned long long drawn, dropped, faults;
        double took, began;
        bendviz_cycle(&drawn, &dropped, &took, &began, &faults);
        unsigned long long nd = drawn - v->bend_drawn0;
        snprintf(v->bend_player, sizeof v->bend_player, "bend taken %d, none %d; take %.2f ms (max %.1f); faults %.1f", v->bend_count, v->bend_empty, v->bend_take_ms / (v->bend_count ? v->bend_count : 1), v->bend_take_max, nd ? (double)(faults - v->bend_faults0) / nd : 0);
        v->bend_drawn0 = drawn, v->bend_dropped0 = dropped, v->bend_took0 = took, v->bend_began0 = began, v->bend_faults0 = faults;
        v->bend_count = v->bend_calls = v->bend_empty = v->bend_short = v->bend_long = 0, v->bend_take_ms = v->bend_take_max = 0;
        v->bend_count_from = now;
    }
}

// The Bend effect's texture, w x h. Static, because Direct3D 11 makes streaming textures dynamic
// resources, which CUDA cannot write; ARGB, which Direct3D 11 stores in Bend's byte order (the
// alpha byte is unused, drawn without blending).
static void bend_texture(viz *v, int w, int h) {
    bend_release(v);
    v->bend_tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    v->bend_w = w, v->bend_h = h;
}

// The plasma again, written in Bend (bend/viz.bend), drawing every pixel of the area, by the same
// function on the GPU or on the CPU's threads, as g chooses. It runs beside the player and draws
// at its own pace: each frame asks for the next and shows the newest one finished.
static void fx_bend(viz *v, SDL_FRect a, gs_glyphs *g, gs_fontset *f) {
    if (!v->bend_started) v->bend_started = bendviz_start("512MB"), v->bend_on_gpu = true;
    int w = (int)a.w < BENDVIZ_MAX ? (int)a.w : BENDVIZ_MAX, h = (int)a.h < BENDVIZ_MAX ? (int)a.h : BENDVIZ_MAX;
    float params[5] = { (float)v->t, v->bass, v->mid, v->hue, v->beat };
    bendviz_request(params, w, h, v->bend_on_gpu);
    // Without vsync the player would present as fast as it can, most often the frame already on
    // screen, and each present takes the GPU from Bend: a moment's wait for Bend's next frame (drawn
    // on the GPU, a millisecond or two) lets the player show only new ones.
    if (!v->vsync && v->bend_shown && v->bend_drawn_gpu) bendviz_wait(8);
    take_from = SDL_GetTicksNS();
    int fw, fh;
    bool gpu;
    double ms;
    bool fresh = false;
    uint64_t take_from;
#ifdef _WIN32
    // On Direct3D 11, a frame drawn on the GPU goes into the texture on the GPU, never crossing to
    // the host; anything else (the CPU's frames, or interop failing) comes through the host.
    if (!v->bend_interop) {
        const char *r = SDL_GetRendererName(v->ren);
        v->bend_interop = r && !strcmp(r, "direct3d11") ? 1 : -1;
        bendviz_device_frames(v->bend_interop == 1);
    }
    if (v->bend_interop == 1) {
        SDL_FlushRenderer(v->ren);  // nothing queued may still be using the texture
        void *d3d = v->bend_tex ? SDL_GetPointerProperty(SDL_GetTextureProperties(v->bend_tex), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL) : NULL;
        int r = bendviz_to_d3d11(d3d, v->bend_w, v->bend_h, &fw, &fh, &gpu, &ms);
        if (r == 0 && fw > 0 && fh > 0 && (fw != v->bend_w || fh != v->bend_h)) {
            bend_texture(v, fw, fh);
            d3d = SDL_GetPointerProperty(SDL_GetTextureProperties(v->bend_tex), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL);
            r = bendviz_to_d3d11(d3d, fw, fh, &fw, &fh, &gpu, &ms);
        }
        if (r == 1) v->bend_drawn_gpu = gpu, v->bend_ms = ms, v->bend_shown = true, v->bend_on_device = true, fresh = true;
        if (r < 0) {
            SDL_Log("bend: graphics interop failed; frames come through the host");
            v->bend_interop = -1;
            bendviz_device_frames(false);
        }
    }
#endif
    const uint32_t *frame = bendviz_borrow(&fw, &fh, &gpu, &ms);
    if (frame) {
        if (fw != v->bend_w || fh != v->bend_h) bend_texture(v, fw, fh);
        SDL_UpdateTexture(v->bend_tex, NULL, frame, fw * 4);
        bendviz_return();
        v->bend_drawn_gpu = gpu, v->bend_ms = ms;
        v->bend_shown = true, v->bend_on_device = false;
        fresh = true;
    }
    bend_count_frame(v, fresh, (SDL_GetTicksNS() - take_from) / 1e6);
    if (v->bend_shown && v->bend_tex) {
        SDL_SetTextureBlendMode(v->bend_tex, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(v->ren, v->bend_tex, NULL, &a);
    }
    char label[96];
    const char *where = !v->bend_shown ? "starting" : v->bend_drawn_gpu ? "GPU" : v->bend_on_gpu && !bendviz_gpu() ? "CPU (no GPU found)" : "CPU";
    if (v->bend_shown) snprintf(label, sizeof label, "Bend on %s, %.1f ms a frame (%dx%d)", where, v->bend_ms, v->bend_w, v->bend_h);
    else snprintf(label, sizeof label, "Bend %s", where);
    // Top left, under where the effect's name shows (the stats overlay has the top right).
    float px = fmaxf(14, a.h * 0.045f), ly = a.y + a.h * 0.09f * 2.3f;
    gs_fontset_draw(g, f, px, a.x + a.h * 0.054f + 2, ly + 2, label, (SDL_FColor){ 0, 0, 0, 0.7f });
    gs_fontset_draw(g, f, px, a.x + a.h * 0.054f, ly, label, (SDL_FColor){ 1, 1, 1, 1 });
}

static int by_z(const void *a, const void *b) {
    float za = ((const vec3 *)a)->z, zb = ((const vec3 *)b)->z;
    return (za < zb) - (za > zb);
}

// A warp starfield around a turning sphere of bobs, each pushed out by its own band.
static void fx_bobs(viz *v, SDL_FRect a) {
    float cx = a.x + a.w / 2, cy = a.y + a.h / 2, u = a.h / 200, speed = 0.4f + v->bass * 4 + v->beat * 3, fov = a.h * 0.45f;
    for (int i = 0; i < STARS; i++) {
        vec3 *s = &v->stars[i];
        float z0 = s->z;
        s->z -= speed * (float)v->dt;
        if (s->z < 0.05f) s->x = rnd(v) * 2 - 1, s->y = rnd(v) * 2 - 1, s->z = z0 = 1 + rnd(v);
        float b = fminf(1, (2 - s->z) * 0.6f);
        seg(&v->m, cx + s->x / z0 * fov, cy + s->y / z0 * fov, cx + s->x / s->z * fov, cy + s->y / s->z * fov, u * (0.4f + b * 0.8f),
            (SDL_FColor){ 0.8f, 0.85f, 1, 0 }, (SDL_FColor){ 0.8f, 0.85f, 1, b });
    }
    draw(v, NULL, SDL_BLENDMODE_ADD);
    vec3 p[BOBS];
    float t = (float)v->t, ay = t * 0.7f, ax = t * 0.43f, R = a.h * (0.26f + v->beat * 0.05f);
    for (int i = 0; i < BOBS; i++) {
        float lat = acosf(1 - 2 * (i + 0.5f) / BOBS), lon = i * 2.39996f;  // evenly spread (a Fibonacci sphere)
        float r = 1 + v->band[(int)(lat / 3.1416f * (BANDS - 1))] * 0.6f;
        float x = sinf(lat) * cosf(lon) * r, y = cosf(lat) * r, z = sinf(lat) * sinf(lon) * r;
        float x2 = x * cosf(ay) + z * sinf(ay), z2 = -x * sinf(ay) + z * cosf(ay);
        float y2 = y * cosf(ax) - z2 * sinf(ax), z3 = y * sinf(ax) + z2 * cosf(ax);
        p[i] = (vec3){ x2, y2, z3 };
    }
    qsort(p, BOBS, sizeof *p, by_z);  // far ones first
    for (int i = 0; i < BOBS; i++) {
        float persp = 3 / (3 + p[i].z);
        sprite(&v->m, cx + p[i].x * R * persp, cy + p[i].y * R * persp, (1.6f + v->beat) * persp * u * 1.6f, hsv(v->hue + p[i].y * 0.15f, 0.7f, 0.5f + 0.5f * persp, 1));
    }
    draw(v, v->ball, SDL_BLENDMODE_BLEND);
}

// ---- Drawing ----

// Each character of the text on its own, riding a sine wave, in a rolling rainbow.
static void scroller(viz *v, SDL_FRect a, const char *text, gs_glyphs *g, gs_fontset *f) {
    float px = fmaxf(16, a.h * 0.075f), total = gs_fontset_width(f, px, text), speed = px * 4.5f;
    float x = a.x + a.w - fmodf((float)v->t * speed, total + a.w), base = a.y + a.h - px * 1.1f;
    char ch[8];
    for (const char *s = text; *s && x < a.x + a.w;) {
        int n = 1;
        while (s[n] && (s[n] & 0xC0) == 0x80) n++;
        memcpy(ch, s, (size_t)n), ch[n] = 0;
        s += n;
        float cw = gs_fontset_width(f, px, ch);
        if (x + cw > a.x) {
            float y = base + sinf(x * 0.012f + (float)v->t * 3.2f) * px * 0.55f;
            gs_fontset_draw(g, f, px, x + 2, y + 2, ch, (SDL_FColor){ 0, 0, 0, 0.6f });  // a drop shadow
            gs_fontset_draw(g, f, px, x, y, ch, hsv(x / a.w * 0.8f - (float)v->t * 0.2f, 0.55f, 1, 1));
        }
        x += cw;
    }
}

void viz_draw(viz *v, SDL_FRect a, double t, const char *title, const char *artist, gs_glyphs *g, gs_fontset *f) {
    v->dt = v->t ? fmin(0.1, t - v->t) : 0;
    v->t = t;
    v->hue += (float)v->dt * 0.02f;
    if (strcmp(title, v->last_title)) {  // a new track: a new effect, in auto mode
        if (v->last_title[0] && v->automatic) viz_step(v, 1);
        SDL_strlcpy(v->last_title, title, sizeof v->last_title);
    }
    if (v->automatic && t - v->fx_since > AUTO_SECONDS) viz_step(v, 1);
    if (!v->fx_since) v->fx_since = t;

    a.x = floorf(a.x), a.y = floorf(a.y), a.w = floorf(a.w), a.h = floorf(a.h);
    SDL_SetRenderClipRect(v->ren, &(SDL_Rect){ (int)a.x, (int)a.y, (int)a.w, (int)a.h });
    SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(v->ren, &a);
    switch (v->fx) {
    case FX_SPECTRUM: fx_spectrum(v, a); break;
    case FX_PLASMA: fx_plasma(v, a); break;
    case FX_TUNNEL: fx_tunnel(v, a); break;
    case FX_FEEDBACK: fx_feedback(v, a); break;
    case FX_FIRE: fx_fire(v, a); break;
    case FX_BOBS: fx_bobs(v, a); break;
    default: fx_bend(v, a, g, f); break;
    }
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);

    if (t < v->name_until) {  // the effect's name, big, for a moment after it changes
        float px = a.h * 0.09f, alpha = (float)fmin(1, (v->name_until - t) / 0.6);
        gs_fontset_draw(g, f, px, a.x + px * 0.6f + 3, a.y + px * 1.4f + 3, fx_names[v->fx], (SDL_FColor){ 0, 0, 0, alpha * 0.7f });
        gs_fontset_draw(g, f, px, a.x + px * 0.6f, a.y + px * 1.4f, fx_names[v->fx], (SDL_FColor){ 1, 1, 1, alpha });
    }
    if (v->scroller && title[0]) {
        char text[900];
        SDL_snprintf(text, sizeof text, "\xE2\x99\xAA %s%s%s \xE2\x99\xAA     gesso gtube     greetings to winamp, milkdrop, avs, future crew, farbrausch and the whole demoscene     ",
                     title, artist[0] ? "  \xC2\xB7  " : "", artist);
        scroller(v, a, text, g, f);
    }
    SDL_SetRenderClipRect(v->ren, NULL);
}

// ---- Setup and settings ----

// A texture from a function of (x, y) in 0..1 giving RGBA.
static SDL_Texture *make(viz *v, int size, void (*fn)(float x, float y, float out[4])) {
    uint32_t *p = malloc(sizeof *p * (size_t)(size * size));
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float c[4];
            fn((x + 0.5f) / size, (y + 0.5f) / size, c);
            p[y * size + x] = (uint32_t)(c[0] * 255) << 24 | (uint32_t)(c[1] * 255) << 16 | (uint32_t)(c[2] * 255) << 8 | (uint32_t)(c[3] * 255);
        }
    SDL_Texture *t = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, size, size);
    SDL_UpdateTexture(t, NULL, p, size * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    free(p);
    return t;
}

static void pattern(float x, float y, float out[4]) {  // the XOR texture of a thousand intros, in bands
    int xi = (int)(x * 256), yi = (int)(y * 256), band = ((xi >> 5) + (yi >> 5)) & 1;
    float f = ((xi ^ yi) & 255) / 255.0f;
    out[0] = fminf(1, (60 + 150 * f + band * 40) / 255), out[1] = (30 + 90 * f * f) / 255, out[2] = fminf(1, (90 + 160 * (1 - f) + band * 30) / 255), out[3] = 1;
}

static void ball(float x, float y, float out[4]) {  // a sphere lit from the top left, white, to be tinted
    float dx = x * 2 - 1, dy = y * 2 - 1, d = dx * dx + dy * dy;
    if (d > 1) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    float z = sqrtf(1 - d), light = fmaxf(0, (-dx * 0.5f - dy * 0.6f + z * 0.62f)), spec = powf(light, 24);
    float k = fminf(1, 0.25f + light * 0.85f + spec);
    out[0] = out[1] = out[2] = k, out[3] = fminf(1, (1 - d) * 12);  // a soft edge
}

static void glow(float x, float y, float out[4]) {
    float dx = x * 2 - 1, dy = y * 2 - 1, d = fmaxf(0, 1 - sqrtf(dx * dx + dy * dy));
    out[0] = out[1] = out[2] = 1, out[3] = d * d;
}

viz *viz_new(SDL_Renderer *ren, int rate) {
    viz *v = calloc(1, sizeof *v);
    v->ren = ren, v->rate = rate, v->seed = 12345;
    v->automatic = v->scroller = true;
    for (int i = 0; i < FFT_N; i++) v->window[i] = 0.5 - 0.5 * cos(6.283185307179586 * i / (FFT_N - 1));  // Hann
    for (int i = 0; i < STARS; i++) v->stars[i] = (vec3){ rnd(v) * 2 - 1, rnd(v) * 2 - 1, 0.05f + rnd(v) * 2 };
    v->pattern = make(v, 256, pattern);
    v->ball = make(v, 64, ball);
    v->glow = make(v, 64, glow);
    for (int i = 0; i < 4096; i++) v->sine[i] = sinf(i * 6.2831853f / 4096);
    return v;
}

void viz_free(viz *v) {
    if (!v) return;
    bend_release(v);
    SDL_Texture *all[] = { v->pattern, v->ball, v->glow, v->feed[0], v->feed[1], v->fire.tex, v->plasma.tex, v->bend_tex };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        if (all[i]) SDL_DestroyTexture(all[i]);
    free(v->heat), free(v->radius), free(v->fire.px), free(v->plasma.px), free(v->m.v), free(v->m.i);
    free(v);
}

void viz_step(viz *v, int dir) {
    v->fx = (v->fx + dir + FX_COUNT) % FX_COUNT;
    v->fx_since = v->t;
    v->name_until = v->t + 2.5;
    v->feed_w = 0;  // feedback starts from black
}

const char *viz_name(const viz *v) { return fx_names[v->fx]; }
bool viz_is_bend(const viz *v) { return v->fx == FX_BEND; }

// Three lines of at most 63 characters (the stats overlay's width; it shows 12 lines in all).
bool viz_bend_stats(const viz *v, char *out, size_t size) {
    if (!v->bend_shown) return false;  // not drawn yet
    double draw, wait, copy;
    bendviz_times(&draw, &wait, &copy);
    int n = snprintf(out, size, "bend %s %.1f ms (copy %.1f), %.0f fps, %dx%d", v->bend_drawn_gpu ? "gpu" : "cpu", v->bend_ms, copy, v->bend_fps, v->bend_w, v->bend_h);
    if (v->bend_drawn_gpu && n > 0 && (size_t)n < size) {
        double before, launch, after;
        bendviz_host_parts(&before, &launch, &after);
        n += snprintf(out + n, size - (size_t)n, "\nbend draw %.1f: gpu %.1f, before %.1f, launch %.1f, after %.1f", draw, wait, before, launch, after);
    }
    if (v->bend_player[0] && n > 0 && (size_t)n < size) snprintf(out + n, size - (size_t)n, "\n%s", v->bend_player);
    return true;
}
void viz_bend_switch(viz *v) { v->bend_on_gpu = !v->bend_on_gpu; }
void viz_set_vsync(viz *v, bool on) {
    v->vsync = on;
#ifdef _WIN32
    bendviz_interop_apart(on);
#else
    (void)on;
#endif
}
int viz_index(const viz *v, int *count) { *count = FX_COUNT; return v->fx; }
void viz_set_auto(viz *v, bool on) { v->automatic = on, v->fx_since = v->t; }
void viz_set_scroller(viz *v, bool on) { v->scroller = on; }
bool viz_get_auto(const viz *v) { return v->automatic; }
bool viz_get_scroller(const viz *v) { return v->scroller; }

// ---- A test signal ----

void viz_test_signal(double t0, float *lr, int frames, int rate) {
    static const float bassline[8] = { 55, 55, 65.4f, 55, 73.4f, 55, 82.4f, 49 };
    for (int i = 0; i < frames; i++) {
        double t = t0 + (double)i / rate, beat = t * 128 / 60;  // 128 bpm
        double pb = beat - floor(beat), p8 = beat * 2 - floor(beat * 2);
        float kick = (float)(sin(6.2832 * (50 * pb * 0.5 + 90 * (1 - exp(-pb * 30)) / 30 * 10)) * exp(-pb * 9));
        uint32_t hash = (uint32_t)(uint64_t)llround(t * rate) * 2654435761u;  // white noise from the sample index
        float noise = (hash >> 8) / 8388608.0f - 1;
        float hat = p8 > 0.5 ? noise * (float)exp(-(p8 - 0.5) * 60) * 0.35f : 0;
        float f = bassline[(int)(beat * 2) % 8];
        float saw = (float)(2 * (t * f - floor(t * f)) - 1) * 0.3f * (float)exp(-p8 * 3);
        float pad = 0;
        static const float chord[3] = { 220, 277.2f, 329.6f };
        for (int k = 0; k < 3; k++) pad += (float)sin(6.2832 * chord[k] * (1 + 0.5 * floor(fmod(beat / 8, 2))) * t) * 0.06f;
        float s = kick * 0.8f + hat + saw + pad * (float)(0.6 + 0.4 * sin(t * 0.7));
        lr[2 * i] = lr[2 * i + 1] = s;
    }
}
