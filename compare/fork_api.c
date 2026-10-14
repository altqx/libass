/*
 * Copyright (C) 2026 libass contributors
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Tests for the fork's editing and rendering API extensions.
 *
 * Every edit made through the API is checked against ass_read_memory of the
 * edited script: the event fields must be equal and every rendered frame must
 * be pixel-identical when both tracks are rendered at the same times with the
 * same renderer.
 *
 * usage: fork_api [source-dir]
 * The source directory defaults to $srcdir (set by make check) or ".".
 * It must contain compare/test and compare/fork-test. Exits with 77 (skip)
 * if the fixtures are missing, e.g. in a release tarball.
 */

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ass.h"

#define FRAME_W 640
#define FRAME_H 360

static int failures;
static int frames_compared, frames_with_images;
static const char *srcdir = ".";

#define CHECK(cond, ...)                                                \
    do {                                                                \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                               \
            fputc('\n', stderr);                                        \
        }                                                               \
    } while (0)

static void msg_callback(int level, const char *fmt, va_list va, void *data)
{
    (void) data;
    if (level > 1)
        return;
    fprintf(stderr, "libass: ");
    vfprintf(stderr, fmt, va);
    fputc('\n', stderr);
}

static void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static char *read_file(const char *dir, const char *name, size_t *size)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    while (1) {
        if (cap - len < 65536) {
            cap = cap * 2 + 65536;
            buf = xrealloc(buf, cap + 1);
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (!n)
            break;
    }
    fclose(f);
    buf[len] = '\0';
    *size = len;
    return buf;
}

// xorshift64*, so that every platform runs the same edit sequences
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint32_t rnd(uint32_t n)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return n ? (uint32_t) ((rng_state * 0x2545F4914F6CDD1DULL) >> 33) % n : 0;
}

/* Frame hash: the fields and pixels that harness/dumpframes.c hashes */
#define K1 0x9E3779B97F4A7C15ULL
#define K2 0xC2B2AE3D27D4EB4FULL

static uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v * K2;
    h = (h << 31) | (h >> 33);
    return h * K1;
}

static uint64_t hash_frame(const ASS_Image *img)
{
    uint64_t h = 0x243F6A8885A308D3ULL;
    for (; img; img = img->next) {
        h = mix(h, (uint32_t) img->type);
        h = mix(h, ((uint64_t) (uint32_t) img->dst_x << 32) | (uint32_t) img->dst_y);
        h = mix(h, ((uint64_t) (uint32_t) img->w << 32) | (uint32_t) img->h);
        h = mix(h, img->color);
        for (int y = 0; y < img->h; y++) {
            const unsigned char *row = img->bitmap + (ptrdiff_t) y * img->stride;
            for (int x = 0; x < img->w; x++)
                h = mix(h, row[x]);
        }
    }
    return h;
}

static uint64_t render_hash(ASS_Renderer *renderer, ASS_Track *track, long long t)
{
    return hash_frame(ass_render_frame(renderer, track, t, NULL));
}

static ASS_Library *library;
static ASS_Renderer *renderer;

static const char *const font_files[] = {
    "compare/test/font1.ttf",
    "compare/test/font2.otf",
    "compare/fork-test/noto-sans-subset.ttf",
    "compare/fork-test/noto-sans-devanagari-subset.ttf",
    "compare/fork-test/noto-sans-thai-subset.ttf",
    "compare/fork-test/noto-sans-cjk-kr-subset.otf",
    "compare/fork-test/forktest-emoji.ttf",
    "compare/fork-test/forktest-bits.ttf",
    "compare/fork-test/forktest-arabic.ttf",
    "compare/fork-test/forktest-sym0020.ttf",
    "compare/fork-test/forktest-symf020.ttf",
    "compare/fork-test/forktest-symf120.ttf",
};

static int load_fonts(ASS_Library *lib)
{
    for (size_t i = 0; i < sizeof(font_files) / sizeof(*font_files); i++) {
        size_t size;
        char *data = read_file(srcdir, font_files[i], &size);
        if (!data)
            return 0;
        ass_add_font(lib, font_files[i], data, (int) size);
        free(data);
    }
    return 1;
}

static ASS_Renderer *new_renderer(ASS_Library *lib)
{
    ASS_Renderer *r = ass_renderer_init(lib);
    if (!r) {
        fprintf(stderr, "ass_renderer_init failed\n");
        exit(1);
    }
    ass_set_frame_size(r, FRAME_W, FRAME_H);
    ass_set_storage_size(r, FRAME_W, FRAME_H);
    ass_set_fonts(r, NULL, NULL, ASS_FONTPROVIDER_NONE, NULL, 0);
    return r;
}

static ASS_Track *parse(const char *text, size_t len)
{
    ASS_Track *track = ass_read_memory(library, (char *) text, len, NULL);
    if (!track) {
        fprintf(stderr, "ass_read_memory failed\n");
        exit(1);
    }
    return track;
}

static int str_equal(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return !strcmp(a, b);
}

/* compare everything that parsing sets, except render_priv */
static int events_equal(const ASS_Track *a, const ASS_Track *b, const char *what)
{
    if (a->n_events != b->n_events) {
        CHECK(0, "%s: n_events %d != %d", what, a->n_events, b->n_events);
        return 0;
    }
    for (int i = 0; i < a->n_events; i++) {
        const ASS_Event *x = a->events + i, *y = b->events + i;
        if (x->Start != y->Start || x->Duration != y->Duration ||
                x->ReadOrder != y->ReadOrder || x->Layer != y->Layer ||
                x->Style != y->Style || x->MarginL != y->MarginL ||
                x->MarginR != y->MarginR || x->MarginV != y->MarginV ||
                !str_equal(x->Name, y->Name) || !str_equal(x->Effect, y->Effect) ||
                !str_equal(x->Text, y->Text)) {
            CHECK(0, "%s: event %d differs", what, i);
            return 0;
        }
    }
    return 1;
}

