/*
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
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

#include "config.h"
#include "ass_compat.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ass_library.h"
#include "ass_render.h"
#include "ass_parse.h"

#define MAX_VALID_NARGS 7
#define MAX_BE 127
#define NBSP 0xa0   // unicode non-breaking space character

struct arg {
    char *start, *end;
};

static inline int32_t argtoi32(struct arg arg)
{
    int32_t value;
    mystrtoi32(&arg.start, 10, &value);
    return value;
}

static inline double argtod(struct arg arg)
{
    double value;
    mystrtod(&arg.start, &value);
    return value;
}

static inline void push_arg(struct arg *args, int *nargs, char *start, char *end)
{
    if (*nargs <= MAX_VALID_NARGS) {
        rskip_spaces(&end, start);
        if (end > start) {
            args[*nargs] = (struct arg) {start, end};
            ++*nargs;
        }
    }
}

/**
 * \brief Check if starting part of (*p) matches sample.
 * If true, shift p to the first symbol after the matching part.
 */
static inline int mystrcmp(char **p, const char *sample)
{
    char *p2;
    for (p2 = *p; *sample != 0 && *p2 == *sample; p2++, sample++)
        ;
    if (*sample == 0) {
        *p = p2;
        return 1;
    }
    return 0;
}

typedef enum {
    GC_OTHER = 0,
    GC_CR,
    GC_LF,
    GC_CONTROL,
    GC_EXTEND,
    GC_ZWJ,
    GC_REGIONAL_INDICATOR,
    GC_PREPEND,
    GC_SPACINGMARK,
    GC_L,
    GC_V,
    GC_T,
    GC_LV,
    GC_LVT
} GraphemeCategory;

static GraphemeCategory get_grapheme_category(unsigned codepoint)
{
    if (codepoint == 0x000D) return GC_CR;
    if (codepoint == 0x000A) return GC_LF;

    if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0)) return GC_CONTROL;
    if (codepoint == 0x200D) return GC_ZWJ;
    if (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF) return GC_REGIONAL_INDICATOR;

    if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||
        (codepoint >= 0x0483 && codepoint <= 0x0489) ||
        (codepoint >= 0x0591 && codepoint <= 0x05BD) ||
        (codepoint >= 0x05BF && codepoint <= 0x05BF) ||
        (codepoint >= 0x05C1 && codepoint <= 0x05C2) ||
        (codepoint >= 0x05C4 && codepoint <= 0x05C5) ||
        (codepoint >= 0x05C7 && codepoint <= 0x05C7) ||
        (codepoint >= 0x0610 && codepoint <= 0x061A) ||
        (codepoint >= 0x064B && codepoint <= 0x065F) ||
        (codepoint >= 0x0670 && codepoint <= 0x0670) ||
        (codepoint >= 0x06D6 && codepoint <= 0x06DC) ||
        (codepoint >= 0x06DF && codepoint <= 0x06E4) ||
        (codepoint >= 0x06E7 && codepoint <= 0x06E8) ||
        (codepoint >= 0x06EA && codepoint <= 0x06ED) ||
        (codepoint >= 0x0711 && codepoint <= 0x0711) ||
        (codepoint >= 0x0730 && codepoint <= 0x074A) ||
        (codepoint >= 0x07A6 && codepoint <= 0x07B0) ||
        (codepoint >= 0x07EB && codepoint <= 0x07F3) ||
        (codepoint >= 0x0816 && codepoint <= 0x0819) ||
        (codepoint >= 0x081B && codepoint <= 0x0823) ||
        (codepoint >= 0x0825 && codepoint <= 0x0827) ||
        (codepoint >= 0x0829 && codepoint <= 0x082D) ||
        (codepoint >= 0x0859 && codepoint <= 0x085B) ||
        (codepoint >= 0x08E3 && codepoint <= 0x0902) ||
        (codepoint >= 0x093A && codepoint <= 0x093A) ||
        (codepoint >= 0x093C && codepoint <= 0x093C) ||
        (codepoint >= 0x0941 && codepoint <= 0x0948) ||
        (codepoint >= 0x094D && codepoint <= 0x094D) ||
        (codepoint >= 0x0951 && codepoint <= 0x0957) ||
        (codepoint >= 0x0962 && codepoint <= 0x0963) ||
        (codepoint >= 0x0981 && codepoint <= 0x0981) ||
        (codepoint >= 0x09BC && codepoint <= 0x09BC) ||
        (codepoint >= 0x09C1 && codepoint <= 0x09C4) ||
        (codepoint >= 0x09CD && codepoint <= 0x09CD) ||
        (codepoint >= 0x09E2 && codepoint <= 0x09E3) ||
        (codepoint >= 0x0E31 && codepoint <= 0x0E31) ||
        (codepoint >= 0x0E34 && codepoint <= 0x0E3A) ||
        (codepoint >= 0x0E47 && codepoint <= 0x0E4E) ||
        (codepoint >= 0x0EB1 && codepoint <= 0x0EB1) ||
        (codepoint >= 0x0EB4 && codepoint <= 0x0EBC) ||
        (codepoint >= 0x0EC8 && codepoint <= 0x0ECD) ||
        (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
        (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
        (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F) ||
        (codepoint >= 0x101FD && codepoint <= 0x101FD) ||
        (codepoint >= 0x102E0 && codepoint <= 0x102E0) ||
        (codepoint >= 0x10376 && codepoint <= 0x1037A) ||
        (codepoint >= 0x10A01 && codepoint <= 0x10A03) ||
        (codepoint >= 0x10A05 && codepoint <= 0x10A06) ||
        (codepoint >= 0x10A0C && codepoint <= 0x10A0F) ||
        (codepoint >= 0x10A38 && codepoint <= 0x10A3A) ||
        (codepoint >= 0x10A3F && codepoint <= 0x10A3F) ||
        (codepoint >= 0x1D167 && codepoint <= 0x1D169) ||
        (codepoint >= 0x1D17B && codepoint <= 0x1D182) ||
        (codepoint >= 0x1D185 && codepoint <= 0x1D18B) ||
        (codepoint >= 0x1D1AA && codepoint <= 0x1D1AD) ||
        (codepoint >= 0x1D242 && codepoint <= 0x1D244)) {
        return GC_EXTEND;
    }

    if (codepoint >= 0xAC00 && codepoint <= 0xD7AF) {
        unsigned sindex = codepoint - 0xAC00;
        return sindex % 28 == 0 ? GC_LV : GC_LVT;
    }

    if (codepoint >= 0x1100 && codepoint <= 0x115F) return GC_L;
    if (codepoint >= 0x1160 && codepoint <= 0x11A7) return GC_V;
    if (codepoint >= 0x11A8 && codepoint <= 0x11FF) return GC_T;

    if (codepoint >= 0xA960 && codepoint <= 0xA97C) return GC_L;
    if (codepoint >= 0xD7B0 && codepoint <= 0xD7C6) return GC_V;
    if (codepoint >= 0xD7CB && codepoint <= 0xD7FB) return GC_T;

    if ((codepoint >= 0x0903 && codepoint <= 0x0903) ||
        (codepoint >= 0x093B && codepoint <= 0x093B) ||
        (codepoint >= 0x093E && codepoint <= 0x0940) ||
        (codepoint >= 0x0949 && codepoint <= 0x094C) ||
        (codepoint >= 0x094E && codepoint <= 0x094F) ||
        (codepoint >= 0x0982 && codepoint <= 0x0983) ||
        (codepoint >= 0x09BE && codepoint <= 0x09C0) ||
        (codepoint >= 0x09C7 && codepoint <= 0x09C8) ||
        (codepoint >= 0x09CB && codepoint <= 0x09CC) ||
        (codepoint >= 0x09D7 && codepoint <= 0x09D7) ||
        (codepoint >= 0x0E33 && codepoint <= 0x0E33) ||
        (codepoint >= 0x0EB3 && codepoint <= 0x0EB3)) {
        return GC_SPACINGMARK;
    }

    if ((codepoint >= 0x0600 && codepoint <= 0x0605) ||
        (codepoint >= 0x06DD && codepoint <= 0x06DD) ||
        (codepoint >= 0x070F && codepoint <= 0x070F) ||
        (codepoint >= 0x08E2 && codepoint <= 0x08E2) ||
        (codepoint >= 0x110BD && codepoint <= 0x110BD)) {
        return GC_PREPEND;
    }

    return GC_OTHER;
}

