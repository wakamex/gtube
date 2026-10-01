#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gs_json.h"
#include "http.h"

#define ORIGIN "https://music.youtube.com"
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36"

// ---- SHA-1, for the signed-request header ----

typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t fill; } sha1;

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(sha1 *s, const uint8_t *b) {
    uint32_t w[80], a = s->h[0], bb = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (bb & c) | (~bb & d), k = 0x5A827999;
        else if (i < 40) f = bb ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60) f = (bb & c) | (bb & d) | (c & d), k = 0x8F1BBCDC;
        else f = bb ^ c ^ d, k = 0xCA62C1D6;
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(bb, 30), bb = a, a = t;
    }
    s->h[0] += a, s->h[1] += bb, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

static void sha1_hex(const char *text, char hex[41]) {
    sha1 s = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
    size_t n = strlen(text);
    for (size_t i = 0; i < n; i++) {
        s.buf[s.fill++] = (uint8_t)text[i];
        if (s.fill == 64) sha1_block(&s, s.buf), s.fill = 0;
    }
    uint64_t bits = (uint64_t)n * 8;
    s.buf[s.fill++] = 0x80;
    if (s.fill > 56) {
        while (s.fill < 64) s.buf[s.fill++] = 0;
        sha1_block(&s, s.buf), s.fill = 0;
    }
    while (s.fill < 56) s.buf[s.fill++] = 0;
    for (int i = 0; i < 8; i++) s.buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_block(&s, s.buf);
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", (s.h[i / 4] >> (24 - 8 * (i % 4))) & 0xFF);
}

// ---- Requests ----

// Appends `s` to a JSON string literal being built in out.
static void json_quote(char *out, size_t size, const char *s) {
    size_t n = strlen(out);
    if (n + 2 >= size) return;
    out[n++] = '"';
    for (; *s && n + 8 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') out[n++] = '\\', out[n++] = (char)c;
        else if (c < 0x20) n += (size_t)snprintf(out + n, size - n, "\\u%04x", c);
        else out[n++] = (char)c;
    }
    out[n++] = '"';
    out[n] = 0;
}

// POSTs {"context": ..., <fields>} to an API endpoint; returns the parsed response or NULL.
static gs_json *request(account *a, const char *endpoint, const char *fields, char *error, size_t esize) {
    char url[256], body[4096], version[32], jar[1200], auth[200] = "", sapisid[256];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    strftime(version, sizeof version, "1.%Y%m%d.01.00", utc);  // the page's own version string, as ytmusicapi sends
    snprintf(url, sizeof url, ORIGIN "/youtubei/v1/%s?prettyPrint=false", endpoint);
    snprintf(body, sizeof body, "{\"context\":{\"client\":{\"clientName\":\"WEB_REMIX\",\"clientVersion\":\"%s\",\"hl\":\"en\"}},%s}", version, fields);
    // Signed in: the cookies, and a hash of the SAPISID cookie that shows the page's origin.
    bool signed_in = account_cookie(a, "SAPISID", sapisid, sizeof sapisid) && account_jar_file(a, jar, sizeof jar);
    if (signed_in) {
        char text[400], hex[41];
        snprintf(text, sizeof text, "%lld %s " ORIGIN, (long long)now, sapisid);
        sha1_hex(text, hex);
        snprintf(auth, sizeof auth, "Authorization: SAPISIDHASH %lld_%s", (long long)now, hex);
    }
    // (signed out, the list ends where the authorization would be)
    const char *headers[] = { "Content-Type: application/json", "Origin: " ORIGIN, "X-Origin: " ORIGIN,
                              signed_in ? auth : NULL, "X-Goog-AuthUser: 0", NULL };
    http_request r = { .url = url, .agent = USER_AGENT, .headers = headers, .body = body, .cookies = signed_in ? jar : NULL, .compressed = true };
    char *out = NULL;
    size_t len = 0;
    int status = http_fetch(&r, &out, &len);
    if (signed_in) account_jar_done(jar);

    gs_json *doc = status == 200 ? gs_json_parse(out, len) : NULL;
    if (!doc) snprintf(error, esize, !status ? "could not reach YouTube Music" : status != 200 ? "YouTube Music answered HTTP %d" : "YouTube Music sent something unreadable", status);
    SDL_free(out);
    return doc;
}

// ---- Reading responses ----

static void runs_text(gs_jv runs, char *out, size_t size) {
    out[0] = 0;
    for (gs_jv r = gs_json_first(runs); r; r = gs_json_next(r)) SDL_strlcat(out, gs_json_str(gs_json_get(r, "text"), ""), size);
}

static double parse_duration(const char *s) {
    double t = 0;
    int parts = 0;
    for (const char *p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') continue;
        if (*p != ':') return 0;
        parts++;
    }
    if (!parts || !*s) return 0;
    for (const char *p = s; *p;) {
        t = t * 60 + atoi(p);
        p = strchr(p, ':');
        if (!p) break;
        p++;
    }
    return t;
}

