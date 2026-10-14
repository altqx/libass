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

#ifndef LIBASS_ATLAS_H
#define LIBASS_ATLAS_H

// Per-renderer state of ass_frame_pack_atlas(), defined in ass_atlas.c.
typedef struct atlas_state AtlasState;

/**
 * \brief Release the atlas state and the frame reference it holds.
 * Must be called before the renderer's caches and image pool are destroyed.
 * \param priv renderer that owns the state
 * \param state state to free, or NULL
 */
void ass_atlas_state_free(ASS_Renderer *priv, AtlasState *state);

#endif /* LIBASS_ATLAS_H */