/**
 * \brief Check if there's a grapheme cluster boundary between two code points
 */
static bool is_grapheme_boundary(unsigned prev, unsigned curr)
{
    GraphemeCategory prev_cat = get_grapheme_category(prev);
    GraphemeCategory curr_cat = get_grapheme_category(curr);

    if (prev == 0) return true;
    if (curr == 0) return true;

    if (prev_cat == GC_CR && curr_cat == GC_LF) return false;

    if (prev_cat == GC_CONTROL || prev_cat == GC_CR || prev_cat == GC_LF) return true;
    if (curr_cat == GC_CONTROL || curr_cat == GC_CR || curr_cat == GC_LF) return true;

    if (curr_cat == GC_EXTEND || curr_cat == GC_ZWJ) return false;
    if (curr_cat == GC_SPACINGMARK) return false;
    if (prev_cat == GC_PREPEND) return false;

    if (prev_cat == GC_REGIONAL_INDICATOR && curr_cat == GC_REGIONAL_INDICATOR) return false;

    if (prev_cat == GC_L &&
        (curr_cat == GC_L || curr_cat == GC_V || curr_cat == GC_LV || curr_cat == GC_LVT)) return false;
    if ((prev_cat == GC_LV || prev_cat == GC_V) &&
        (curr_cat == GC_V || curr_cat == GC_T)) return false;
    if ((prev_cat == GC_LVT || prev_cat == GC_T) && curr_cat == GC_T) return false;

    return true;
}

static int ass_get_next_grapheme_cluster(char **str, unsigned *out, int out_cap)
{
    char *p = *str;
    int count = 0;

    if (!*p)
        return 0;

    unsigned prev = ass_utf8_get_char(&p);
    out[count++] = prev;

    while (*p) {
        // Override blocks and escapes are markup, not text: a cluster never
        // takes in a '{' or '\\', even after a Prepend character that would
        // otherwise join the next character into its cluster. The caller
        // then sees them at the same positions as upstream libass does.
        if (*p == '{' || *p == '\\')
            break;

        char *next_p = p;
        unsigned curr = ass_utf8_get_char(&next_p);

        if (is_grapheme_boundary(prev, curr))
            break;

        if (count >= out_cap)
            break;

        out[count++] = curr;
        p = next_p;
        prev = curr;
    }

    *str = p;
    return count;
}

/**
 * \brief Change current font, using setting from render_priv->state.
 */
void ass_update_font(RenderContext *state)
{
    unsigned val;
    ASS_FontDesc desc;

    desc.family = state->family;
    if (!desc.family.str)
        return;
    if (desc.family.len && desc.family.str[0] == '@') {
        desc.vertical = 1;
        desc.family.str++;
        desc.family.len--;
    } else {
        desc.vertical = 0;
    }

    val = state->bold;
    // 0 = normal, 1 = bold, >1 = exact weight
    if (val == 1 || val == -1)
        val = 700;               // bold
    else if (val <= 0)
        val = 400;               // normal
    desc.bold = val;

    val = state->italic;
    if (val == 1)
        val = 100;              // italic
    else if (val <= 0)
        val = 0;                // normal
    desc.italic = val;

    state->font = ass_font_new(state->renderer, &desc);
}

/**
 * \brief Convert double to int32_t without UB
 * on out-of-range values; match x86 behavior
 */
static inline int32_t dtoi32(double val)
{
    if (ass_isnan(val) || val <= INT32_MIN || val >= INT32_MAX + 1LL)
        return INT32_MIN;
    return val;
}

static double calc_anim(double new, double old, double pwr)
{
   return (1 - pwr) * old + new * pwr;
}

static int32_t calc_anim_int32(uint32_t new, uint32_t old, double pwr)
{
    return dtoi32(calc_anim(new, old, pwr));
}

/**
 * \brief Calculate a weighted average of two colors
 * calculates c1*(1-a) + c2*a, but separately for each component except alpha
 */
static void change_color(uint32_t *var, uint32_t new, double pwr)
{
    uint32_t co = ass_bswap32(*var);
    uint32_t cn = ass_bswap32(new);

    uint32_t cc = (calc_anim_int32(cn & 0xff0000, co & 0xff0000, pwr) & 0xff0000) |
                  (calc_anim_int32(cn & 0x00ff00, co & 0x00ff00, pwr) & 0x00ff00) |
                  (calc_anim_int32(cn & 0x0000ff, co & 0x0000ff, pwr) & 0x0000ff);

    (*var) = (ass_bswap32(cc & 0xffffff)) | _a(*var);
}

// like change_color, but for alpha component only
static inline void change_alpha(uint32_t *var, int32_t new, double pwr)
{
    *var = (*var & 0xFFFFFF00) | (uint8_t)calc_anim_int32(new, _a(*var), pwr);
}

/**
 * \brief Multiply two alpha values
 * \param a first value
 * \param b second value
 * \return result of multiplication
 * At least one of the parameters must be less than or equal to 0xFF.
 * The result is less than or equal to max(a, b, 0xFF).
 */
static inline uint32_t mult_alpha(uint32_t a, uint32_t b)
{
    return a - ((uint64_t) a * b + 0x7F) / 0xFF + b;
}

void ass_apply_fade(uint32_t *clr, int fade)
{
    // VSFilter compatibility: apply fade only when it's positive
    if (fade > 0)
        change_alpha(clr, mult_alpha(_a(*clr), fade), 1);
}

/**
 * \brief Calculate alpha value by piecewise linear function
 * Used for \fad, \fade implementation.
 */
static int
interpolate_alpha(long long now, int32_t t1, int32_t t2, int32_t t3,
                  int32_t t4, int a1, int a2, int a3)
{
    int a;
    double cf;

    if (now < t1) {
        a = a1;
    } else if (now < t2) {
        cf = ((double) (int32_t) ((uint32_t) now - t1)) /
                (int32_t) ((uint32_t) t2 - t1);
        a = a1 * (1 - cf) + a2 * cf;
    } else if (now < t3) {
        a = a2;
    } else if (now < t4) {
        cf = ((double) (int32_t) ((uint32_t) now - t3)) /
                (int32_t) ((uint32_t) t4 - t3);
        a = a2 * (1 - cf) + a3 * cf;
    } else {                    // now >= t4
        a = a3;
    }

    return a;
}

static int32_t parse_alpha_tag(char *str)
{
    int32_t alpha = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &alpha);
    return alpha;
}