static int styles_equal(const ASS_Track *a, const ASS_Track *b, const char *what)
{
    if (a->n_styles != b->n_styles || a->default_style != b->default_style) {
        CHECK(0, "%s: n_styles %d/%d default_style %d/%d", what,
              a->n_styles, b->n_styles, a->default_style, b->default_style);
        return 0;
    }
    for (int i = 0; i < a->n_styles; i++) {
        const ASS_Style *x = a->styles + i, *y = b->styles + i;
        if (!str_equal(x->Name, y->Name) || !str_equal(x->FontName, y->FontName) ||
                x->FontSize != y->FontSize || x->PrimaryColour != y->PrimaryColour ||
                x->SecondaryColour != y->SecondaryColour ||
                x->OutlineColour != y->OutlineColour || x->BackColour != y->BackColour ||
                x->Bold != y->Bold || x->Italic != y->Italic ||
                x->Underline != y->Underline || x->StrikeOut != y->StrikeOut ||
                x->ScaleX != y->ScaleX || x->ScaleY != y->ScaleY ||
                x->Spacing != y->Spacing || x->Angle != y->Angle ||
                x->BorderStyle != y->BorderStyle || x->Outline != y->Outline ||
                x->Shadow != y->Shadow || x->Alignment != y->Alignment ||
                x->MarginL != y->MarginL || x->MarginR != y->MarginR ||
                x->MarginV != y->MarginV || x->Encoding != y->Encoding ||
                x->treat_fontname_as_pattern != y->treat_fontname_as_pattern ||
                x->Blur != y->Blur || x->Justify != y->Justify) {
            CHECK(0, "%s: style %d differs", what, i);
            return 0;
        }
    }
    return 1;
}

/*
 * A script split into the text before the first Dialogue/Comment line of
 * [Events] (header) and the lines after it. Each line remembers whether a
 * fresh parse turns it into an event.
 */
typedef struct {
    char *text;
    int is_event;
} Line;

typedef struct {
    char *header;
    Line *lines;
    int n_lines, max_lines;
    char **style_names;
    int n_style_names;
    long long max_end;
} Script;

static int line_is_event(const char *header, const char *line)
{
    size_t hl = strlen(header), ll = strlen(line);
    char *buf = xmalloc(hl + ll + 2);
    memcpy(buf, header, hl);
    memcpy(buf + hl, line, ll);
    buf[hl + ll] = '\n';
    ASS_Track *t = parse(buf, hl + ll + 1);
    int n = t->n_events;
    ass_free_track(t);
    free(buf);
    return n;
}

static void script_insert(Script *s, int at, const char *text)
{
    if (s->n_lines == s->max_lines) {
        s->max_lines = s->max_lines * 2 + 16;
        s->lines = xrealloc(s->lines, s->max_lines * sizeof(*s->lines));
    }
    memmove(s->lines + at + 1, s->lines + at, (s->n_lines - at) * sizeof(*s->lines));
    s->lines[at].text = xstrdup(text);
    s->lines[at].is_event = line_is_event(s->header, text);
    s->n_lines++;
}

static void script_remove(Script *s, int at, int n)
{
    for (int i = at; i < at + n; i++)
        free(s->lines[i].text);
    if (s->n_lines - at - n)
        memmove(s->lines + at, s->lines + at + n, (s->n_lines - at - n) * sizeof(*s->lines));
    s->n_lines -= n;
}

static int script_eid(const Script *s, int line)
{
    int eid = 0;
    for (int i = 0; i < line; i++)
        eid += s->lines[i].is_event;
    return eid;
}

static int script_count(const Script *s, int line, int n)
{
    int count = 0;
    for (int i = line; i < line + n; i++)
        count += s->lines[i].is_event;
    return count;
}

static char *script_text(const Script *s, size_t *len)
{
    size_t total = strlen(s->header);
    for (int i = 0; i < s->n_lines; i++)
        total += strlen(s->lines[i].text) + 1;
    char *buf = xmalloc(total + 1), *p = buf;
    size_t hl = strlen(s->header);
    memcpy(p, s->header, hl);
    p += hl;
    for (int i = 0; i < s->n_lines; i++) {
        size_t n = strlen(s->lines[i].text);
        memcpy(p, s->lines[i].text, n);
        p += n;
        *p++ = '\n';
    }
    *p = '\0';
    *len = p - buf;
    return buf;
}

static void script_free(Script *s)
{
    free(s->header);
    script_remove(s, 0, s->n_lines);
    free(s->lines);
    for (int i = 0; i < s->n_style_names; i++)
        free(s->style_names[i]);
    free(s->style_names);
}

