#include "viz.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gs_dsp.h"

#define FFT_N 2048
#define BANDS 64
#define STARS 500
#define BOBS 180
#define AUTO_SECONDS 40

enum { FX_SPECTRUM, FX_PLASMA, FX_TUNNEL, FX_FEEDBACK, FX_FIRE, FX_BOBS, FX_COUNT };
static const char *const fx_names[FX_COUNT] = { "Spectrum", "Plasma", "Tunnel", "Feedback", "Fire", "Stars & bobs" };

typedef struct { float x, y, z; } vec3;

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
    int fx, w, h;
    uint32_t *px, *prev;
    uint8_t *heat;
    uint16_t *tunnel;       // per pixel: angle and depth into the texture
    uint32_t texture[256 * 256];
    SDL_Texture *tex;
    vec3 stars[STARS];
    float hue;              // drifts, and jumps on beats
    double t, fx_since, name_until;
    char last_title[256];
    bool automatic, scroller;
    uint32_t seed;
};

// ---- Small helpers ----

static uint32_t rgb(float r, float g, float b) {
    int R = r < 0 ? 0 : r > 255 ? 255 : (int)r, G = g < 0 ? 0 : g > 255 ? 255 : (int)g, B = b < 0 ? 0 : b > 255 ? 255 : (int)b;
    return (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
}

static uint32_t hsv(float h, float s, float v) {
    h = (h - floorf(h)) * 6;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i) {
    case 0: r = v, g = u, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = u; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = u, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return rgb(r * 255, g * 255, b * 255);
}

static uint32_t add(uint32_t a, uint32_t b) {
    uint32_t r = ((a >> 16) & 255) + ((b >> 16) & 255), g = ((a >> 8) & 255) + ((b >> 8) & 255), bl = (a & 255) + (b & 255);
    return (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
}

static uint32_t scale(uint32_t c, float k) { return rgb(((c >> 16) & 255) * k, ((c >> 8) & 255) * k, (c & 255) * k); }

static float rnd(viz *v) {
    v->seed = v->seed * 1664525u + 1013904223u;
    return (v->seed >> 8) / 16777216.0f;
}

static void plot(viz *v, int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < v->w && y < v->h) v->px[y * v->w + x] = add(v->px[y * v->w + x], c);
}

static void line(viz *v, float x0, float y0, float x1, float y1, uint32_t c) {
    int n = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= n; i++) plot(v, (int)(x0 + (x1 - x0) * i / n), (int)(y0 + (y1 - y0) * i / n), c);
}

static void disc(viz *v, float cx, float cy, float r, uint32_t c) {
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++)
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            float d = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (r * r);
            if (d <= 1 && x >= 0 && y >= 0 && x < v->w && y < v->h) v->px[y * v->w + x] = scale(c, 1.15f - 0.55f * d);  // a lit ball
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

static void fx_spectrum(viz *v) {
    int w = v->w, h = v->h, base = (int)(h * 0.78f);
    for (int i = 0; i < w * h; i++) v->px[i] = 0;
    for (int y = 4; y < base; y += 6)  // a faint dotted grid
        for (int x = 2; x < w; x += 6) v->px[y * w + x] = 0x1a1a1a;
    int bars = 32, gap = 1, bw = (w - 8) / bars - gap, x0 = (w - bars * (bw + gap)) / 2;
    float top = h * 0.36f;
    for (int b = 0; b < bars; b++) {
        float val = fmaxf(v->band[2 * b], v->band[2 * b + 1]), pk = fmaxf(v->peak[2 * b], v->peak[2 * b + 1]);
        int bh = (int)(val * (base - top)), ph = (int)(pk * (base - top)), x = x0 + b * (bw + gap);
        for (int y = 0; y < bh; y++) {
            float f = (float)y / (base - top);  // Winamp's green, through yellow, to red
            uint32_t c = f < 0.5f ? rgb(40 + f * 2 * 215, 200 + f * 55, 30) : rgb(255, 255 - (f - 0.5f) * 2 * 215, 30);
            for (int k = 0; k < bw; k++) {
                v->px[(base - 1 - y) * w + x + k] = c;
                int ry = base + 1 + y / 2;  // a dim reflection on the floor
                if (ry < h && y % 2 == 0) v->px[ry * w + x + k] = scale(c, 0.22f * (1 - (float)(ry - base) / (h - base)));
            }
        }
        for (int k = 0; k < bw; k++) plot(v, x + k, base - 1 - ph, 0xd0d0d0);
    }
    // The oscilloscope above, 576 samples wide like Winamp's.
    float cy = h * 0.19f, amp = h * 0.15f;
    for (int x = 0; x < w; x++) {
        float s0 = v->wave[FFT_N - 576 + (x * 575) / w], s1 = v->wave[FFT_N - 576 + ((x + 1) * 575) / w];
        uint32_t c = hsv(v->hue + x / (float)w * 0.3f, 0.6f, 1);
        line(v, x, cy - s0 * amp * 2, x + 1, cy - s1 * amp * 2, c);
    }
}