static uint32_t parse_color_tag(char *str)
{
    int32_t color = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &color);
    return ass_bswap32((uint32_t) color);
}

/**
 * \brief find style by name as in \r
 * \param track track
 * \param name style name
 * \param len style name length
 * \return style in track->styles
 * Returns NULL if no style has the given name.
 */
static ASS_Style *lookup_style_strict(ASS_Track *track, char *name, size_t len)
{
    int i;
    for (i = track->n_styles - 1; i >= 0; --i) {
        if (strncmp(track->styles[i].Name, name, len) == 0 &&
            track->styles[i].Name[len] == '\0')
            return track->styles + i;
    }
    ass_msg(track->library, MSGL_WARN,
            "[%p]: Warning: no style named '%.*s' found",
            track, (int) len, name);
    return NULL;
}

/*
 * Override tags are handled in two steps. compile_tags() does everything
 * that depends only on the text of a {...} block: it splits the arguments
 * the way VSFilter does, matches the tag names and converts the numbers.
 * exec_tags() applies the compiled tags to the render context; everything
 * that depends on time, style, settings or earlier tags happens there.
 * Spans of text such as font and style names are stored as offsets from
 * the start of the block and resolved against the text being rendered.
 */

enum {
    TAG_XBORD,
    TAG_YBORD,
    TAG_XSHAD,
    TAG_YSHAD,
    TAG_FAX,
    TAG_FAY,
    TAG_CLIP,
    TAG_BLUR,
    TAG_FSCX,
    TAG_FSCY,
    TAG_FSC,
    TAG_FSP,
    TAG_FS,
    TAG_BORD,
    TAG_MOVE,
    TAG_FRX,
    TAG_FRY,
    TAG_FRZ,
    TAG_FN,
    TAG_ALPHA,
    TAG_AN,
    TAG_A,
    TAG_POS,
    TAG_FADE,
    TAG_ORG,
    TAG_T,
    TAG_T_END,
    TAG_COLOR,
    TAG_ALPHA_N,
    TAG_R,
    TAG_BE,
    TAG_B,
    TAG_I,
    TAG_KT,
    TAG_KF,
    TAG_KO,
    TAG_K,
    TAG_SHAD,
    TAG_S,
    TAG_U,
    TAG_PBO,
    TAG_P,
    TAG_Q,
    TAG_FE,
};

typedef struct tag_token {
    uint8_t type;
    uint8_t index;      // colour/alpha component; \iclip for TAG_CLIP;
                        // \t: the tokens up to the next TAG_T_END are
                        // the body
    bool has_arg;       // the tag has at least one argument
    bool flag;          // \fs: relative size; \fn: style font;
                        // \t: the rest of the block is nested;
                        // TAG_CLIP: vector clip
    union {
        double d[4];
        int32_t i[7];
        uint32_t color;
        struct {
            double x1, y1, x2, y2;
            int32_t t1, t2;
        } move;
        struct {
            int32_t t1, t2;
            double accel;
        } t;
        // \fn, \r and vector clip text; offset is from the block start
        struct {
            size_t offset, len;
            int32_t scale;
        } str;
    } v;
} TagToken;

typedef struct {
    double pwr;         // multiplier for some tag effects (comes from \t tags)
    bool nested;        // whether this is the body of a \t tag
    double outer_pwr;   // pwr and nested to restore after a \t body
    bool outer_nested;
} TagExecState;

// Number of tokens compiled at a time when they are applied right away
#define TAG_CHUNK_SIZE 64

typedef struct {
    char *start;
    TagToken *tokens;
    size_t count, capacity;
    // If state is set, a full buffer is applied to it and then reused,
    // so a block of any size is compiled in bounded memory.
    RenderContext *state;
    TagExecState exec;
    TagToken dummy;     // written to on overflow, which cannot happen
} TagCompiler;

static void exec_tags(RenderContext *state, char *base, const TagToken *tok,
                      const TagToken *end, TagExecState *x);

static void flush_tags(TagCompiler *c)
{
    exec_tags(c->state, c->start, c->tokens, c->tokens + c->count, &c->exec);
    c->count = 0;
}

static TagToken *add_token(TagCompiler *c, int type, int nargs)
{
    if (c->count == c->capacity && c->state)
        flush_tags(c);
    TagToken *tok = &c->dummy;
    if (c->count < c->capacity)
        tok = &c->tokens[c->count];
    c->count++;
    memset(tok, 0, sizeof(*tok));
    tok->type = type;
    tok->has_arg = nargs > 0;
    return tok;
}

static void add_number(TagCompiler *c, int type, struct arg *args, int nargs)
{
    add_token(c, type, nargs)->v.d[0] = argtod(*args);
}

static void add_int(TagCompiler *c, int type, struct arg *args, int nargs)
{
    add_token(c, type, nargs)->v.i[0] = argtoi32(*args);
}

static void set_str(TagCompiler *c, TagToken *tok, char *start, char *end)
{
    tok->v.str.offset = start - c->start;
    tok->v.str.len = end - start;
}

static void add_clip(TagCompiler *c, bool inverse, struct arg *args, int nargs)
{
    TagToken *tok;
    if (nargs == 4) {
        tok = add_token(c, TAG_CLIP, nargs);
        for (int i = 0; i < 4; i++)
            tok->v.i[i] = argtoi32(args[i]);
    } else if (nargs == 1 || nargs == 2) {
        // vector clip, with an optional scale
        tok = add_token(c, TAG_CLIP, nargs);
        tok->flag = true;
        tok->v.str.scale = 1;
        if (nargs == 2)
            tok->v.str.scale = FFMAX(argtoi32(args[0]), 1);
        set_str(c, tok, args[nargs - 1].start, args[nargs - 1].end);
    } else
        return;
    tok->index = inverse;
}

static void add_color(TagCompiler *c, int index, struct arg *args, int nargs)
{
    TagToken *tok = add_token(c, TAG_COLOR, nargs);
    tok->index = index;
    tok->v.color = parse_color_tag(args->start);
}

static void add_alpha(TagCompiler *c, int index, struct arg *args, int nargs)
{
    TagToken *tok = add_token(c, TAG_ALPHA_N, nargs);
    tok->index = index;
    tok->v.i[0] = parse_alpha_tag(args->start);
}

/**
 * \brief Compile style override tags.
 * \param p string to parse
 * \param end end of string to parse, which must be '}', ')', or the first
 *            of a number of spaces immediately preceding '}' or ')'
 * \param nested whether this is the body of a \t tag
 */