// Splits "Daft Punk • Discovery • 5:02": the artist is the first part (after a "Song" label, in
// mixed results), and a trailing time is the duration.
static void byline(const char *text, char *artist, size_t size, double *duration) {
    char buf[512];
    SDL_strlcpy(buf, text, sizeof buf);
    const char *sep = " \xE2\x80\xA2 ";
    char *parts[8];
    int n = 0;
    for (char *p = buf; p && n < 8;) {
        parts[n++] = p;
        char *q = strstr(p, sep);
        if (q) *q = 0, q += strlen(sep);
        p = q;
    }
    int first = n > 1 && (!strcmp(parts[0], "Song") || !strcmp(parts[0], "Video") || !strcmp(parts[0], "Episode"));
    SDL_strlcpy(artist, n > first ? parts[first] : "", size);
    if (duration && n > 1 && parse_duration(parts[n - 1]) > 0) *duration = parse_duration(parts[n - 1]);
}

static item *add(page *pg, int *cap) {
    if (pg->n == *cap) pg->items = realloc(pg->items, sizeof *pg->items * (size_t)(*cap = *cap ? *cap * 2 : 64));
    item *it = &pg->items[pg->n++];
    memset(it, 0, sizeof *it);
    return it;
}

static const char *page_type(gs_jv endpoint) {
    return gs_json_str(gs_json_path(endpoint, "browseEndpoint.browseEndpointContextSupportedConfigs.browseEndpointContextMusicConfig.pageType"), "");
}

// A row in a search result, playlist or album.
static void list_item(page *pg, int *cap, gs_jv r) {
    char cols[4][512] = { "", "", "", "" };
    int ncols = 0;
    for (gs_jv c = gs_json_first(gs_json_get(r, "flexColumns")); c && ncols < 4; c = gs_json_next(c))
        runs_text(gs_json_path(c, "musicResponsiveListItemFlexColumnRenderer.text.runs"), cols[ncols++], sizeof cols[0]);
    const char *video = gs_json_str(gs_json_path(r, "playlistItemData.videoId"), NULL);
    if (!video) video = gs_json_str(gs_json_path(r, "overlay.musicItemThumbnailOverlayRenderer.content.musicPlayButtonRenderer.playNavigationEndpoint.watchEndpoint.videoId"), NULL);
    gs_jv nav = gs_json_get(r, "navigationEndpoint");
    const char *type = page_type(nav);
    if (video && !strstr(type, "ALBUM") && !strstr(type, "PLAYLIST") && !strstr(type, "ARTIST")) {
        item *it = add(pg, cap);
        it->kind = ITEM_SONG;
        SDL_strlcpy(it->id, video, sizeof it->id);
        SDL_strlcpy(it->title, cols[0], sizeof it->title);
        byline(cols[1], it->artist, sizeof it->artist, &it->duration);
        char fixed[64];
        runs_text(gs_json_path(r, "fixedColumns.0.musicResponsiveListItemFixedColumnRenderer.text.runs"), fixed, sizeof fixed);
        if (parse_duration(fixed) > 0) it->duration = parse_duration(fixed);
    } else if (strstr(type, "ALBUM") || strstr(type, "PLAYLIST")) {
        item *it = add(pg, cap);
        it->kind = strstr(type, "ALBUM") ? ITEM_ALBUM : ITEM_PLAYLIST;
        SDL_strlcpy(it->id, gs_json_str(gs_json_path(nav, "browseEndpoint.browseId"), ""), sizeof it->id);
        SDL_strlcpy(it->title, cols[0], sizeof it->title);
        SDL_strlcpy(it->artist, cols[1], sizeof it->artist);
    }
}

// A tile in the library's grid of playlists.
static void tile(page *pg, int *cap, gs_jv r) {
    gs_jv nav = gs_json_get(r, "navigationEndpoint");
    const char *type = page_type(nav), *id = gs_json_str(gs_json_path(nav, "browseEndpoint.browseId"), NULL);
    if (!id || (!strstr(type, "ALBUM") && !strstr(type, "PLAYLIST"))) return;  // such as "New playlist"
    item *it = add(pg, cap);
    it->kind = strstr(type, "ALBUM") ? ITEM_ALBUM : ITEM_PLAYLIST;
    SDL_strlcpy(it->id, id, sizeof it->id);
    runs_text(gs_json_path(r, "title.runs"), it->title, sizeof it->title);
    runs_text(gs_json_path(r, "subtitle.runs"), it->artist, sizeof it->artist);
}

// A track in a radio.
static void queue_item(page *pg, int *cap, gs_jv r) {
    const char *video = gs_json_str(gs_json_get(r, "videoId"), NULL);
    if (!video) return;
    item *it = add(pg, cap);
    it->kind = ITEM_SONG;
    SDL_strlcpy(it->id, video, sizeof it->id);
    runs_text(gs_json_path(r, "title.runs"), it->title, sizeof it->title);
    char by[512];
    runs_text(gs_json_path(r, "longBylineText.runs"), by, sizeof by);
    byline(by, it->artist, sizeof it->artist, NULL);
    char len[32];
    runs_text(gs_json_path(r, "lengthText.runs"), len, sizeof len);
    it->duration = parse_duration(len);
}