static void script_init(Script *s, const char *text)
{
    memset(s, 0, sizeof(*s));
    const char *p = text, *events = NULL;
    while (*p) {
        const char *q = p;
        while (*q && *q != '\n' && *q != '\r')
            q++;
        if (!strncmp(p, "Dialogue:", 9) || !strncmp(p, "Comment:", 8)) {
            events = p;
            break;
        }
        if (!strncmp(p, "Style:", 6)) {
            const char *n = p + 6, *e;
            while (*n == ' ')
                n++;
            for (e = n; *e != ',' && e < q; e++)
                ;
            s->style_names = xrealloc(s->style_names,
                                      (s->n_style_names + 1) * sizeof(char *));
            char *name = xmalloc(e - n + 1);
            memcpy(name, n, e - n);
            name[e - n] = '\0';
            s->style_names[s->n_style_names++] = name;
        }
        p = q;
        while (*p == '\n' || *p == '\r')
            p++;
    }
    if (!events)
        events = p;
    s->header = xmalloc(events - text + 1);
    memcpy(s->header, text, events - text);
    s->header[events - text] = '\0';

    p = events;
    while (*p) {
        const char *q = p;
        while (*q && *q != '\n' && *q != '\r')
            q++;
        if (q > p) {
            char *line = xmalloc(q - p + 1);
            memcpy(line, p, q - p);
            line[q - p] = '\0';
            script_insert(s, s->n_lines, line);
            free(line);
        }
        p = q;
        while (*p == '\n' || *p == '\r')
            p++;
    }

    size_t len;
    char *full = script_text(s, &len);
    ASS_Track *t = parse(full, len);
    for (int i = 0; i < t->n_events; i++) {
        long long end = t->events[i].Start + t->events[i].Duration;
        if (end > s->max_end)
            s->max_end = end;
    }
    ass_free_track(t);
    free(full);
}

static void format_time(char *buf, size_t size, long long ms)
{
    if (ms < 0)
        ms = 0;
    snprintf(buf, size, "%lld:%02lld:%02lld.%02lld", ms / 3600000,
             ms / 60000 % 60, ms / 1000 % 60, ms / 10 % 100);
}

static void prepend(char *text, size_t size, const char *prefix)
{
    size_t pl = strlen(prefix), tl = strlen(text);
    if (pl >= size)
        return;
    if (pl + tl >= size)
        tl = size - pl - 1;
    memmove(text + pl, text, tl);
    text[pl + tl] = '\0';
    memcpy(text, prefix, pl);
}

/*
 * Return a changed copy of a Dialogue/Comment line (or a new line when src is
 * NULL), in one of the ways an editor or a broken file changes lines.
 */
static char *mutate_line(const Script *s, const char *src)
{
    char fields[9][256];
    char text[2048];
    const char *body = NULL;
    int n = 0;
    if (src) {
        body = strchr(src, ':');
        body = body ? body + 1 : src;
        while (*body == ' ')
            body++;
        for (; n < 9; n++) {
            const char *c = strchr(body, ',');
            if (!c)
                break;
            size_t l = c - body;
            if (l > 255)
                l = 255;
            memcpy(fields[n], body, l);
            fields[n][l] = '\0';
            body = c + 1;
        }
    }
    if (n < 9) {
        long long start = rnd((uint32_t) (s->max_end + 1));
        char ts[32], te[32];
        format_time(ts, sizeof(ts), start);
        format_time(te, sizeof(te), start + 300 + rnd(3000));
        snprintf(fields[0], 256, "%u", rnd(2));
        snprintf(fields[1], 256, "%s", ts);
        snprintf(fields[2], 256, "%s", te);
        snprintf(fields[3], 256, "%s", s->n_style_names ?
                 s->style_names[rnd(s->n_style_names)] : "Default");
        snprintf(fields[4], 256, "%s", "");
        for (int i = 5; i < 8; i++)
            snprintf(fields[i], 256, "0");
        fields[8][0] = '\0';
        body = "New line";
    }
    snprintf(text, sizeof(text), "%s", body);

    const char *kind = "Dialogue";
    char *out = xmalloc(8192);
    switch (rnd(12)) {
    case 0:
        strncat(text, "a", sizeof(text) - strlen(text) - 1);
        break;
    case 1:
        strncat(text, "\\Nsecond row", sizeof(text) - strlen(text) - 1);
        break;
    case 2: {
        long long start = rnd((uint32_t) (s->max_end + 1));
        format_time(fields[1], 256, start);
        format_time(fields[2], 256, start + 200 + rnd(4000));
        break;
    }
    case 3:
        snprintf(fields[0], 256, "%u", rnd(3));
        break;
    case 4:
        snprintf(fields[3], 256, "%s", s->n_style_names && rnd(4) ?
                 s->style_names[rnd(s->n_style_names)] : "NoSuchStyle");
        break;
    case 5:
        kind = "Comment";
        break;
    case 6:
        // too few fields: dropped by the parser
        snprintf(out, 8192, "Dialogue: %s,%s", fields[0], fields[1]);
        return out;
    case 7: {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "{\\an%u}", 1 + rnd(9));
        prepend(text, sizeof(text), tmp);
        break;
    }
    case 8: {
        char tmp[96];
        unsigned w = 20 + rnd(200), h = 10 + rnd(100);
        snprintf(tmp, sizeof(tmp), "{\\p1}m 0 0 l %u 0 %u %u 0 %u{\\p0}", w, w, h, h);
        prepend(text, sizeof(text), tmp);
        break;
    }
    case 9:
        // trailing white space is trimmed by the parser
        strncat(text, " \t ", sizeof(text) - strlen(text) - 1);
        break;
    case 10: {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "{\\pos(%u,%u)}", rnd(FRAME_W), rnd(FRAME_H));
        prepend(text, sizeof(text), tmp);
        break;
    }
    default:
        snprintf(fields[7], 256, "%u", rnd(80));
        break;
    }
    snprintf(out, 8192, "%s%s: %s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
             rnd(8) ? "" : "  ", kind, fields[0], fields[1], fields[2], fields[3],
             fields[4], fields[5], fields[6], fields[7], fields[8], text);
    return out;
}

/*
 * Render live and fresh at the same times, in the same order, and require
 * identical frames. Times include the midpoints of the events in [eid, eid+n).
 */