static void compile_tags(TagCompiler *c, char *p, char *end, bool nested)
{
    for (char *q; p < end; p = q) {
        while (*p != '\\' && p != end)
            ++p;
        if (*p != '\\')
            break;
        ++p;
        if (p != end)
            skip_spaces(&p);

        q = p;
        while (*q != '(' && *q != '\\' && q != end)
            ++q;
        if (q == p)
            continue;

        char *name_end = q;

        // Store one extra element to be able to detect excess arguments
        struct arg args[MAX_VALID_NARGS + 1];
        int nargs = 0;
        bool has_backslash_arg = false;
        for (int i = 0; i <= MAX_VALID_NARGS; ++i)
            args[i].start = args[i].end = "";

        // Split parenthesized arguments. Do this for all tags and before
        // any non-parenthesized argument because that's what VSFilter does.
        if (*q == '(') {
            ++q;
            while (1) {
                if (q != end)
                    skip_spaces(&q);

                // Split on commas. If there is a backslash, ignore any
                // commas following it and lump everything starting from
                // the last comma, through the backslash and all the way
                // to the end of the argument string into a single argument.

                char *r = q;
                while (*r != ',' && *r != '\\' && *r != ')' && r != end)
                    ++r;

                if (*r == ',') {
                    push_arg(args, &nargs, q, r);
                    q = r + 1;
                } else {
                    // Swallow the rest of the parenthesized string. This could
                    // be either a backslash-argument or simply the last argument.
                    if (*r == '\\') {
                        has_backslash_arg = true;
                        char *paren = memchr(r, ')', end - r);
                        if (paren)
                            r = paren;
                        else
                            r = end;
                    }
                    push_arg(args, &nargs, q, r);
                    q = r;
                    // The closing parenthesis could be missing.
                    if (q != end)
                        ++q;
                    break;
                }
            }
        }

// Compare the first character inline to skip most mystrcmp() calls.
// This does not change which tag matches.
#define tag(name) (*p == (name)[0] && mystrcmp(&p, (name)) && \
                   (push_arg(args, &nargs, p, name_end), 1))
#define complex_tag(name) (*p == (name)[0] && mystrcmp(&p, (name)))

        // New tags introduced in vsfilter 2.39
        if (tag("xbord")) {
            add_number(c, TAG_XBORD, args, nargs);
        } else if (tag("ybord")) {
            add_number(c, TAG_YBORD, args, nargs);
        } else if (tag("xshad")) {
            add_number(c, TAG_XSHAD, args, nargs);
        } else if (tag("yshad")) {
            add_number(c, TAG_YSHAD, args, nargs);
        } else if (tag("fax")) {
            add_number(c, TAG_FAX, args, nargs);
        } else if (tag("fay")) {
            add_number(c, TAG_FAY, args, nargs);
        } else if (complex_tag("iclip")) {
            add_clip(c, true, args, nargs);
        } else if (tag("blur")) {
            add_number(c, TAG_BLUR, args, nargs);
            // ASS standard tags
        } else if (tag("fscx")) {
            add_number(c, TAG_FSCX, args, nargs);
        } else if (tag("fscy")) {
            add_number(c, TAG_FSCY, args, nargs);
        } else if (tag("fsc")) {
            add_token(c, TAG_FSC, nargs);
        } else if (tag("fsp")) {
            add_number(c, TAG_FSP, args, nargs);
        } else if (tag("fs")) {
            TagToken *tok = add_token(c, TAG_FS, nargs);
            tok->v.d[0] = argtod(*args);
            tok->flag = *args->start == '+' || *args->start == '-';
        } else if (tag("bord")) {
            add_number(c, TAG_BORD, args, nargs);
        } else if (complex_tag("move")) {
            if (nargs == 4 || nargs == 6) {
                TagToken *tok = add_token(c, TAG_MOVE, nargs);
                int32_t t1, t2;
                tok->v.move.x1 = argtod(args[0]);
                tok->v.move.y1 = argtod(args[1]);
                tok->v.move.x2 = argtod(args[2]);
                tok->v.move.y2 = argtod(args[3]);
                t1 = t2 = 0;
                if (nargs == 6) {
                    t1 = argtoi32(args[4]);
                    t2 = argtoi32(args[5]);
                    if (t1 > t2) {
                        long long tmp = t2;
                        t2 = t1;
                        t1 = tmp;
                    }
                }
                tok->v.move.t1 = t1;
                tok->v.move.t2 = t2;
            }
        } else if (tag("frx")) {
            add_number(c, TAG_FRX, args, nargs);
        } else if (tag("fry")) {
            add_number(c, TAG_FRY, args, nargs);
        } else if (tag("frz") || tag("fr")) {
            add_number(c, TAG_FRZ, args, nargs);
        } else if (tag("fn")) {
            TagToken *tok = add_token(c, TAG_FN, nargs);
            char *start = args->start;
            if (nargs && strncmp(start, "0", args->end - start)) {
                skip_spaces(&start);
                set_str(c, tok, start, args->end);
            } else
                tok->flag = true;
        } else if (tag("alpha")) {
            TagToken *tok = add_token(c, TAG_ALPHA, nargs);
            tok->v.i[0] = parse_alpha_tag(args->start);
        } else if (tag("an")) {
            add_int(c, TAG_AN, args, nargs);
        } else if (tag("a")) {
            add_int(c, TAG_A, args, nargs);
        } else if (complex_tag("pos")) {
            if (nargs == 2) {
                TagToken *tok = add_token(c, TAG_POS, nargs);
                tok->v.d[0] = argtod(args[0]);
                tok->v.d[1] = argtod(args[1]);
            }
        } else if (complex_tag("fade") || complex_tag("fad")) {
            int32_t *v;
            if (nargs == 2) {
                // 2-argument version (\fad, according to specs)
                v = add_token(c, TAG_FADE, nargs)->v.i;
                v[0] = 0xFF;
                v[1] = 0;
                v[2] = 0xFF;
                v[3] = -1;
                v[4] = argtoi32(args[0]);
                v[5] = argtoi32(args[1]);
                v[6] = -1;
            } else if (nargs == 7) {
                // 7-argument version (\fade)
                v = add_token(c, TAG_FADE, nargs)->v.i;
                for (int i = 0; i < 7; i++)
                    v[i] = argtoi32(args[i]);
            }
        } else if (complex_tag("org")) {
            if (nargs == 2) {
                TagToken *tok = add_token(c, TAG_ORG, nargs);
                tok->v.d[0] = argtod(args[0]);
                tok->v.d[1] = argtod(args[1]);
            }
        } else if (complex_tag("t")) {
            TagToken *tok = add_token(c, TAG_T, nargs);
            int cnt = nargs - 1;
            // VSFilter compatibility (because we can): parse the
            // timestamps differently depending on argument count.
            if (cnt == 3) {
                tok->v.t.t1 = argtoi32(args[0]);
                tok->v.t.t2 = argtoi32(args[1]);
                tok->v.t.accel = argtod(args[2]);
            } else if (cnt == 2) {
                tok->v.t.t1 = dtoi32(argtod(args[0]));
                tok->v.t.t2 = dtoi32(argtod(args[1]));
                tok->v.t.accel = 1.;
            } else if (cnt == 1) {
                tok->v.t.accel = argtod(args[0]);
            } else {
                tok->v.t.accel = 1.;
            }
            if (cnt < 0 || cnt > 3)
                continue;
            // If there's no backslash in the arguments, there are no
            // override tags, so it's pointless to try to parse them.
            if (!has_backslash_arg)
                continue;
            p = args[cnt].start;
            if (args[cnt].end < end) {
                assert(!nested);
                tok->index = 1;
                compile_tags(c, p, args[cnt].end, true);
                add_token(c, TAG_T_END, 0);
            } else {
                assert(q == end);
                // No other tags can possibly follow this \t tag,
                // so the rest of the block is its body.
                tok->flag = true;
                nested = true;
                q = p;
            }
        } else if (complex_tag("clip")) {
            add_clip(c, false, args, nargs);
        } else if (tag("c") || tag("1c")) {
            add_color(c, 0, args, nargs);
        } else if (tag("2c")) {
            add_color(c, 1, args, nargs);
        } else if (tag("3c")) {
            add_color(c, 2, args, nargs);
        } else if (tag("4c")) {
            add_color(c, 3, args, nargs);
        } else if (tag("1a")) {
            add_alpha(c, 0, args, nargs);
        } else if (tag("2a")) {
            add_alpha(c, 1, args, nargs);
        } else if (tag("3a")) {
            add_alpha(c, 2, args, nargs);
        } else if (tag("4a")) {
            add_alpha(c, 3, args, nargs);
        } else if (tag("r")) {
            TagToken *tok = add_token(c, TAG_R, nargs);
            if (nargs)
                set_str(c, tok, args->start, args->end);
        } else if (tag("be")) {
            add_number(c, TAG_BE, args, nargs);
        } else if (tag("b")) {
            add_int(c, TAG_B, args, nargs);
        } else if (tag("i")) {
            add_int(c, TAG_I, args, nargs);
        } else if (tag("kt")) {
            add_number(c, TAG_KT, args, nargs);
        } else if (tag("kf") || tag("K")) {
            add_number(c, TAG_KF, args, nargs);
        } else if (tag("ko")) {
            add_number(c, TAG_KO, args, nargs);
        } else if (tag("k")) {
            add_number(c, TAG_K, args, nargs);
        } else if (tag("shad")) {
            add_number(c, TAG_SHAD, args, nargs);
        } else if (tag("s")) {
            add_int(c, TAG_S, args, nargs);
        } else if (tag("u")) {
            add_int(c, TAG_U, args, nargs);
        } else if (tag("pbo")) {
            add_number(c, TAG_PBO, args, nargs);
        } else if (tag("p")) {
            add_int(c, TAG_P, args, nargs);
        } else if (tag("q")) {
            add_int(c, TAG_Q, args, nargs);
        } else if (tag("fe")) {
            add_int(c, TAG_FE, args, nargs);
        }
    }
}