static void walk(page *pg, int *cap, gs_jv v, char *owner, size_t osize) {
    for (gs_jv c = gs_json_first(v); c; c = gs_json_next(c)) {
        const char *k = gs_json_key(c);
        if (k && !strcmp(k, "musicResponsiveListItemRenderer")) list_item(pg, cap, c);
        else if (k && !strcmp(k, "straplineTextOne")) runs_text(gs_json_get(c, "runs"), owner, osize);  // an album page's artist
        else if (k && !strcmp(k, "musicTwoRowItemRenderer")) tile(pg, cap, c);
        else if (k && !strcmp(k, "playlistPanelVideoRenderer")) queue_item(pg, cap, c);
        else if (k && !strcmp(k, "continuationItemRenderer"))  // more of the same list
            SDL_strlcpy(pg->more, gs_json_str(gs_json_path(c, "continuationEndpoint.continuationCommand.token"), ""), sizeof pg->more);
        else if (k && !strcmp(k, "nextRadioContinuationData"))
            SDL_strlcpy(pg->more, gs_json_str(gs_json_get(c, "continuation"), ""), sizeof pg->more), pg->radio = true;
        else if (k && (!strcmp(k, "musicCarouselShelfRenderer") || !strcmp(k, "nextContinuationData")))
            continue;  // related shelves and suggestions under a playlist
        else if (gs_json_kind(c) == GS_JSON_OBJECT || gs_json_kind(c) == GS_JSON_ARRAY) walk(pg, cap, c, owner, osize);
    }
}

static bool read_page(gs_json *doc, page *out) {
    int cap = 0;
    char owner[160] = "";
    walk(out, &cap, gs_json_root(doc), owner, sizeof owner);
    for (int i = 0; i < out->n; i++)  // album tracks name their artist once, in the page's header
        if (out->items[i].kind == ITEM_SONG && !out->items[i].artist[0]) SDL_strlcpy(out->items[i].artist, owner, sizeof out->items[i].artist);
    gs_json_free(doc);
    return true;
}

// ---- The calls ----

bool api_search(account *a, const char *query, search_kind kind, page *out) {
    // The filters the page's own search tabs use.
    static const char *const params[] = { "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D", "EgWKAQIYAWoKEAkQAxAEEAoQBQ%3D%3D", "EgeKAQQoAEABagoQAxAEEAkQChAF" };
    memset(out, 0, sizeof *out);
    char fields[1500] = "\"query\":";
    json_quote(fields, sizeof fields, query);
    SDL_strlcat(fields, ",\"params\":\"", sizeof fields);
    SDL_strlcat(fields, params[kind], sizeof fields);
    SDL_strlcat(fields, "\"", sizeof fields);
    gs_json *doc = request(a, "search", fields, out->error, sizeof out->error);
    return doc && read_page(doc, out);
}

bool api_browse(account *a, const char *browse_id, const char *more, page *out) {
    memset(out, 0, sizeof *out);
    char fields[2300] = "";
    SDL_strlcat(fields, more ? "\"continuation\":" : "\"browseId\":", sizeof fields);
    json_quote(fields, sizeof fields, more ? more : browse_id);
    gs_json *doc = request(a, "browse", fields, out->error, sizeof out->error);
    return doc && read_page(doc, out);
}

bool api_radio(account *a, const char *video_id, const char *more, page *out) {
    memset(out, 0, sizeof *out);
    char fields[2500] = "\"isAudioOnly\":true,\"playlistId\":", list[80];
    snprintf(list, sizeof list, "RDAMVM%s", video_id);
    json_quote(fields, sizeof fields, list);
    SDL_strlcat(fields, more ? ",\"continuation\":" : ",\"videoId\":", sizeof fields);
    json_quote(fields, sizeof fields, more ? more : video_id);
    gs_json *doc = request(a, "next", fields, out->error, sizeof out->error);
    if (!doc || !read_page(doc, out)) return false;
    if (!out->n && !out->error[0]) SDL_strlcpy(out->error, "no radio for that song", sizeof out->error);
    return out->n > 0;
}

bool api_like(account *a, const char *video_id, bool like, char *error, size_t size) {
    char fields[200] = "\"target\":{\"videoId\":";
    json_quote(fields, sizeof fields, video_id);
    SDL_strlcat(fields, "}", sizeof fields);
    gs_json *doc = request(a, like ? "like/like" : "like/removelike", fields, error, size);
    gs_json_free(doc);
    return doc != NULL;
}

void page_free(page *p) {
    free(p->items);
    p->items = NULL, p->n = 0;
}