static void fx_plasma(viz *v) {
    int w = v->w, h = v->h;
    float t = (float)v->t, zoom = 0.045f * (1 + v->bass * 0.5f), cx = w / 2.0f, cy = h / 2.0f;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        float f = i / 256.0f;
        pal[i] = hsv(v->hue + f * 0.6f + t * 0.03f, 0.75f, 0.35f + 0.5f * (0.5f + 0.5f * sinf(f * 6.2832f * 2 + t)) + v->beat * 0.25f);
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = (x - cx) * zoom, dy = (y - cy) * zoom;
            float s = sinf(dx + t) + sinf(dy * 1.3f + t * 1.1f) + sinf((dx + dy) * 0.7f + t * 0.7f) + sinf(sqrtf(dx * dx + dy * dy) * 1.5f - t * 2 - v->mid * 3);
            v->px[y * w + x] = pal[(int)((s + 4) * 32 + t * 40) & 255];
        }
    for (int x = 0; x < w; x++) {  // the waveform, glowing across the middle
        float s0 = v->wave[FFT_N - 1024 + x * 1023 / w], s1 = v->wave[FFT_N - 1024 + (x + 1) * 1023 / w];
        line(v, x, cy - s0 * h * 0.6f, x + 1, cy - s1 * h * 0.6f, 0x606060);
    }
}

static void make_texture(viz *v) {
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int xr = (x ^ y) & 255, band = ((x >> 5) + (y >> 5)) & 1;
            float f = xr / 255.0f;
            v->texture[y * 256 + x] = rgb(60 + 150 * f + band * 40, 30 + 90 * f * f, 90 + 160 * (1 - f) + band * 30);
        }
}

static void fx_tunnel(viz *v) {
    int w = v->w, h = v->h;
    if (!v->tunnel) {
        v->tunnel = malloc(sizeof *v->tunnel * 2 * (size_t)(w * h));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float dx = x - w / 2.0f, dy = y - h / 2.0f, d = sqrtf(dx * dx + dy * dy) + 0.5f;
                v->tunnel[2 * (y * w + x)] = (uint16_t)((int)(atan2f(dy, dx) / 6.2832f * 512 + 512) & 255);
                v->tunnel[2 * (y * w + x) + 1] = (uint16_t)fminf(65535, 36 * h / d);
            }
    }
    static float depth, turn;
    depth += (float)(0.016 * (70 + v->bass * 420));
    turn += (float)(0.016 * (8 + (v->mid - v->treble) * 60));
    uint32_t tint = hsv(v->hue, 0.5f, 1);
    float tr = ((tint >> 16) & 255) / 255.0f, tg = ((tint >> 8) & 255) / 255.0f, tb = (tint & 255) / 255.0f;
    for (int i = 0; i < w * h; i++) {
        int a = v->tunnel[2 * i], d = v->tunnel[2 * i + 1];
        uint32_t c = v->texture[((d + (int)depth) & 255) * 256 + ((a + (int)turn) & 255)];
        float fog = fminf(1, 1.9f * h / (d + 1.0f) * 0.12f + v->beat * 0.3f);  // dark in the distance
        v->px[i] = rgb(((c >> 16) & 255) * fog * (0.5f + tr), ((c >> 8) & 255) * fog * (0.5f + tg), (c & 255) * fog * (0.5f + tb));
    }
    disc(v, w / 2.0f, h / 2.0f, 3 + v->bass * h * 0.12f, hsv(v->hue + 0.5f, 0.3f, 0.6f + v->beat * 0.4f));  // light at the end
}