/**
 * \brief Count the tokens the {...} block from start to end may compile to.
 * Every token except TAG_T_END consumes a different backslash. A TAG_T_END
 * ends the body of a \t tag, which has two backslashes of its own: one for
 * its name and one in its body.
 */
static size_t count_tags(char *start, char *end)
{
    size_t count = 0;
    for (char *s = start; (s = memchr(s, '\\', end - s)); s++)
        count++;
    return count + count / 2;
}

/**
 * \brief Compile the {...} block from start to end ('}') into tokens,
 * which must have room for count_tags(start, end) elements.
 * \return number of tokens
 */
static size_t build_tags(TagToken *tokens, size_t capacity,
                         char *start, char *end)
{
    TagCompiler c = {
        .start = start,
        .tokens = tokens,
        .capacity = capacity,
    };
    compile_tags(&c, start, end, false);
    assert(c.count <= capacity);
    return FFMIN(c.count, capacity);
}

// Blocks whose text or tokens are larger than this are not cached, so that
// a single block cannot take up a large part of the cache. They are compiled
// and applied in pieces every time instead, as when caching fails.
#define MAX_CACHED_TAGS_SIZE (TAGS_CACHE_MAX_SIZE / 64)
#define MAX_CACHED_TAGS_COUNT (MAX_CACHED_TAGS_SIZE / sizeof(TagToken))

static bool is_cacheable(char *start, char *end)
{
    size_t len = end - start;
    if (len >= MAX_CACHED_TAGS_SIZE)
        return false;
    // count_tags() is at most 1.5 times the length,
    // so only long blocks need to be counted.
    return len <= MAX_CACHED_TAGS_COUNT / 2 ||
           count_tags(start, end) <= MAX_CACHED_TAGS_COUNT;
}

// Compile a {...} block for the tag cache. The key holds a copy of the block.
size_t ass_tags_construct(void *key, void *value, void *priv)
{
    TagsHashKey *k = key;
    TagsHashValue *v = value;
    char *start = (char *) k->text.str;
    char *end = start + k->text.len - 1;
    size_t size = sizeof(TagsHashKey) + sizeof(TagsHashValue) + k->text.len + 1;

    size_t capacity = count_tags(start, end);
    v->tokens = ass_realloc_array(NULL, capacity, sizeof(TagToken));
    v->valid = v->tokens;
    v->count = 0;
    if (!v->valid)
        return size;
    v->count = build_tags(v->tokens, capacity, start, end);

    // Keep only the room that is used; for a block without \t,
    // count_tags() reserves half as much again as it needs.
    if (v->count < capacity) {
        TagToken *tokens =
            ass_realloc_array(v->tokens, v->count, sizeof(TagToken));
        if (tokens) {
            v->tokens = tokens;
            capacity = v->count;
        }
    }
    return size + capacity * sizeof(TagToken);
}

static uint32_t style_color(ASS_Style *style, int index)
{
    switch (index) {
    case 0:
        return style->PrimaryColour;
    case 1:
        return style->SecondaryColour;
    case 2:
        return style->OutlineColour;
    default:
        return style->BackColour;
    }
}

/**
 * \brief Apply compiled style override tags.
 * \param base start of the {...} block the tokens were compiled from
 * \param x \t state, which is updated so that the tokens of a block
 *          can be applied in several pieces
 */