static void compare_frames(ASS_Track *live, ASS_Track *fresh, const Script *s,
                           int eid, int n, const char *what)
{
    long long times[16];
    int nt = 0;
    for (int i = eid; i < eid + n && i < live->n_events && nt < 8; i++)
        times[nt++] = live->events[i].Start + live->events[i].Duration / 2;
    while (nt < 12)
        times[nt++] = rnd((uint32_t) (s->max_end + 1));
    for (int i = 0; i < nt; i++) {
        uint64_t a = render_hash(renderer, live, times[i]);
        uint64_t b = render_hash(renderer, fresh, times[i]);
        CHECK(a == b, "%s: frame at %lld ms differs from a fresh parse", what, times[i]);
        frames_compared++;
        frames_with_images += a != hash_frame(NULL);
    }
}

/* make the generated collision script: overlapping, staggered events */
static char *collide_script(void)
{
    size_t cap = 1 << 16, len = 0;
    char *buf = xmalloc(cap);
    len += snprintf(buf + len, cap - len,
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n"
        "WrapStyle: 0\nScaledBorderAndShadow: yes\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
        "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Default,Aileron,24,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
        "0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n"
        "Style: Top,Aileron,20,&H0000FFFF,&H000000FF,&H00000000,&H00000000,"
        "0,0,0,0,100,100,0,0,1,1,0,8,10,10,10,1\n"
        "Style: Default,Aileron,26,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
        "0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
        "MarginV, Effect, Text\n");
    for (int g = 0; g < 40; g++) {
        for (int k = 0; k < 5; k++) {
            char ts[32], te[32];
            long long start = g * 1500LL + k * 250;
            format_time(ts, sizeof(ts), start);
            format_time(te, sizeof(te), start + 1200 + (k % 3) * 700);
            if (cap - len < 512) {
                cap *= 2;
                buf = xrealloc(buf, cap);
            }
            len += snprintf(buf + len, cap - len,
                            "Dialogue: %d,%s,%s,%s,,0,0,0,,Group %d line %d%s\n",
                            k == 4, ts, te, k % 2 ? "Top" : "Default", g, k,
                            k == 2 ? "\\Nwith a second row" : "");
        }
    }
    return buf;
}

static char *load_script(const char *name, size_t *len)
{
    if (!strcmp(name, "collide")) {
        char *s = collide_script();
        *len = strlen(s);
        return s;
    }
    return read_file(srcdir, name, len);
}

static const char *const scripts[] = {
    "collide",
    "compare/test/sub1.ass",
    "compare/test/sub2.ass",
    "compare/fork-test/grapheme.ass",
    "compare/fork-test/cmap.ass",
};
#define N_SCRIPTS (sizeof(scripts) / sizeof(*scripts))

static void test_splice_arguments(void)
{
    size_t len;
    char *text = load_script("collide", &len);
    ASS_Track *track = parse(text, len);
    ASS_Track *ref = parse(text, len);
    int n = track->n_events;
    const char *line = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x";

    CHECK(ass_track_splice_events(track, -1, 0, line, strlen(line)) == -1, "negative first_eid");
    CHECK(ass_track_splice_events(track, n + 1, 0, line, strlen(line)) == -1, "first_eid > n_events");
    CHECK(ass_track_splice_events(track, 0, -1, line, strlen(line)) == -1, "negative n_remove");
    CHECK(ass_track_splice_events(track, n - 1, 2, line, strlen(line)) == -1, "range past the end");
    CHECK(ass_track_splice_events(track, 0, 0, NULL, 5) == -1, "NULL lines with a length");
    CHECK(ass_track_splice_events(NULL, 0, 0, line, strlen(line)) == -1, "NULL track");
    events_equal(track, ref, "after rejected splices");

    // no-op splices
    CHECK(ass_track_splice_events(track, 0, 0, NULL, 0) == 0, "empty splice");
    CHECK(ass_track_splice_events(track, n, 0, "Comment: x\r\n\r\n", 14) == 0, "comment only");
    events_equal(track, ref, "after empty splices");

    // a track without an event format cannot be spliced
    const char *header_only = "[Script Info]\nScriptType: v4.00+\n";
    ASS_Track *bare = parse(header_only, strlen(header_only));
    CHECK(ass_track_splice_events(bare, 0, 0, line, strlen(line)) == -1, "no event format");
    CHECK(bare->n_events == 0, "no event format: track changed");
    ass_free_track(bare);

    // parsing stops at NUL, like ass_read_memory
    const char nul_lines[] = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,b\0"
                             "\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,c";
    CHECK(ass_track_splice_events(track, 0, 0, nul_lines, sizeof(nul_lines) - 1) == 2,
          "NUL ends the input");

    // remove everything, then add back
    CHECK(ass_track_splice_events(track, 0, track->n_events, NULL, 0) == 0, "remove all");
    CHECK(track->n_events == 0, "remove all: %d left", track->n_events);
    CHECK(ass_track_splice_events(track, 0, 0, line, strlen(line)) == 1, "add to empty");
    CHECK(track->n_events == 1 && track->events[0].ReadOrder == 0, "add to empty: state");

    ass_free_track(ref);
    ass_free_track(track);
    free(text);
}

