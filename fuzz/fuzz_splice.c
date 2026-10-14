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
 * Differential fuzz target for ass_track_splice_events.
 *
 * The input is a sequence of edit operations, one per input line. Each
 * operation splices event lines into a live track and is mirrored on a list
 * of script lines. After every operation the live track must equal a track
 * parsed with ass_read_memory from the edited script, field by field, and a
 * frame rendered from each must be identical; otherwise the target aborts.
 *
 * Operation line: <op> <position> <count> <content>
 *   op % 4: 0 insert, 1 remove, 2 replace, 3 render only (after a layout reset)
 *   content: the new lines, separated by 0x1F
 *
 * Fonts come from the system font provider; without any font nothing is
 * rendered and only the event fields are compared.
 *
 * Standalone build: fuzz_splice <file>... runs every file once.
 * libFuzzer build (-DASS_FUZZMODE=2): exports LLVMFuzzerTestOneInput.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ass.h"

#define FUZZMODE_STANDALONE 0
#define FUZZMODE_LIBFUZZER  2
#ifndef ASS_FUZZMODE
    #define ASS_FUZZMODE FUZZMODE_STANDALONE
#endif

#define MAX_OPS 64
#define MAX_LINES 256
#define MAX_LINE_LEN 1024

static const char header[] =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 320\n"
    "PlayResY: 180\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Sans,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n"
    "Style: Alt,Sans,16,&H0000FFFF,&H000000FF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,2,1,8,10,10,10,1\n"
    "Style: Default,Sans,24,&H00FF00FF,&H000000FF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,3,1,0,5,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

static ASS_Library *library;
static ASS_Renderer *renderer;

static void msg_callback(int level, const char *fmt, va_list va, void *data)
{
    (void) level;
    (void) fmt;
    (void) va;
    (void) data;
}

static void fail(const char *what, int op)
{
    fprintf(stderr, "fuzz_splice: %s after operation %d\n", what, op);
    abort();
}

static void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p)
        abort();
    return p;
}

typedef struct {
    char *text;
    int is_event;
} Line;

static ASS_Track *parse_lines(const Line *lines, int n)
{
    size_t len = sizeof(header) - 1;
    for (int i = 0; i < n; i++)
        len += strlen(lines[i].text) + 1;
    char *buf = xrealloc(NULL, len + 1), *p = buf;
    memcpy(p, header, sizeof(header) - 1);
    p += sizeof(header) - 1;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(lines[i].text);
        memcpy(p, lines[i].text, l);
        p += l;
        *p++ = '\n';
    }
    ASS_Track *track = ass_read_memory(library, buf, p - buf, NULL);
    free(buf);
    if (!track)
        abort();
    return track;
}

static int str_equal(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return !strcmp(a, b);
}

static int tracks_equal(const ASS_Track *a, const ASS_Track *b)
{
    if (a->n_events != b->n_events)
        return 0;
    for (int i = 0; i < a->n_events; i++) {
        const ASS_Event *x = a->events + i, *y = b->events + i;
        if (x->Start != y->Start || x->Duration != y->Duration ||
                x->ReadOrder != y->ReadOrder || x->Layer != y->Layer ||
                x->Style != y->Style || x->MarginL != y->MarginL ||
                x->MarginR != y->MarginR || x->MarginV != y->MarginV ||
                !str_equal(x->Name, y->Name) || !str_equal(x->Effect, y->Effect) ||
                !str_equal(x->Text, y->Text))
            return 0;
    }
    return 1;
}

static uint64_t hash_frame(const ASS_Image *img)
{
    uint64_t h = 1469598103934665603ULL;
#define MIX(v) (h = (h ^ (uint64_t) (v)) * 1099511628211ULL)
    for (; img; img = img->next) {
        MIX(img->type);
        MIX((uint32_t) img->dst_x);
        MIX((uint32_t) img->dst_y);
        MIX((uint32_t) img->w);
        MIX((uint32_t) img->h);
        MIX(img->color);
        for (int y = 0; y < img->h; y++)
            for (int x = 0; x < img->w; x++)
                MIX(img->bitmap[(ptrdiff_t) y * img->stride + x]);
    }
#undef MIX
    return h;
}

/*
 * Make content a line that the splice contract covers: no line breaks or NUL,
 * and nothing a full parse would treat as a section header or Format line.
 */
static char *sanitize_line(const unsigned char *src, size_t len)
{
    if (len > MAX_LINE_LEN)
        len = MAX_LINE_LEN;
    char *out = xrealloc(NULL, len + 16), *o = out;
    const char *p = (const char *) src, *end = p + len;
    for (; p < end; p++)
        if (*p != '\r' && *p != '\n' && *p != '\0')
            *o++ = *p;
    *o = '\0';

    const char *s = out;
    while (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf')
        s += 3;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '[' || !strncmp(s, "Format:", 7)) {
        size_t l = strlen(out);
        memmove(out + 9, out, l + 1);
        memcpy(out, "Comment: ", 9);
    }
    return out;
}

static int line_is_event(const char *text)
{
    Line line = { (char *) text, 0 };
    ASS_Track *track = parse_lines(&line, 1);
    int n = track->n_events;
    ass_free_track(track);
    return n;
}