static void exec_tags(RenderContext *state, char *base, const TagToken *tok,
                      const TagToken *end, TagExecState *x)
{
    ASS_Renderer *render_priv = state->renderer;
    double pwr = x->pwr;
    bool nested = x->nested;
    for (; tok < end; tok++) {
        switch (tok->type) {
        case TAG_XBORD: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0];
                val = state->border_x * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->Outline;
            state->border_x = val;
            break;
        }
        case TAG_YBORD: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0];
                val = state->border_y * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->Outline;
            state->border_y = val;
            break;
        }
        case TAG_XSHAD: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0];
                val = state->shadow_x * (1 - pwr) + val * pwr;
            } else
                val = state->style->Shadow;
            state->shadow_x = val;
            break;
        }
        case TAG_YSHAD: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0];
                val = state->shadow_y * (1 - pwr) + val * pwr;
            } else
                val = state->style->Shadow;
            state->shadow_y = val;
            break;
        }
        case TAG_FAX:
            if (tok->has_arg)
                state->fax =
                    tok->v.d[0] * pwr + state->fax * (1 - pwr);
            else
                state->fax = 0.;
            break;
        case TAG_FAY:
            if (tok->has_arg)
                state->fay =
                    tok->v.d[0] * pwr + state->fay * (1 - pwr);
            else
                state->fay = 0.;
            break;
        case TAG_CLIP:
            if (!tok->flag) {
                int32_t x0, y0, x1, y1;
                x0 = tok->v.i[0];
                y0 = tok->v.i[1];
                x1 = tok->v.i[2];
                y1 = tok->v.i[3];
                state->clip_x0 =
                    state->clip_x0 * (1 - pwr) + x0 * pwr;
                state->clip_x1 =
                    state->clip_x1 * (1 - pwr) + x1 * pwr;
                state->clip_y0 =
                    state->clip_y0 * (1 - pwr) + y0 * pwr;
                state->clip_y1 =
                    state->clip_y1 * (1 - pwr) + y1 * pwr;
                state->clip_mode = tok->index;
            } else if (!state->clip_drawing_text.str) {
                state->clip_drawing_text.str = base + tok->v.str.offset;
                state->clip_drawing_text.len = tok->v.str.len;
                state->clip_drawing_scale = tok->v.str.scale;
                state->clip_drawing_mode = tok->index;
            }
            break;
        case TAG_BLUR:
            if (tok->has_arg) {
                double val = tok->v.d[0];
                val = state->blur * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
                val = (val > BLUR_MAX_RADIUS) ? BLUR_MAX_RADIUS : val;
                state->blur = val;
            } else
                state->blur = 0.0;
            break;
        case TAG_FSCX: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0] / 100;
                val = state->scale_x * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->ScaleX;
            state->scale_x = val;
            break;
        }
        case TAG_FSCY: {
            double val;
            if (tok->has_arg) {
                val = tok->v.d[0] / 100;
                val = state->scale_y * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->ScaleY;
            state->scale_y = val;
            break;
        }
        case TAG_FSC:
            state->scale_x = state->style->ScaleX;
            state->scale_y = state->style->ScaleY;
            break;
        case TAG_FSP:
            if (tok->has_arg)
                state->hspacing =
                    state->hspacing * (1 - pwr) + tok->v.d[0] * pwr;
            else
                state->hspacing = state->style->Spacing;
            break;
        case TAG_FS: {
            double val = 0;
            if (tok->has_arg) {
                val = tok->v.d[0];
                if (tok->flag)
                    val = state->font_size * (1 + pwr * val / 10);
                else
                    val = state->font_size * (1 - pwr) + val * pwr;
            }
            if (val <= 0)
                val = state->style->FontSize;
            state->font_size = val;
            break;
        }
        case TAG_BORD: {
            double val, xval, yval;
            if (tok->has_arg) {
                val = tok->v.d[0];
                xval = state->border_x * (1 - pwr) + val * pwr;
                yval = state->border_y * (1 - pwr) + val * pwr;
                xval = (xval < 0) ? 0 : xval;
                yval = (yval < 0) ? 0 : yval;
            } else
                xval = yval = state->style->Outline;
            state->border_x = xval;
            state->border_y = yval;
            break;
        }
        case TAG_MOVE: {
            double x1 = tok->v.move.x1, y1 = tok->v.move.y1;
            double x2 = tok->v.move.x2, y2 = tok->v.move.y2;
            int32_t t1 = tok->v.move.t1, t2 = tok->v.move.t2;
            int32_t delta_t, t;
            double x, y;
            double k;
            if (t1 <= 0 && t2 <= 0) {
                t1 = 0;
                t2 = state->event->Duration;
            }
            delta_t = (uint32_t) t2 - t1;
            t = render_priv->time - state->event->Start;
            if (t <= t1)
                k = 0.;
            else if (t >= t2)
                k = 1.;
            else
                k = ((double) (int32_t) ((uint32_t) t - t1)) / delta_t;
            x = k * (x2 - x1) + x1;
            y = k * (y2 - y1) + y1;
            if (!(state->evt_type & EVENT_POSITIONED)) {
                state->pos_x = x;
                state->pos_y = y;
                state->detect_collisions = 0;
                state->evt_type |= EVENT_POSITIONED;
            }
            break;
        }
        case TAG_FRX:
            if (tok->has_arg)
                state->frx =
                    tok->v.d[0] * pwr + state->frx * (1 - pwr);
            else
                state->frx = 0.;
            break;
        case TAG_FRY:
            if (tok->has_arg)
                state->fry =
                    tok->v.d[0] * pwr + state->fry * (1 - pwr);
            else
                state->fry = 0.;
            break;
        case TAG_FRZ:
            if (tok->has_arg)
                state->frz =
                    tok->v.d[0] * pwr + state->frz * (1 - pwr);
            else
                state->frz =
                    state->style->Angle;
            break;
        case TAG_FN:
            if (!tok->flag) {
                state->family.str = base + tok->v.str.offset;
                state->family.len = tok->v.str.len;
            } else {
                state->family.str = state->style->FontName;
                state->family.len = strlen(state->style->FontName);
            }
            ass_update_font(state);
            break;
        case TAG_ALPHA:
            if (tok->has_arg) {
                for (int i = 0; i < 4; ++i)
                    change_alpha(&state->c[i], tok->v.i[0], pwr);
            } else {
                for (int i = 0; i < 4; ++i)
                    change_alpha(&state->c[i],
                                 _a(style_color(state->style, i)), 1);
            }
            break;
        case TAG_AN: {
            int32_t val = tok->v.i[0];
            if ((state->parsed_tags & PARSED_A) == 0) {
                if (val >= 1 && val <= 9)
                    state->alignment = numpad2align(val);
                else
                    state->alignment =
                        state->style->Alignment;
                state->parsed_tags |= PARSED_A;
            }
            break;
        }
        case TAG_A: {
            int32_t val = tok->v.i[0];
            if ((state->parsed_tags & PARSED_A) == 0) {
                if (val >= 1 && val <= 11)
                    // take care of a vsfilter quirk:
                    // handle illegal \a8 and \a4 like \a5
                    state->alignment = ((val & 3) == 0) ? 5 : val;
                else
                    state->alignment =
                        state->style->Alignment;
                state->parsed_tags |= PARSED_A;
            }
            break;
        }
        case TAG_POS:
            if (state->evt_type & EVENT_POSITIONED) {
                ass_msg(render_priv->library, MSGL_V, "Subtitle has a new \\pos "
                       "after \\move or \\pos, ignoring");
            } else {
                state->evt_type |= EVENT_POSITIONED;
                state->detect_collisions = 0;
                state->pos_x = tok->v.d[0];
                state->pos_y = tok->v.d[1];
            }
            break;
        case TAG_FADE: {
            int32_t a1 = tok->v.i[0], a2 = tok->v.i[1], a3 = tok->v.i[2];
            int32_t t1 = tok->v.i[3], t2 = tok->v.i[4];
            int32_t t3 = tok->v.i[5], t4 = tok->v.i[6];
            if (t1 == -1 && t4 == -1) {
                t1 = 0;
                t4 = state->event->Duration;
                t3 = (uint32_t) t4 - t3;
            }
            if ((state->parsed_tags & PARSED_FADE) == 0) {
                state->fade =
                    interpolate_alpha(render_priv->time -
                            state->event->Start, t1, t2,
                            t3, t4, a1, a2, a3);
                state->parsed_tags |= PARSED_FADE;
            }
            break;
        }
        case TAG_ORG:
            if (!state->have_origin) {
                state->org_x = tok->v.d[0];
                state->org_y = tok->v.d[1];
                state->have_origin = 1;
                state->detect_collisions = 0;
            }
            break;
        case TAG_T: {
            int32_t t1 = tok->v.t.t1, t2 = tok->v.t.t2, t, delta_t;
            double k;
            state->detect_collisions = 0;
            if (t2 == 0)
                t2 = state->event->Duration;
            delta_t = (uint32_t) t2 - t1;
            t = render_priv->time - state->event->Start;        // FIXME: move to render_context
            if (t < t1)
                k = 0.;
            else if (t >= t2)
                k = 1.;
            else {
                assert(delta_t != 0.);
                k = pow((double) (int32_t) ((uint32_t) t - t1) / delta_t,
                        tok->v.t.accel);
            }
            if (nested)
                pwr = k;
            if (tok->flag) {
                // The rest of the block is the body of this \t tag.
                pwr = k;
                nested = true;
            } else if (tok->index) {
                x->outer_pwr = pwr;
                x->outer_nested = nested;
                pwr = k;
                nested = true;
            }
            break;
        }
        case TAG_T_END:
            pwr = x->outer_pwr;
            nested = x->outer_nested;
            break;
        case TAG_COLOR:
            if (tok->has_arg)
                change_color(&state->c[tok->index], tok->v.color, pwr);
            else
                change_color(&state->c[tok->index],
                             style_color(state->style, tok->index), 1);
            break;
        case TAG_ALPHA_N:
            if (tok->has_arg)
                change_alpha(&state->c[tok->index], tok->v.i[0], pwr);
            else
                change_alpha(&state->c[tok->index],
                             _a(style_color(state->style, tok->index)), 1);
            break;
        case TAG_R:
            if (tok->has_arg) {
                int len = tok->v.str.len;
                ass_reset_render_context(state,
                        lookup_style_strict(render_priv->track,
                                            base + tok->v.str.offset, len));
            } else
                ass_reset_render_context(state, NULL);
            break;
        case TAG_BE:
            if (tok->has_arg) {
                int32_t val;
                // VSFilter always adds +0.5, even if the value is negative
                val = dtoi32(state->be * (1 - pwr) + tok->v.d[0] * pwr + 0.5);
                // Clamp to a safe upper limit, since high values need excessive CPU
                val = (val < 0) ? 0 : val;
                val = (val > MAX_BE) ? MAX_BE : val;
                state->be = val;
            } else
                state->be = 0;
            break;
        case TAG_B: {
            int32_t val = tok->v.i[0];
            if (!tok->has_arg || !(val == 0 || val == 1 || val >= 100))
                val = state->style->Bold;
            state->bold = val;
            ass_update_font(state);
            break;
        }
        case TAG_I: {
            int32_t val = tok->v.i[0];
            if (!tok->has_arg || !(val == 0 || val == 1))
                val = state->style->Italic;
            state->italic = val;
            ass_update_font(state);
            break;
        }
        case TAG_KT: {
            // v4++
            double val = 0;
            if (tok->has_arg)
                val = tok->v.d[0] * 10;
            state->effect_skip_timing = dtoi32(val);
            state->effect_timing = 0;
            state->reset_effect = true;
            break;
        }
        case TAG_KF:
        case TAG_KO:
        case TAG_K: {
            double val = 100;
            if (tok->has_arg)
                val = tok->v.d[0];
            state->effect_type = tok->type == TAG_KF ? EF_KARAOKE_KF :
                                 tok->type == TAG_KO ? EF_KARAOKE_KO :
                                 EF_KARAOKE;
            state->effect_skip_timing +=
                    (uint32_t) state->effect_timing;
            state->effect_timing = dtoi32(val * 10);
            break;
        }
        case TAG_SHAD: {
            double val, xval, yval;
            if (tok->has_arg) {
                val = tok->v.d[0];
                xval = state->shadow_x * (1 - pwr) + val * pwr;
                yval = state->shadow_y * (1 - pwr) + val * pwr;
                // VSFilter compatibility: clip for \shad but not for \[xy]shad
                xval = (xval < 0) ? 0 : xval;
                yval = (yval < 0) ? 0 : yval;
            } else
                xval = yval = state->style->Shadow;
            state->shadow_x = xval;
            state->shadow_y = yval;
            break;
        }
        case TAG_S: {
            int32_t val = tok->v.i[0];
            if (!tok->has_arg || !(val == 0 || val == 1))
                val = state->style->StrikeOut;
            if (val)
                state->flags |= DECO_STRIKETHROUGH;
            else
                state->flags &= ~DECO_STRIKETHROUGH;
            break;
        }
        case TAG_U: {
            int32_t val = tok->v.i[0];
            if (!tok->has_arg || !(val == 0 || val == 1))
                val = state->style->Underline;
            if (val)
                state->flags |= DECO_UNDERLINE;
            else
                state->flags &= ~DECO_UNDERLINE;
            break;
        }
        case TAG_PBO:
            state->pbo = tok->v.d[0];
            break;
        case TAG_P: {
            int32_t val = tok->v.i[0];
            val = (val < 0) ? 0 : val;
            state->drawing_scale = val;
            break;
        }
        case TAG_Q: {
            int32_t val = tok->v.i[0];
            if (!tok->has_arg || !(val >= 0 && val <= 3))
                val = render_priv->track->WrapStyle;
            state->wrap_style = val;
            break;
        }
        case TAG_FE: {
            int32_t val;
            if (tok->has_arg)
                val = tok->v.i[0];
            else
                val = state->style->Encoding;
            state->font_encoding = val;
            break;
        }
        }
    }
    x->pwr = pwr;
    x->nested = nested;
}