static void test_splice_script(const char *name, int n_ops)
{
    size_t len;
    char *text = load_script(name, &len);
    if (!text) {
        CHECK(0, "cannot read %s", name);
        return;
    }
    Script s;
    script_init(&s, text);
    free(text);

    char *full = script_text(&s, &len);
    ASS_Track *live = parse(full, len);
    free(full);

    for (int op = 0; op < n_ops; op++) {
        char what[256];
        snprintf(what, sizeof(what), "%s op %d", name, op);

        // render the live track first so that it has placement history
        for (int i = 0; i < 3; i++)
            render_hash(renderer, live, rnd((uint32_t) (s.max_end + 1)));

        int at = s.n_lines ? rnd(s.n_lines + 1) : 0;
        int n_remove = 0, n_insert = 0;
        switch (rnd(4)) {
        case 0: // replace one line
            if (at == s.n_lines)
                at = s.n_lines ? s.n_lines - 1 : 0;
            n_remove = s.n_lines ? 1 : 0;
            n_insert = 1;
            break;
        case 1: // insert lines
            n_insert = 1 + rnd(3);
            break;
        case 2: // delete lines
            n_remove = rnd(s.n_lines - at + 1);
            if (n_remove > 4)
                n_remove = 4;
            break;
        default: // replace a range with a different number of lines
            n_remove = rnd(s.n_lines - at + 1);
            if (n_remove > 3)
                n_remove = 3;
            n_insert = rnd(4);
            break;
        }

        char **inserted = xmalloc((n_insert + 1) * sizeof(char *));
        for (int i = 0; i < n_insert; i++) {
            const char *src = NULL;
            if (n_remove && i < n_remove)
                src = s.lines[at + i].text;
            else if (s.n_lines && rnd(2))
                src = s.lines[rnd(s.n_lines)].text;
            inserted[i] = mutate_line(&s, src);
        }

        int first_eid = script_eid(&s, at);
        int remove_events = script_count(&s, at, n_remove);
        script_remove(&s, at, n_remove);
        size_t blen = 0;
        for (int i = 0; i < n_insert; i++)
            blen += strlen(inserted[i]) + 2;
        char *block = xmalloc(blen + 1), *p = block;
        const char *sep = rnd(2) ? "\n" : "\r\n";
        for (int i = 0; i < n_insert; i++) {
            size_t l = strlen(inserted[i]);
            memcpy(p, inserted[i], l);
            p += l;
            if (i + 1 < n_insert || rnd(2)) {
                memcpy(p, sep, strlen(sep));
                p += strlen(sep);
            }
            script_insert(&s, at + i, inserted[i]);
            free(inserted[i]);
        }
        free(inserted);
        int expected = script_count(&s, at, n_insert);

        int ret = ass_track_splice_events(live, first_eid, remove_events,
                                          block, p - block);
        free(block);
        CHECK(ret == expected, "%s: splice returned %d, expected %d", what, ret, expected);

        full = script_text(&s, &len);
        ASS_Track *fresh = parse(full, len);
        free(full);
        if (events_equal(live, fresh, what))
            compare_frames(live, fresh, &s, first_eid, ret > 0 ? ret : 1, what);
        ass_free_track(fresh);
    }

    // one last full pass over the whole script, without the placement
    // history of the renders above
    ass_track_reset_layout_state(live);
    full = script_text(&s, &len);
    ASS_Track *fresh = parse(full, len);
    free(full);
    for (long long t = 0; t <= s.max_end; t += 250) {
        uint64_t a = render_hash(renderer, live, t);
        uint64_t b = render_hash(renderer, fresh, t);
        CHECK(a == b, "%s: final pass: frame at %lld ms differs", name, t);
    }
    ass_free_track(fresh);
    ass_free_track(live);
    script_free(&s);
}

static void test_splice(void)
{
    test_splice_arguments();
    for (size_t i = 0; i < N_SCRIPTS; i++)
        test_splice_script(scripts[i], i ? 60 : 300);
}