static void run(const uint8_t *data, size_t size)
{
    Line *lines = NULL;
    int n_lines = 0;
    ASS_Track *live = parse_lines(NULL, 0);

    const uint8_t *p = data, *end = data + size;
    for (int op = 0; op < MAX_OPS && p < end; op++) {
        const uint8_t *eol = memchr(p, '\n', end - p);
        if (!eol)
            eol = end;
        size_t len = eol - p;
        const uint8_t *cur = p;
        p = eol < end ? eol + 1 : end;
        if (len < 3)
            continue;

        int kind = cur[0] % 4;
        int at = n_lines ? cur[1] % (n_lines + 1) : 0;
        int count = cur[2] % 4;
        cur += 3;
        len -= 3;

        int n_remove = 0;
        if (kind == 1 || kind == 2) {
            n_remove = count;
            if (n_remove > n_lines - at)
                n_remove = n_lines - at;
        }

        // the new lines
        Line added[8];
        int n_added = 0;
        if (kind == 0 || kind == 2) {
            const uint8_t *s = cur, *e = cur + len;
            while (n_added < 8 && n_lines - n_remove + n_added < MAX_LINES) {
                const uint8_t *sep = memchr(s, 0x1f, e - s);
                if (!sep)
                    sep = e;
                char *text = sanitize_line(s, sep - s);
                if (*text) {
                    added[n_added].text = text;
                    added[n_added].is_event = line_is_event(text);
                    n_added++;
                } else {
                    free(text);
                }
                if (sep == e)
                    break;
                s = sep + 1;
            }
        }

        if (kind != 3) {
            int first_eid = 0, n_remove_events = 0, n_added_events = 0;
            for (int i = 0; i < at; i++)
                first_eid += lines[i].is_event;
            for (int i = at; i < at + n_remove; i++)
                n_remove_events += lines[i].is_event;

            size_t blen = 0;
            for (int i = 0; i < n_added; i++)
                blen += strlen(added[i].text) + 2;
            char *block = xrealloc(NULL, blen + 1), *b = block;
            for (int i = 0; i < n_added; i++) {
                size_t l = strlen(added[i].text);
                memcpy(b, added[i].text, l);
                b += l;
                if (i & 1)
                    *b++ = '\r';
                *b++ = '\n';
                n_added_events += added[i].is_event;
            }

            int ret = ass_track_splice_events(live, first_eid, n_remove_events,
                                              block, b - block);
            free(block);
            if (ret != n_added_events)
                fail("wrong splice result", op);

            for (int i = at; i < at + n_remove; i++)
                free(lines[i].text);
            int new_n = n_lines - n_remove + n_added;
            if (new_n > n_lines)
                lines = xrealloc(lines, new_n * sizeof(*lines));
            if (n_lines - at - n_remove)
                memmove(lines + at + n_added, lines + at + n_remove,
                        (n_lines - at - n_remove) * sizeof(*lines));
            if (n_added)
                memcpy(lines + at, added, n_added * sizeof(*lines));
            n_lines = new_n;
        } else {
            ass_track_reset_layout_state(live);
        }

        ASS_Track *fresh = parse_lines(lines, n_lines);
        if (!tracks_equal(live, fresh))
            fail("track differs from a fresh parse", op);
        if (live->n_events) {
            // render at two event times; the second render has history
            for (int k = 0; k < 2; k++) {
                unsigned sel = len ? cur[k % len] : (unsigned) k;
                const ASS_Event *e = live->events + sel % live->n_events;
                long long t = e->Start + (e->Duration > 0 ? e->Duration / 2 : 0);
                uint64_t a = hash_frame(ass_render_frame(renderer, live, t, NULL));
                uint64_t b = hash_frame(ass_render_frame(renderer, fresh, t, NULL));
                if (a != b)
                    fail("frame differs from a fresh parse", op);
            }
        }
        ass_free_track(fresh);
        // the live track keeps the history of these renders; the next
        // comparison is after a splice or reset, which drops it
    }

    for (int i = 0; i < n_lines; i++)
        free(lines[i].text);
    free(lines);
    ass_free_track(live);
}

static int init(void)
{
    library = ass_library_init();
    if (!library)
        return 0;
    ass_set_message_cb(library, msg_callback, NULL);
    renderer = ass_renderer_init(library);
    if (!renderer)
        return 0;
    ass_set_frame_size(renderer, 320, 180);
    ass_set_storage_size(renderer, 320, 180);
    // text and drawings render only if some font is found
    ass_set_fonts(renderer, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
    return 1;
}

#if ASS_FUZZMODE == FUZZMODE_STANDALONE
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: %s <input file>...\n", argc ? argv[0] : "fuzz_splice");
        return 2;
    }
    if (!init()) {
        printf("library init failed!\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            printf("cannot open %s\n", argv[i]);
            return 1;
        }
        uint8_t *buf = NULL;
        size_t len = 0, cap = 0, n;
        do {
            if (cap - len < 4096) {
                cap = cap * 2 + 4096;
                buf = xrealloc(buf, cap);
            }
            n = fread(buf + len, 1, cap - len, f);
            len += n;
        } while (n);
        fclose(f);
        run(buf, len);
        free(buf);
    }
    ass_renderer_done(renderer);
    ass_library_done(library);
    return 0;
}
#elif ASS_FUZZMODE == FUZZMODE_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!library && !init())
        abort();
    if (size > 64 * 1024)
        return 0;
    run(data, size);
    return 0;
}
#else
    #error Unsupported fuzzing mode
#endif