/**
 * \brief Parse style override tags.
 * \param p start of the block, which must be '{'
 * \param end end of the block, which must be '}'
 * \return end
 */
char *ass_parse_tags(RenderContext *state, char *p, char *end)
{
    ASS_Renderer *render_priv = state->renderer;

    // Blocks without a backslash, such as comments, contain no tags.
    if (!memchr(p, '\\', end - p))
        return end;

    // The tokens depend only on the content of the block, which is copied
    // into the key, so this is safe even if the event text is replaced
    // in place between frames.
    if (is_cacheable(p, end)) {
        Cache *cache = render_priv->cache.tags_cache;
        TagsHashKey key = { .text = { p, end + 1 - p } };
        TagsHashValue *val = ass_cache_get(cache, &key, NULL);
        bool hit = val && val->valid;
        if (hit) {
            TagExecState x = { .pwr = 1. };
            exec_tags(state, p, val->tokens, val->tokens + val->count, &x);
        }
        // Unlike the other caches, this is limited after every block,
        // so that a frame with many different blocks cannot exceed it.
        ass_cache_cut(cache, TAGS_CACHE_MAX_SIZE);
        if (hit)
            return end;
    }

    // The block is too large to cache or caching failed.
    TagToken tokens[TAG_CHUNK_SIZE];
    TagCompiler c = {
        .start = p,
        .tokens = tokens,
        .capacity = TAG_CHUNK_SIZE,
        .state = state,
        .exec = { .pwr = 1. },
    };
    compile_tags(&c, p, end, false);
    flush_tags(&c);
    return end;
}