/* find the n-th Style line in the header: returns its start, *len its length */
static char *find_style_line(const Script *s, int n, size_t *len)
{
    char *p = s->header;
    while ((p = strstr(p, "Style:"))) {
        if (p == s->header || p[-1] == '\n' || p[-1] == '\r' ||
                (p - s->header == 3 && !memcmp(s->header, "\xef\xbb\xbf", 3))) {
            if (!n--) {
                *len = strcspn(p, "\r\n");
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static void replace_style_line(Script *s, int n, const char *line)
{
    size_t old_len = 0;
    char *at = find_style_line(s, n, &old_len);
    if (!at)
        return;
    size_t off = at - s->header, hl = strlen(s->header), nl = strlen(line);
    char *h = xmalloc(hl - old_len + nl + 1);
    memcpy(h, s->header, off);
    memcpy(h + off, line, nl);
    memcpy(h + off + nl, s->header + off + old_len, hl - off - old_len + 1);
    free(s->header);
    s->header = h;
}

/* Style line with one field (not the name) changed */
static char *mutate_style(const char *src, size_t len)
{
    static const char *const fonts[] = {
        "Aileron", "Noto Sans", "Pixel Operator Mono", "ForkTest Bits", "NoSuchFont"
    };
    char fields[23][256];
    int n = 0;
    const char *p = src + 6;
    const char *end = src + len;
    while (*p == ' ')
        p++;
    while (n < 23) {
        const char *c = memchr(p, ',', end - p);
        size_t l = (c ? c : end) - p;
        if (l > 255)
            l = 255;
        memcpy(fields[n], p, l);
        fields[n][l] = '\0';
        n++;
        if (!c)
            break;
        p = c + 1;
    }
    int f = 1 + rnd(n > 1 ? n - 1 : 1);
    if (f < n) {
        switch (f) {
        case 1:  snprintf(fields[f], 256, "%s", fonts[rnd(5)]); break;
        case 2:  snprintf(fields[f], 256, "%u", 8 + rnd(60)); break;
        case 3: case 4: case 5: case 6:
            snprintf(fields[f], 256, "&H%08X", (unsigned) rnd(0x7FFFFFFF) * 2u + rnd(2));
            break;
        case 7: case 8: case 9: case 10:
            snprintf(fields[f], 256, "%d", rnd(3) ? (int) rnd(2) : -1);
            break;
        case 11: case 12: snprintf(fields[f], 256, "%u", 50 + rnd(150)); break;
        case 13: snprintf(fields[f], 256, "%u", rnd(6)); break;
        case 14: snprintf(fields[f], 256, "%d", (int) rnd(90) - 45); break;
        case 15: snprintf(fields[f], 256, "%u", rnd(2) ? 1 : 3); break;
        case 16: case 17: snprintf(fields[f], 256, "%u.%u", rnd(6), rnd(10)); break;
        case 18: snprintf(fields[f], 256, "%u", 1 + rnd(9)); break;
        case 19: case 20: case 21: snprintf(fields[f], 256, "%u", rnd(120)); break;
        default: snprintf(fields[f], 256, "%u", rnd(3)); break;
        }
    }
    char *out = xmalloc(23 * 257 + 16);
    char *o = out;
    o += sprintf(o, "Style: %s%s", rnd(5) ? "" : "*", fields[0]);
    for (int i = 1; i < n; i++)
        o += sprintf(o, ",%s", fields[i]);
    return out;
}

static void test_set_style_script(const char *name, int n_ops)
{
    size_t len;
    char *text = load_script(name, &len);
    if (!text) {
        CHECK(0, "cannot read %s", name);
        return;
    }
    Script s;
    script_init(&s, text);
    free(text);

    char *full = script_text(&s, &len);
    ASS_Track *live = parse(full, len);
    free(full);
    int n_style_lines = live->n_styles - 1;

    for (int op = 0; op < n_ops && n_style_lines > 0; op++) {
        char what[256];
        snprintf(what, sizeof(what), "%s style op %d", name, op);
        for (int i = 0; i < 3; i++)
            render_hash(renderer, live, rnd((uint32_t) (s.max_end + 1)));

        int n = rnd(n_style_lines);
        size_t old_len = 0;
        const char *old = find_style_line(&s, n, &old_len);
        if (!old) {
            CHECK(0, "%s: no Style line %d in the header", what, n);
            break;
        }
        char *line = mutate_style(old, old_len);
        replace_style_line(&s, n, line);
        // a trailing line break and following text are ignored
        size_t ll = strlen(line);
        if (rnd(2)) {
            line = xrealloc(line, ll + 32);
            strcpy(line + ll, "\r\nStyle: Other,x");
        }
        int ret = ass_track_set_style(live, n + 1, line, strlen(line));
        free(line);
        CHECK(ret == 0, "%s: ass_track_set_style returned %d", what, ret);

        full = script_text(&s, &len);
        ASS_Track *fresh = parse(full, len);
        free(full);
        if (styles_equal(live, fresh, what) && events_equal(live, fresh, what))
            compare_frames(live, fresh, &s, 0, live->n_events < 8 ? live->n_events : 8, what);
        ass_free_track(fresh);
    }
    ass_free_track(live);
    script_free(&s);
}

static void test_set_style(void)
{
    size_t len;
    char *text = load_script("collide", &len);
    ASS_Track *track = parse(text, len);
    ASS_Track *ref = parse(text, len);
    const char *good = "Style: Top,Aileron,30,&H0000FFFF,&H000000FF,&H00000000,"
                       "&H00000000,0,0,0,0,100,100,0,0,1,1,0,8,10,10,10,1";
    const char *renamed = "Style: Topp,Aileron,30,&H0000FFFF,&H000000FF,&H00000000,"
                          "&H00000000,0,0,0,0,100,100,0,0,1,1,0,8,10,10,10,1";
    CHECK(ass_track_set_style(track, 0, good, strlen(good)) == -1, "built-in style 0");
    CHECK(ass_track_set_style(track, track->n_styles, good, strlen(good)) == -1, "sid too large");
    CHECK(ass_track_set_style(track, 2, NULL, 4) == -1, "NULL line");
    CHECK(ass_track_set_style(track, 2, good + 7, strlen(good + 7)) == -1, "no Style: prefix");
    CHECK(ass_track_set_style(track, 2, renamed, strlen(renamed)) == -1, "renamed style");
    CHECK(ass_track_set_style(track, 1, good, strlen(good)) == -1, "other style's name");
    CHECK(ass_track_set_style(track, 2, "Style:", 6) == -1, "empty Style line");
    CHECK(ass_track_set_style(NULL, 2, good, strlen(good)) == -1, "NULL track");
    styles_equal(track, ref, "after rejected style edits");
    CHECK(ass_track_set_style(track, 2, good, strlen(good)) == 0, "valid style edit");
    CHECK(track->styles[2].FontSize == 30, "valid style edit: FontSize %g",
          track->styles[2].FontSize);
    CHECK(track->n_styles == ref->n_styles, "valid style edit: n_styles changed");
    ass_free_track(ref);
    ass_free_track(track);
    free(text);

    for (size_t i = 0; i < N_SCRIPTS; i++)
        test_set_style_script(scripts[i], 40);

    // style overrides are applied to the new style as a fresh parse applies them
    char *overrides[] = { "Default.Outline=4", "FontSize=18", "Top.PrimaryColour=&H00FF0000", NULL };
    ass_set_style_overrides(library, overrides);
    test_set_style_script("collide", 20);
    test_set_style_script("compare/fork-test/grapheme.ass", 20);
    ass_set_style_overrides(library, NULL);
}

static uint64_t render_events_hash(ASS_Track *track, const int *eids, int n,
                                   long long t, int flags)
{
    return hash_frame(ass_render_events(renderer, track, eids, n, t, flags, NULL));
}

/* parse the header plus the given event lines of a script */
static ASS_Track *parse_subset(const Script *s, const int *line_ids, int n)
{
    Script sub = *s;
    Line *lines = xmalloc((n + 1) * sizeof(*lines));
    for (int i = 0; i < n; i++)
        lines[i] = s->lines[line_ids[i]];
    sub.lines = lines;
    sub.n_lines = n;
    size_t len;
    char *text = script_text(&sub, &len);
    ASS_Track *track = parse(text, len);
    free(text);
    free(lines);
    return track;
}

static void test_render_events_script(const char *name)
{
    size_t len;
    char *text = load_script(name, &len);
    if (!text) {
        CHECK(0, "cannot read %s", name);
        return;
    }
    Script s;
    script_init(&s, text);
    ASS_Track *track = parse(text, len);
    ASS_Track *other = parse(text, len);
    free(text);

    int *line_of = xmalloc((track->n_events + 1) * sizeof(int));
    for (int i = 0, e = 0; i < s.n_lines; i++)
        if (s.lines[i].is_event)
            line_of[e++] = i;

    // a single event renders as ass_render_frame renders a track with only that event
    for (int e = 0; e < track->n_events; e++) {
        const ASS_Event *ev = track->events + e;
        long long t = ev->Start + ev->Duration / 2;
        ASS_Track *single = parse_subset(&s, line_of + e, 1);
        uint64_t ref = render_hash(renderer, single, t);
        ass_free_track(single);
        CHECK(render_events_hash(track, &e, 1, t, 0) == ref,
              "%s: event %d alone differs from a one-event track", name, e);
        CHECK(render_events_hash(track, &e, 1, t, ASS_RENDER_EVENTS_NO_COLLISIONS) == ref,
              "%s: event %d alone (no collisions) differs from a one-event track", name, e);
        frames_compared += 2;
        frames_with_images += 2 * (ref != hash_frame(NULL));
    }

    // a subset renders as ass_render_frame renders a fresh track with only those events,
    // and does not change what ass_render_frame produces on the full track
    int *eids = xmalloc((track->n_events + 1) * sizeof(int));
    int *lines = xmalloc((track->n_events + 1) * sizeof(int));
    for (int k = 0; k < 60; k++) {
        long long t = rnd((uint32_t) (s.max_end + 1));
        CHECK(render_hash(renderer, track, t) == render_hash(renderer, other, t),
              "%s: ass_render_events changed a later ass_render_frame at %lld ms", name, t);

        int n = 0;
        for (int e = 0; e < track->n_events; e++)
            if (rnd(3)) {
                eids[n] = e;
                lines[n] = line_of[e];
                n++;
            }
        long long t2 = rnd(4) ? t : (long long) rnd((uint32_t) (s.max_end + 1));
        ASS_Track *subset = parse_subset(&s, lines, n);
        uint64_t ref = render_hash(renderer, subset, t2);
        ass_free_track(subset);
        // shuffle: the order of eids does not matter
        for (int i = n - 1; i > 0; i--) {
            int j = rnd(i + 1), tmp = eids[i];
            eids[i] = eids[j];
            eids[j] = tmp;
        }
        CHECK(render_events_hash(track, eids, n, t2, 0) == ref,
              "%s: subset of %d events at %lld ms differs from a fresh subset track",
              name, n, t2);
        frames_compared++;
        frames_with_images += ref != hash_frame(NULL);
    }
    free(eids);
    free(lines);
    free(line_of);
    ass_free_track(other);
    ass_free_track(track);
    script_free(&s);
}

static void test_render_events(void)
{
    size_t len;
    char *text = load_script("collide", &len);
    ASS_Track *track = parse(text, len);
    free(text);
    int all[1024], n = track->n_events < 1024 ? track->n_events : 1024;
    for (int i = 0; i < n; i++)
        all[i] = i;
    int change = -1;

    CHECK(!ass_render_events(renderer, track, all, n, 1000, 2, &change) && change == 2,
          "unknown flags");
    change = -1;
    CHECK(!ass_render_events(renderer, track, all, -1, 1000, 0, &change) && change == 2,
          "negative n_eids");
    change = -1;
    CHECK(!ass_render_events(renderer, track, NULL, 3, 1000, 0, &change) && change == 2,
          "NULL eids");

    // detect_change compares with the previous call of either function
    ASS_Image *a = ass_render_events(renderer, track, all, n, 1000, 0, &change);
    CHECK(a, "all events at 1000 ms: no images");
    ass_render_events(renderer, track, all, n, 1000, 0, &change);
    CHECK(change == 0, "same events twice: detect_change %d", change);
    ass_render_frame(renderer, track, 1000, &change);
    CHECK(change == 0, "ass_render_frame after the same events: detect_change %d", change);
    int bad[] = { -1, n, 1 << 30 };
    CHECK(!ass_render_events(renderer, track, bad, 3, 1000, 0, &change) && change == 2,
          "only invalid ids: images or detect_change %d", change);
    CHECK(!ass_render_events(renderer, track, NULL, 0, 1000, 0, &change) && change == 0,
          "no events after an empty frame: detect_change %d", change);
    ass_free_track(track);

    for (size_t i = 0; i < N_SCRIPTS; i++)
        test_render_events_script(scripts[i]);
}

typedef struct {
    char *data;
    size_t size;
    int released;
} FontRef;

static void release_font(void *opaque)
{
    FontRef *ref = opaque;
    ref->released++;
    // freeing the bytes lets ASan catch any later read by libass
    free(ref->data);
    ref->data = NULL;
}

#define N_FONTS (sizeof(font_files) / sizeof(*font_files))

static void add_fonts_by_ref(ASS_Library *lib, FontRef *refs, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++) {
        refs[i].data = read_file(srcdir, font_files[i], &refs[i].size);
        refs[i].released = 0;
        ass_add_font_ref(lib, font_files[i], refs[i].data, refs[i].size,
                         release_font, refs + i);
    }
}

static int count_released(const FontRef *refs, int expected)
{
    int ok = 1;
    for (size_t i = 0; i < N_FONTS; i++)
        ok &= refs[i].released == expected;
    return ok;
}

/* render the font-sensitive scripts with renderer r and compare with expected[] */
static void render_with_library(ASS_Library *lib, ASS_Renderer *r,
                                const uint64_t *expected, uint64_t *out,
                                const char *what)
{
    static const char *const names[] = {
        "compare/test/sub1.ass", "compare/test/sub2.ass",
        "compare/fork-test/grapheme.ass", "compare/fork-test/cmap.ass",
    };
    int k = 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        size_t len;
        char *text = read_file(srcdir, names[i], &len);
        ASS_Track *track = ass_read_memory(lib, text, len, NULL);
        free(text);
        if (!track) {
            CHECK(0, "%s: cannot parse %s", what, names[i]);
            return;
        }
        for (int e = 0; e < track->n_events && k < 64; e++, k++) {
            const ASS_Event *ev = track->events + e;
            uint64_t h = render_hash(r, track, ev->Start + ev->Duration / 2);
            if (out)
                out[k] = h;
            if (expected)
                CHECK(h == expected[k], "%s: %s event %d differs from copied fonts",
                      what, names[i], e);
        }
        ass_free_track(track);
    }
}

static void test_add_font_ref(void)
{
    uint64_t expected[64];
    render_with_library(library, renderer, NULL, expected, "copied fonts");

    FontRef refs[N_FONTS], refs2[N_FONTS];
    ASS_Library *lib = ass_library_init();
    ass_set_message_cb(lib, msg_callback, NULL);
    add_fonts_by_ref(lib, refs, 0, N_FONTS);
    ASS_Renderer *r = new_renderer(lib);
    render_with_library(lib, r, expected, NULL, "fonts by reference");
    CHECK(count_released(refs, 0), "fonts released while in use");

    // ass_clear_fonts in the middle of a session, after the renderers that
    // used the fonts are gone; the last frame is still referenced by r
    ass_renderer_done(r);
    CHECK(count_released(refs, 0), "fonts released by ass_renderer_done");
    ass_clear_fonts(lib);
    CHECK(count_released(refs, 1), "ass_clear_fonts did not release every font exactly once");

    // a second set, half of it added after the renderer exists
    add_fonts_by_ref(lib, refs2, 0, N_FONTS / 2);
    r = new_renderer(lib);
    add_fonts_by_ref(lib, refs2, N_FONTS / 2, N_FONTS);
    render_with_library(lib, r, expected, NULL, "fonts by reference after ass_clear_fonts");

    // teardown with the last frame still referenced by the renderer
    ass_renderer_done(r);
    CHECK(count_released(refs2, 0), "fonts released before ass_library_done");
    ass_library_done(lib);
    CHECK(count_released(refs2, 1), "ass_library_done did not release every font exactly once");
    CHECK(count_released(refs, 1), "first font set released again");

    // fonts that cannot be added are released at once
    lib = ass_library_init();
    ass_set_message_cb(lib, msg_callback, NULL);
    FontRef bad[4];
    memset(bad, 0, sizeof(bad));
    for (int i = 0; i < 4; i++)
        bad[i].data = xmalloc(16);
    ass_add_font_ref(lib, NULL, bad[0].data, 16, release_font, bad + 0);
    ass_add_font_ref(lib, "x", NULL, 16, release_font, bad + 1);
    free(bad[1].data);
    bad[1].data = NULL;
    ass_add_font_ref(lib, "x", bad[2].data, 0, release_font, bad + 2);
    ass_add_font_ref(lib, "x", bad[3].data, (size_t) INT_MAX + 1, release_font, bad + 3);
    for (int i = 0; i < 4; i++)
        CHECK(bad[i].released == 1, "rejected font %d released %d times", i, bad[i].released);

    // without release, libass neither frees nor touches the caller's bytes
    static const char not_a_font[] = "not a font";
    ass_add_font_ref(lib, "static", not_a_font, sizeof(not_a_font), NULL, NULL);
    ass_add_font_ref(lib, NULL, not_a_font, sizeof(not_a_font), NULL, NULL);
    ass_library_done(lib);
}

int main(int argc, char *argv[])
{
    if (argc > 1)
        srcdir = argv[1];
    else if (getenv("srcdir"))
        srcdir = getenv("srcdir");

    library = ass_library_init();
    if (!library) {
        fprintf(stderr, "ass_library_init failed\n");
        return 1;
    }
    ass_set_message_cb(library, msg_callback, NULL);
    if (!load_fonts(library)) {
        fprintf(stderr, "fixtures not found under %s, skipping\n", srcdir);
        ass_library_done(library);
        return 77;
    }
    renderer = new_renderer(library);

    test_splice();
    test_set_style();
    test_render_events();
    test_add_font_ref();

    ass_renderer_done(renderer);
    ass_library_done(library);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("fork_api: all checks passed (%d of %d edit frames compared had images)\n",
           frames_with_images, frames_compared);
    return 0;
}
