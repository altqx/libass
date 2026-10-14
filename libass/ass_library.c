/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
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

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef CONFIG_UNIBREAK
#include <linebreak.h>
#endif

#include "ass.h"
#include "ass_library.h"
#include "ass_utils.h"
#include "ass_string.h"

static void ass_msg_handler(int level, const char *fmt, va_list va, void *data)
{
    if (level > MSGL_INFO)
        return;
    fprintf(stderr, "[ass] ");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
}

ASS_Library *ass_library_init(void)
{
    ASS_Library* lib = calloc(1, sizeof(*lib));
    if (lib)
        lib->msg_callback = ass_msg_handler;
    #ifdef CONFIG_UNIBREAK
    // libunibreak works without, but its docs suggest this improves performance
    init_linebreak();
    #endif
    return lib;
}

void ass_library_done(ASS_Library *priv)
{
    if (priv) {
        ass_set_fonts_dir(priv, NULL);
        ass_set_style_overrides(priv, NULL);
        ass_clear_fonts(priv);
        free(priv);
    }
}

void ass_set_fonts_dir(ASS_Library *priv, const char *fonts_dir)
{
    free(priv->fonts_dir);

    priv->fonts_dir = fonts_dir ? strdup(fonts_dir) : 0;
}

void ass_set_extract_fonts(ASS_Library *priv, int extract)
{
    priv->extract_fonts = !!extract;
}

void ass_set_style_overrides(ASS_Library *priv, char **list)
{
    // Documentation promises input lists gets copied without modifications
    char **p;
    char **q;
    int cnt;

    if (priv->style_overrides) {
        for (p = priv->style_overrides; *p; ++p)
            free(*p);
    }
    free(priv->style_overrides);
    priv->style_overrides = NULL;

    if (!list)
        return;

    for (p = list, cnt = 0; *p; ++p, ++cnt) {
    }

    priv->style_overrides = calloc(cnt + 1, sizeof(char *));
    if (!priv->style_overrides)
        return;
    for (p = list, q = priv->style_overrides; *p; ++p, ++q)
        *q = strdup(*p);
}

/**
 * \brief Append an entry for a memory font to priv->fontdata.
 * \return the entry with only its name set, or NULL on failure;
 *         num_fontdata is not incremented yet
 */
static ASS_Fontdata *new_fontdata(ASS_Library *priv, const char *name)
{
    size_t idx = priv->num_fontdata;
    if (!(idx & (idx - 32)) && // power of two >= 32, or zero --> time for realloc
            !ASS_REALLOC_ARRAY(priv->fontdata, FFMAX(2 * idx, 32)))
        return NULL;

    ASS_Fontdata *fd = priv->fontdata + idx;
    memset(fd, 0, sizeof(*fd));
    fd->name = strdup(name);
    return fd->name ? fd : NULL;
}

void ass_add_font(ASS_Library *priv, const char *name, const char *data, int size)
{
    if (!name || !data || size <= 0)
        return;
    ASS_Fontdata *fd = new_fontdata(priv, name);
    if (!fd)
        return;

    fd->data = malloc(size);
    if (!fd->data) {
        free(fd->name);
        return;
    }
    memcpy(fd->data, data, size);
    fd->size = size;

    priv->num_fontdata++;
}

void ass_add_font_ref(ASS_Library *priv, const char *name, const char *data,
                      size_t size, void (*release)(void *opaque), void *opaque)
{
    ASS_Fontdata *fd = NULL;
    if (priv && name && data && size && size <= INT_MAX)
        fd = new_fontdata(priv, name);
    if (!fd) {
        if (release)
            release(opaque);
        return;
    }

    fd->data = (char *) data;
    fd->size = (int) size;
    fd->is_ref = 1;
    fd->release = release;
    fd->opaque = opaque;

    priv->num_fontdata++;
}

void ass_clear_fonts(ASS_Library *priv)
{
    for (size_t i = 0; i < priv->num_fontdata; i++) {
        ASS_Fontdata *fd = priv->fontdata + i;
        free(fd->name);
        if (!fd->is_ref)
            free(fd->data);
        else if (fd->release)
            fd->release(fd->opaque);
    }
    free(priv->fontdata);
    priv->fontdata = NULL;
    priv->num_fontdata = 0;
}

/*
 * Register a message callback function with libass.  Without setting one,
 * a default handler is used which prints everything with MSGL_INFO or
 * higher to the standard output.
 *
 * \param msg_cb the callback function
 * \param data additional data that will be passed to the callback
 */
void ass_set_message_cb(ASS_Library *priv,
                        void (*msg_cb)(int, const char *, va_list, void *),
                        void *data)
{
    if (msg_cb) {
        priv->msg_callback = msg_cb;
        priv->msg_callback_data = data;
    }
}

void ass_set_font_diagnostics(ASS_Library *priv,
                              const ASS_FontDiagnostics *callbacks,
                              void *data)
{
    if (callbacks && callbacks->version == ASS_FONT_DIAGNOSTICS_VERSION) {
        priv->font_diagnostics = *callbacks;
        priv->font_diagnostics_data = data;
    } else {
        memset(&priv->font_diagnostics, 0, sizeof(priv->font_diagnostics));
        priv->font_diagnostics_data = NULL;
    }
}

void *ass_malloc(size_t size)
{
    return malloc(size);
}

void ass_free(void *ptr)
{
    free(ptr);
}