void ass_apply_transition_effects(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    int v[4];
    int cnt;
    ASS_Event *event = state->event;
    char *p = event->Effect;

    if (!p || !*p)
        return;

    cnt = 0;
    while (cnt < 4 && (p = strchr(p, ';'))) {
        v[cnt++] = atoi(++p);
    }

    ASS_Vector layout_res = ass_layout_res(render_priv);
    if (strncmp(event->Effect, "Banner;", 7) == 0) {
        double delay;
        if (cnt < 1) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        if (cnt >= 2 && v[1])   // left-to-right
            state->scroll_direction = SCROLL_LR;
        else                    // right-to-left
            state->scroll_direction = SCROLL_RL;

        delay = v[0];
        // VSF works in storage coordinates, but scales delay to PlayRes canvas
        // before applying max(scaled_ delay, 1). This means, if scaled_delay < 1
        // (esp. delay=0) we end up with 1 ms per _storage pixel_ without any
        // PlayRes scaling.
        // The way libass deals with delay, it is automatically relative to the
        // PlayRes canvas, so we only want to "unscale" the small delay values.
        //
        // VSF also casts the scaled delay to int, which if not emulated leads to
        // easily noticeable deviations from VSFilter as the effect goes on.
        // To achieve both we need to keep our Playres-relative delay with high precision,
        // but must temporarily convert to storage-relative and truncate and take the
        // maxuimum there, before converting back.
        double scale_x = ((double) layout_res.x) / render_priv->track->PlayResX;
        delay = ((int) FFMAX(delay / scale_x, 1)) * scale_x;
        state->scroll_shift =
            (render_priv->time - event->Start) / delay;
        state->evt_type |= EVENT_HSCROLL;
        state->detect_collisions = 0;
        state->wrap_style = 2;
        return;
    }

    if (strncmp(event->Effect, "Scroll up;", 10) == 0) {
        state->scroll_direction = SCROLL_BT;
    } else if (strncmp(event->Effect, "Scroll down;", 12) == 0) {
        state->scroll_direction = SCROLL_TB;
    } else {
        ass_msg(render_priv->library, MSGL_DBG2,
                "Unknown transition effect: '%s'", event->Effect);
        return;
    }
    // parse scroll up/down parameters
    {
        double delay;
        int y0, y1;
        if (cnt < 3) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        delay = v[2];
        // See explanation for Banner
        double scale_y = ((double) layout_res.y) / render_priv->track->PlayResY;
        delay = ((int) FFMAX(delay / scale_y, 1)) * scale_y;
        state->scroll_shift =
            (render_priv->time - event->Start) / delay;
        if (v[0] < v[1]) {
            y0 = v[0];
            y1 = v[1];
        } else {
            y0 = v[1];
            y1 = v[0];
        }
        state->scroll_y0 = y0;
        state->scroll_y1 = y1;
        state->evt_type |= EVENT_VSCROLL;
        state->detect_collisions = 0;
    }

}

/**
 * \brief determine karaoke effects
 * Karaoke effects cannot be calculated during parse stage (ass_get_next_char()),
 * so they are done in a separate step.
 * Parse stage: when karaoke style override is found, its parameters are stored in the next glyph's
 * (the first glyph of the karaoke word)'s effect_type and effect_timing.
 * This function:
 * 1. sets effect_type for all glyphs in the word (_karaoke_ word)
 * 2. sets effect_timing for all glyphs to x coordinate of the border line between the left and right karaoke parts
 * (left part is filled with PrimaryColour, right one - with SecondaryColour).
 */
void ass_process_karaoke_effects(RenderContext *state)
{
    TextInfo *text_info = &state->text_info;
    long long tm_current = state->renderer->time - state->event->Start;

    int32_t timing = 0, skip_timing = 0;
    Effect effect_type = EF_NONE;
    GlyphInfo *last_boundary = NULL;
    bool has_reset = false;
    for (int i = 0; i <= text_info->length; i++) {
        if (i < text_info->length &&
            !text_info->glyphs[i].starts_new_run) {

            if (text_info->glyphs[i].reset_effect) {
                has_reset = true;
                skip_timing = 0;
            }

            // VSFilter compatibility: if we have \k12345\k0 without a run
            // break, subsequent text is still part of the same karaoke word,
            // the current word's starting and ending time stay unchanged,
            // but the starting time of the next karaoke word is advanced.
            skip_timing += (uint32_t) text_info->glyphs[i].effect_skip_timing;
            continue;
        }

        GlyphInfo *start = last_boundary;
        GlyphInfo *end = text_info->glyphs + i;
        last_boundary = end;
        if (!start)
            continue;

        if (start->effect_type != EF_NONE)
            effect_type = start->effect_type;
        if (effect_type == EF_NONE)
            continue;

        if (start->reset_effect)
            timing = 0;

        long long tm_start = timing + start->effect_skip_timing;
        long long tm_end = tm_start + start->effect_timing;
        timing = !has_reset * tm_end + skip_timing;
        skip_timing = 0;
        has_reset = false;

        if (effect_type != EF_KARAOKE_KF)
            tm_end = tm_start;

        int x;
        if (tm_current < tm_start)
            x = -100000000;
        else if (tm_current >= tm_end)
            x = 100000000;
        else {
            GlyphInfo *first_visible = start, *last_visible = end - 1;
            while (first_visible < last_visible && first_visible->skip)
                ++first_visible;
            while (first_visible < last_visible && last_visible->skip)
                --last_visible;

            int x_start = first_visible->pos.x;
            int x_end = last_visible->pos.x + last_visible->advance.x;
            double dt = (double) (tm_current - tm_start) / (tm_end - tm_start);
            double frz = fmod(start->frz, 360);
            if (frz > 90 && frz < 270) {
                // Fill from right to left
                dt = 1 - dt;
                for (GlyphInfo *info = start; info < end; info++) {
                    uint32_t tmp = info->c[0];
                    info->c[0] = info->c[1];
                    info->c[1] = tmp;
                }
            }
            x = x_start + ass_lrint((x_end - x_start) * dt);
        }

        for (GlyphInfo *info = start; info < end; info++) {
            info->effect_type = effect_type;
            info->effect_timing = x - info->pos.x;
        }
    }
}


/**
 * \brief Get next ucs4 char from string, parsing UTF-8 and escapes
 * \param str string pointer
 * \return ucs4 code of the next char
 * On return str points to the unparsed part of the string
 */
unsigned ass_get_next_char(RenderContext *state, char **str)
{
    char *p = *str;
    unsigned chr;

    if (state->grapheme_pending_pos < state->grapheme_pending_len)
        return state->grapheme_pending[state->grapheme_pending_pos++];
    
    if (*p == '\t') {
        ++p;
        *str = p;
        return ' ';
    }
    if (*p == '\\') {
        if ((p[1] == 'N') || ((p[1] == 'n') &&
                              (state->wrap_style == 2))) {
            p += 2;
            *str = p;
            return '\n';
        } else if (p[1] == 'n') {
            p += 2;
            *str = p;
            return ' ';
        } else if (p[1] == 'h') {
            p += 2;
            *str = p;
            return NBSP;
        } else if (p[1] == '{') {
            p += 2;
            *str = p;
            return '{';
        } else if (p[1] == '}') {
            p += 2;
            *str = p;
            return '}';
        }
    }
    int cluster_len = ass_get_next_grapheme_cluster((char **) &p, state->grapheme_pending,
                                                    sizeof(state->grapheme_pending) /
                                                        sizeof(state->grapheme_pending[0]));
    if (cluster_len <= 0) {
        *str = p;
        return 0;
    }

    state->grapheme_pending_len = cluster_len;
    state->grapheme_pending_pos = 1;
    chr = state->grapheme_pending[0];
    *str = p;
    return chr;
}

// Return 1 if the event contains tags that will apply overrides the selective
// style override code should not touch. Return 0 otherwise.
int ass_event_has_hard_overrides(char *str)
{
    // look for \pos and \move tags inside {...}
    // mirrors ass_get_next_char, but is faster and doesn't change any global state
    while (*str) {
        if (str[0] == '\\' && str[1] != '\0') {
            str += 2;
        } else if (str[0] == '{') {
            str++;
            while (*str && *str != '}') {
                if (*str == '\\') {
                    char *p = str + 1;
                    if (mystrcmp(&p, "pos") || mystrcmp(&p, "move") ||
                        mystrcmp(&p, "clip") || mystrcmp(&p, "iclip") ||
                        mystrcmp(&p, "org") || mystrcmp(&p, "pbo") ||
                        mystrcmp(&p, "p"))
                        return 1;
                }
                str++;
            }
        } else {
            str++;
        }
    }
    return 0;
}