static void fx_feedback(viz *v) {
    int w = v->w, h = v->h;
    memcpy(v->prev, v->px, sizeof *v->px * (size_t)(w * h));
    float t = (float)v->t, zoom = 1.02f + v->beat * 0.06f + v->bass * 0.01f, rot = 0.006f + (v->mid - v->treble) * 0.02f;
    float cs = cosf(rot) / zoom, sn = sinf(rot) / zoom, cx = w / 2.0f, cy = h / 2.0f;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            // Where this pixel was last frame: zoomed in, turned, and rippled.
            float dx = x - cx, dy = y - cy;
            float sx = cx + dx * cs - dy * sn + sinf(y * 0.06f + t * 1.7f) * 0.6f, sy = cy + dx * sn + dy * cs + cosf(x * 0.05f + t * 1.3f) * 0.6f;
            int ix = (int)sx, iy = (int)sy;
            if (ix < 0 || iy < 0 || ix >= w - 1 || iy >= h - 1) { v->px[y * w + x] = 0; continue; }
            float fx = sx - ix, fy = sy - iy;
            uint32_t a = v->prev[iy * w + ix], b = v->prev[iy * w + ix + 1], c = v->prev[(iy + 1) * w + ix], d = v->prev[(iy + 1) * w + ix + 1];
            float k[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy }, out[3];
            for (int ch = 0; ch < 3; ch++) {
                int sh = 16 - 8 * ch;
                out[ch] = (((a >> sh) & 255) * k[0] + ((b >> sh) & 255) * k[1] + ((c >> sh) & 255) * k[2] + ((d >> sh) & 255) * k[3]) * 0.955f;
            }
            v->px[y * w + x] = rgb(out[0], out[1], out[2]);
        }
    // A ring scope: the waveform wrapped around a circle that swells with the bass.
    float r = h * (0.18f + v->bass * 0.12f);
    int n = 360;
    float px0 = 0, py0 = 0;
    for (int i = 0; i <= n; i++) {
        float ang = i * 6.2832f / n + t * 0.4f, s = v->wave[FFT_N - 1440 + (i % n) * 4] * h * 0.35f;
        float x = cx + cosf(ang) * (r + s), y = cy + sinf(ang) * (r + s);
        if (i) line(v, px0, py0, x, y, hsv(v->hue + i / (float)n, 0.8f, 1));
        px0 = x, py0 = y;
    }
    if (v->beat > 0.9f)  // sparks on the beat
        for (int i = 0; i < 40; i++) plot(v, (int)(rnd(v) * w), (int)(rnd(v) * h), 0xffffff);
}

static void fx_fire(viz *v) {
    int w = v->w, h = v->h;
    uint8_t *heat = v->heat;
    for (int x = 0; x < w; x++) {  // the fuel: each column burns as loud as its part of the spectrum
        // Embers on or off at random, more often where it is louder: flames rather than stripes.
        float s = v->band[x * BANDS / w], chance = 0.15f + 0.85f * s + v->beat * 0.2f;
        heat[(h + 1) * w + x] = heat[h * w + x] = rnd(v) < chance ? 255 : 0;
    }
    // Heat rises, averaged from below and sampled a step to either side at random so the flames
    // lick, and cools by 1 a row on average: the loudest columns reach about two thirds up.
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int j = x + (int)(rnd(v) * 3) - 1, l = j > 0 ? j - 1 : 0, c = j < 0 ? 0 : j >= w ? w - 1 : j, r = c < w - 1 ? c + 1 : c;
            int sum = heat[(y + 1) * w + l] + heat[(y + 1) * w + c] + heat[(y + 1) * w + r] + heat[(y + 2) * w + c];
            int k = (sum + (int)(rnd(v) * 4)) / 4 - (rnd(v) < 0.5f) - (rnd(v) < 0.5f);
            heat[y * w + x] = (uint8_t)(k < 0 ? 0 : k);
        }
    for (int i = 0; i < w * h; i++) {
        float f = heat[i] / 255.0f;  // black, red, orange, yellow, white
        v->px[i] = rgb(f * 3 * 255, (f - 0.33f) * 2.2f * 255, (f - 0.7f) * 3.3f * 255);
    }
}

static int by_z(const void *a, const void *b) {
    float za = ((const vec3 *)a)->z, zb = ((const vec3 *)b)->z;
    return (za < zb) - (za > zb);
}

static void fx_bobs(viz *v) {
    int w = v->w, h = v->h;
    for (int i = 0; i < w * h; i++) v->px[i] = scale(v->px[i], 0.55f);  // short trails
    float cx = w / 2.0f, cy = h / 2.0f, speed = 0.4f + v->bass * 4 + v->beat * 3, fov = h * 0.9f;
    for (int i = 0; i < STARS; i++) {
        vec3 *s = &v->stars[i];
        float z0 = s->z;
        s->z -= speed * 0.016f;
        if (s->z < 0.05f) s->x = rnd(v) * 2 - 1, s->y = rnd(v) * 2 - 1, s->z = z0 = 1 + rnd(v);
        float x0 = cx + s->x / z0 * fov * 0.5f, y0 = cy + s->y / z0 * fov * 0.5f, x1 = cx + s->x / s->z * fov * 0.5f, y1 = cy + s->y / s->z * fov * 0.5f;
        float b = fminf(1, (2 - s->z) * 0.6f);
        line(v, x0, y0, x1, y1, rgb(b * 200, b * 210, b * 255));
    }
    // A sphere of bobs, turning, each pushed out by its own band.
    vec3 p[BOBS];
    float t = (float)v->t, ay = t * 0.7f, ax = t * 0.43f, R = h * (0.26f + v->beat * 0.05f);
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
        float persp = 3 / (3 + p[i].z), size = (1.2f + v->beat) * persp * h / 120;
        disc(v, cx + p[i].x * R * persp, cy + p[i].y * R * persp, size, hsv(v->hue + p[i].y * 0.15f, 0.7f, 0.5f + 0.5f * persp));
    }
}

// ---- Drawing ----

static void resize(viz *v, int w, int h) {
    if (w == v->w && h == v->h) return;
    v->w = w, v->h = h;
    free(v->px), free(v->prev), free(v->heat), free(v->tunnel);
    v->px = calloc((size_t)(w * h), sizeof *v->px);
    v->prev = calloc((size_t)(w * h), sizeof *v->prev);
    v->heat = calloc((size_t)(w * (h + 2)), 1);
    v->tunnel = NULL;
    if (v->tex) SDL_DestroyTexture(v->tex);
    v->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_PIXELART)) SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_NEAREST);
}

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
            uint32_t c = hsv(x / a.w * 0.8f - (float)v->t * 0.2f, 0.55f, 1);
            gs_fontset_draw(g, f, px, x + 2, y + 2, ch, (SDL_FColor){ 0, 0, 0, 0.6f });  // a drop shadow
            gs_fontset_draw(g, f, px, x, y, ch, (SDL_FColor){ ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, 1 });
        }
        x += cw;
    }
}

void viz_draw(viz *v, SDL_FRect a, double t, const char *title, const char *artist, gs_glyphs *g, gs_fontset *f) {
    double dt = v->t ? t - v->t : 0;
    v->t = t;
    v->hue += (float)dt * 0.02f;
    if (strcmp(title, v->last_title)) {  // a new track: a new effect, in auto mode
        if (v->last_title[0] && v->automatic) viz_step(v, 1);
        SDL_strlcpy(v->last_title, title, sizeof v->last_title);
    }
    if (v->automatic && t - v->fx_since > AUTO_SECONDS) viz_step(v, 1);
    if (!v->fx_since) v->fx_since = t;

    // A VGA-ish 200 lines, as wide as the area's shape asks.
    int h = 200, w = (int)(h * a.w / fmaxf(1, a.h));
    w = w < 160 ? 160 : w > 720 ? 720 : w;
    resize(v, w, h);
    switch (v->fx) {
    case FX_SPECTRUM: fx_spectrum(v); break;
    case FX_PLASMA: fx_plasma(v); break;
    case FX_TUNNEL: fx_tunnel(v); break;
    case FX_FEEDBACK: fx_feedback(v); break;
    case FX_FIRE: fx_fire(v); break;
    default: fx_bobs(v); break;
    }
    SDL_UpdateTexture(v->tex, NULL, v->px, w * 4);
    SDL_RenderTexture(v->ren, v->tex, NULL, &a);

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
}

// ---- Setup and settings ----

viz *viz_new(SDL_Renderer *ren, int rate) {
    viz *v = calloc(1, sizeof *v);
    v->ren = ren, v->rate = rate, v->seed = 12345;
    v->automatic = v->scroller = true;
    for (int i = 0; i < FFT_N; i++) v->window[i] = 0.5 - 0.5 * cos(6.283185307179586 * i / (FFT_N - 1));  // Hann
    for (int i = 0; i < STARS; i++) v->stars[i] = (vec3){ rnd(v) * 2 - 1, rnd(v) * 2 - 1, 0.05f + rnd(v) * 2 };
    make_texture(v);
    return v;
}

void viz_free(viz *v) {
    if (!v) return;
    free(v->px), free(v->prev), free(v->heat), free(v->tunnel);
    if (v->tex) SDL_DestroyTexture(v->tex);
    free(v);
}

void viz_step(viz *v, int dir) {
    v->fx = (v->fx + dir + FX_COUNT) % FX_COUNT;
    v->fx_since = v->t;
    v->name_until = v->t + 2.5;
    if (v->px) memset(v->px, 0, sizeof *v->px * (size_t)(v->w * v->h));
}

const char *viz_name(const viz *v) { return fx_names[v->fx]; }
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
