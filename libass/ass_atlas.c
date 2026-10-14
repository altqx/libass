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
 * Frame atlas packing: copies the bitmaps of an ASS_Image list into R8
 * pages and emits one GPU-ready ASS_AtlasInstance per image (or tile).
 *
 * Layout is a deterministic shelf packer: cells are sorted by height,
 * then width (descending), with the instance index as the final tie-break,
 * and each cell goes to the first recent shelf with enough free width.
 * Pixels are then written row by row per shelf, so every byte of the
 * reported pages is written exactly once (image data or zero).
 *
 * Bitmap reuse: when the list is a frame of this renderer and its
 * (bitmap, stride, w, h) sequence equals that of the previous pack, the
 * pixels are left untouched and only positions/colors are refreshed. This
 * is exact because the previously packed frame stays referenced until the
 * next pack, so its bitmaps (immutable cache values or frame-owned buffers)
 * cannot be freed and their addresses cannot be reused for other content.
 */

#include "config.h"
#include "ass_compat.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ass.h"
#include "ass_atlas.h"
#include "ass_render.h"
#include "ass_utils.h"

#define ATLAS_MAX_SIZE      32768
#define ATLAS_MAX_PADDING   64
// Number of most recent shelves searched for free space; bounds the packer
// to O(n * ATLAS_SHELF_WINDOW) on adversarial input.
#define ATLAS_SHELF_WINDOW  64
#define ATLAS_ALIGN         64

#define ATLAS_CHECK(name, cond) typedef char atlas_check_##name[(cond) ? 1 : -1]
ATLAS_CHECK(instance_size, sizeof(ASS_AtlasInstance) == 32);
ATLAS_CHECK(instance_rect, offsetof(ASS_AtlasInstance, w) == 8);
ATLAS_CHECK(instance_src, offsetof(ASS_AtlasInstance, atlas_x) == 16);
ATLAS_CHECK(instance_color, offsetof(ASS_AtlasInstance, color) == 28);
// Sort keys pack 15-bit size complements and a 31-bit index.
ATLAS_CHECK(key_bits, ATLAS_MAX_SIZE <= (1 << 15) && INT_MAX <= 0x7FFFFFFF);

typedef struct {
    const unsigned char *bitmap;
    int stride, w, h;
    int dst_x, dst_y;
    uint32_t color;
    int first;                  // index of the image's first instance
} AtlasImage;

typedef struct {
    const unsigned char *src;   // first source pixel of the tile
    int stride;
    int image;                  // index into AtlasState.images
    int tx, ty;                 // tile offset inside the image
    int w, h;                   // tile size
    int x, y, shelf;            // placement
} AtlasCell;

typedef struct {
    int page, y, h;
    int x;                      // first free column
    int first, count;           // cell range in AtlasState.shelf_cells
} AtlasShelf;

struct atlas_state {
    ASS_Atlas result;
    bool valid;                 // result describes the last successful pack
    ASS_Image *frame;           // referenced renderer frame of that pack
    int max_size, padding;

    AtlasImage *images;
    size_t images_cap;
    int n_images;

    AtlasCell *cells;           // one per instance, in instance order
    size_t cells_cap;
    uint64_t *keys;             // placement order
    size_t keys_cap;
    int *shelf_cells;           // cell indices grouped by shelf, x ascending
    size_t shelf_cells_cap;
    AtlasShelf *shelves;
    size_t shelves_cap;
    int n_shelves;

    ASS_AtlasInstance *instances;
    size_t instances_cap;
    unsigned char *buffer;
    size_t buffer_cap;
};

/**
 * \brief Ensure capacity for at least need elements, growing geometrically.
 * Contents are not preserved. On failure the array is released.
 */
static bool grow_array(void **ptr, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap)
        return true;
    size_t max = SIZE_MAX / elem;
    if (need > max)
        return false;
    size_t n = *cap > max / 2 ? max : 2 * *cap;
    if (n < need)
        n = need;
    if (n < 16 && 16 <= max)
        n = 16;
    free(*ptr);
    *ptr = malloc(n * elem);
    *cap = *ptr ? n : 0;
    return *ptr;
}

#define GROW(st, arr, need) \
    grow_array((void **) &(st)->arr, &(st)->arr##_cap, need, sizeof(*(st)->arr))

/**
 * \brief Ensure the pixel buffer holds at least need bytes.
 * Grows to need plus 25% headroom rather than doubling, so one large frame
 * pins at most 1.25 times its own size (until ass_frame_atlas_reset()) while
 * slowly growing frames still reallocate only geometrically often.
 */
static bool grow_buffer(AtlasState *st, size_t need)
{
    if (need <= st->buffer_cap)
        return true;
    size_t n = need > SIZE_MAX - need / 4 ? need : need + need / 4;
    ass_aligned_free(st->buffer);
    st->buffer = ass_aligned_alloc(ATLAS_ALIGN, n, false);
    if (!st->buffer && n > need)
        st->buffer = ass_aligned_alloc(ATLAS_ALIGN, n = need, false);
    st->buffer_cap = st->buffer ? n : 0;
    return st->buffer;
}

static void set_frame(ASS_Renderer *priv, AtlasState *st, ASS_Image *frame)
{
    if (frame == st->frame)
        return;
    ass_frame_ref(frame);
    ass_frame_unref(priv, st->frame);
    st->frame = frame;
}

void ass_atlas_state_free(ASS_Renderer *priv, AtlasState *st)
{
    if (!st)
        return;
    ass_frame_unref(priv, st->frame);
    free(st->images);
    free(st->cells);
    free(st->keys);
    free(st->shelf_cells);
    free(st->shelves);
    free(st->instances);
    ass_aligned_free(st->buffer);
    free(st);
}

void ass_frame_atlas_reset(ASS_Renderer *priv)
{
    if (!priv)
        return;
    ass_atlas_state_free(priv, priv->atlas);
    priv->atlas = NULL;
}

static inline bool skip_image(const ASS_Image *img)
{
    return img->w <= 0 || img->h <= 0 || !img->bitmap ||
           (img->color & 0xFF) == 0xFF;
}

static void sift_down(uint64_t *keys, size_t root, size_t n)
{
    uint64_t v = keys[root];
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n)
            break;
        if (child + 1 < n && keys[child + 1] > keys[child])
            child++;
        if (keys[child] <= v)
            break;
        keys[root] = keys[child];
        root = child;
    }
    keys[root] = v;
}

/**
 * \brief Sort keys ascending in place.
 * Heapsort instead of qsort(), which may allocate (glibc merge sort);
 * keys are unique, so the order is fully determined.
 */
static void sort_keys(uint64_t *keys, size_t n)
{
    if (n < 2)
        return;
    for (size_t i = n / 2; i-- > 0;)
        sift_down(keys, i, n);
    for (size_t end = n - 1; end > 0; end--) {
        uint64_t top = keys[0];
        keys[0] = keys[end];
        keys[end] = top;
        sift_down(keys, 0, end);
    }
}

/**
 * \brief Place all cells on shelves of the given width.
 * The shelves array must hold n_cells entries (one shelf per cell at most).
 */
static void place_cells(AtlasState *st, int n_cells, int width, int max_size,
                        int pad, int *n_pages, int *used_w, int *height)
{
    int n_shelves = 0, page = 0, next_y = pad, max_y = 0;
    for (int k = 0; k < n_cells; k++) {
        AtlasCell *cell = &st->cells[st->keys[k] & 0x7FFFFFFF];
        int need = cell->w + pad;

        int s = n_shelves > ATLAS_SHELF_WINDOW ? n_shelves - ATLAS_SHELF_WINDOW : 0;
        for (; s < n_shelves; s++)
            if (st->shelves[s].x <= width - need)
                break;

        if (s == n_shelves) {
            // Heights are non-increasing, so the new shelf is as tall as
            // this cell; cell->h + 2 * pad <= max_size holds by tiling.
            if (cell->h + pad > max_size - next_y) {
                if (next_y > max_y)
                    max_y = next_y;
                page++;
                next_y = pad;
            }
            st->shelves[s] = (AtlasShelf) {
                .page = page, .y = next_y, .h = cell->h, .x = pad,
            };
            next_y += cell->h + pad;
            n_shelves++;
        }

        AtlasShelf *shelf = &st->shelves[s];
        cell->x = shelf->x;
        cell->y = shelf->y;
        cell->shelf = s;
        shelf->x += need;
        shelf->count++;
    }
    if (next_y > max_y)
        max_y = next_y;
    int max_x = 0;
    for (int s = 0; s < n_shelves; s++)
        max_x = FFMAX(max_x, st->shelves[s].x);
    st->n_shelves = n_shelves;
    *used_w = max_x;
    *n_pages = page + 1;
    *height = max_y;
}

static void emit_shelf(AtlasState *st, const AtlasShelf *shelf,
                       unsigned char *page, int width)
{
    const int *list = st->shelf_cells + shelf->first;
    for (int r = 0; r < shelf->h; r++) {
        unsigned char *dst = page + (size_t) (shelf->y + r) * width;
        int x0 = 0;
        for (int i = 0; i < shelf->count; i++) {
            const AtlasCell *cell = &st->cells[list[i]];
            memset(dst + x0, 0, cell->x - x0);
            if (r < cell->h)
                memcpy(dst + cell->x, cell->src + (ptrdiff_t) r * cell->stride,
                       cell->w);
            else
                memset(dst + cell->x, 0, cell->w);
            x0 = cell->x + cell->w;
        }
        memset(dst + x0, 0, width - x0);
    }
}

static void emit_pages(AtlasState *st, int n_pages, int width, int height)
{
    size_t page_size = (size_t) width * height;
    unsigned char *page = st->buffer;
    int cur = 0, row = 0;
    for (int s = 0; s < st->n_shelves; s++) {
        const AtlasShelf *shelf = &st->shelves[s];
        while (shelf->page != cur) {
            memset(page + (size_t) row * width, 0, (size_t) (height - row) * width);
            page += page_size;
            cur++;
            row = 0;
        }
        memset(page + (size_t) row * width, 0, (size_t) (shelf->y - row) * width);
        emit_shelf(st, shelf, page, width);
        row = shelf->y + shelf->h;
    }
    memset(page + (size_t) row * width, 0, (size_t) (height - row) * width);
}

static const ASS_Atlas *pack_empty(AtlasState *st)
{
    ASS_Atlas *res = &st->result;
    bool was_empty = st->valid && !res->n_pages;
    bool had_none = st->valid && !res->n_instances;
    *res = (ASS_Atlas) {
        .instances = st->instances,
        .pixels_changed = !was_empty,
        .instances_changed = !had_none,
    };
    return res;
}

/**
 * \brief Lay out and copy n validated images from scratch.
 * \return false if more than max_pages pages are needed, on size overflow
 * or on allocation failure
 */
static bool pack_full(AtlasState *st, ASS_Image *images, int n,
                      int max_size, int pad, int max_pages)
{
    ASS_Atlas *res = &st->result;
    const int tile = max_size - 2 * pad;

    if (!GROW(st, images, n))
        return false;
    // Every cell occupies (w + pad) x (h + pad) texels inside the
    // (max_size - pad)^2 area of its page, which bounds the page count from
    // below; check it before anything proportional to the cell count is
    // allocated. The bound is at most 2^31 * 2^30, so it cannot overflow.
    const uint64_t page_area = (uint64_t) (max_size - pad) * (max_size - pad);
    const uint64_t max_area = (uint64_t) max_pages * page_area;
    uint64_t n_cells = 0, area = 0;
    int k = 0;
    for (ASS_Image *img = images; img; img = img->next) {
        if (skip_image(img))
            continue;
        uint64_t nx = (img->w - 1) / tile + 1, ny = (img->h - 1) / tile + 1;
        uint64_t tiles = nx * ny;
        if (tiles > INT_MAX - n_cells)
            return false;
        // Both factors are below 2^38; their product may not fit.
        uint64_t ew = img->w + nx * pad, eh = img->h + ny * pad;
        if (eh > (max_area - area) / ew)
            return false;
        area += ew * eh;
        st->images[k++] = (AtlasImage) {
            .bitmap = img->bitmap, .stride = img->stride,
            .w = img->w, .h = img->h,
            .dst_x = img->dst_x, .dst_y = img->dst_y,
            .color = img->color, .first = (int) n_cells,
        };
        n_cells += tiles;
    }
    st->n_images = n;

    if (!GROW(st, cells, n_cells) || !GROW(st, keys, n_cells) ||
            !GROW(st, shelves, n_cells) || !GROW(st, shelf_cells, n_cells) ||
            !GROW(st, instances, n_cells))
        return false;

    int max_w = 0;
    int c = 0;
    for (k = 0; k < n; k++) {
        const AtlasImage *img = &st->images[k];
        for (int ty = 0; ty < img->h; ty += tile) {
            int th = FFMIN(tile, img->h - ty);
            for (int tx = 0; tx < img->w; tx += tile) {
                int tw = FFMIN(tile, img->w - tx);
                st->cells[c] = (AtlasCell) {
                    .src = img->bitmap + (ptrdiff_t) ty * img->stride + tx,
                    .stride = img->stride, .image = k,
                    .tx = tx, .ty = ty, .w = tw, .h = th,
                };
                st->keys[c] = (uint64_t) (ATLAS_MAX_SIZE - th) << 46 |
                              (uint64_t) (ATLAS_MAX_SIZE - tw) << 31 |
                              (uint64_t) c;
                max_w = FFMAX(max_w, tw);
                c++;
            }
        }
    }
    sort_keys(st->keys, c);

    // Aim for a roughly square page that fits the widest cell; width is
    // rounded up to a multiple of 4 where max_size permits.
    int width = max_w + 2 * pad;
    double side = ceil(sqrt((double) area)) + pad;
    if (side > width)
        width = side < max_size ? (int) side : max_size;
    width = FFMIN((width + 3) & ~3, max_size);

    int n_pages, used_w, height;
    place_cells(st, c, width, max_size, pad, &n_pages, &used_w, &height);
    if (n_pages > 1 && width < max_size) {
        width = max_size;
        place_cells(st, c, width, max_size, pad, &n_pages, &used_w, &height);
    }
    if (n_pages > max_pages)
        return false;
    // Drop unused columns; every shelf ends at most at used_w.
    width = FFMIN((used_w + 3) & ~3, width);

    size_t page_size = (size_t) width * height;
    if (n_pages > SIZE_MAX / page_size)
        return false;
    if (!grow_buffer(st, n_pages * page_size))
        return false;

    // Group cells by shelf; placement order keeps x ascending per shelf.
    int start = 0;
    for (int s = 0; s < st->n_shelves; s++) {
        st->shelves[s].first = start;
        start += st->shelves[s].count;
        st->shelves[s].count = 0;
    }
    for (int i = 0; i < c; i++) {
        int idx = st->keys[i] & 0x7FFFFFFF;
        AtlasShelf *shelf = &st->shelves[st->cells[idx].shelf];
        st->shelf_cells[shelf->first + shelf->count++] = idx;
    }

    emit_pages(st, n_pages, width, height);

    for (int i = 0; i < c; i++) {
        const AtlasCell *cell = &st->cells[i];
        const AtlasImage *img = &st->images[cell->image];
        st->instances[i] = (ASS_AtlasInstance) {
            .dst_x = img->dst_x + cell->tx, .dst_y = img->dst_y + cell->ty,
            .w = cell->w, .h = cell->h,
            .atlas_x = cell->x, .atlas_y = cell->y,
            .page = st->shelves[cell->shelf].page,
            .color = img->color,
        };
    }

    *res = (ASS_Atlas) {
        .instances = st->instances, .n_instances = c,
        .n_pages = n_pages, .width = width, .height = height,
        .buffer = st->buffer,
        .pixels_changed = 1, .instances_changed = 1,
    };
    return true;
}

/**
 * \brief Refresh positions and colors after a pixel-identical frame.
 */
static void update_placement(AtlasState *st, ASS_Image *images)
{
    int k = 0;
    for (ASS_Image *img = images; img; img = img->next) {
        if (skip_image(img))
            continue;
        AtlasImage *rec = &st->images[k++];
        rec->dst_x = img->dst_x;
        rec->dst_y = img->dst_y;
        rec->color = img->color;
        int end = k < st->n_images ? st->images[k].first : st->result.n_instances;
        for (int i = rec->first; i < end; i++) {
            ASS_AtlasInstance *inst = &st->instances[i];
            inst->dst_x = rec->dst_x + st->cells[i].tx;
            inst->dst_y = rec->dst_y + st->cells[i].ty;
            inst->color = rec->color;
        }
    }
}

const ASS_Atlas *ass_frame_pack_atlas(ASS_Renderer *priv, ASS_Image *images,
                                      int max_size, int padding,
                                      int max_pages)
{
    if (!priv)
        return NULL;

    AtlasState *st = priv->atlas;
    if (!st) {
        st = priv->atlas = calloc(1, sizeof(*st));
        if (!st)
            return NULL;
    }

    if (padding < 0 || padding > ATLAS_MAX_PADDING ||
            max_size <= 2 * padding || max_pages < 1)
        goto fail;
    max_size = FFMIN(max_size, ATLAS_MAX_SIZE);

    // Only a frame of this renderer can be compared by bitmap address:
    // the previously packed frame is still referenced, see above.
    bool own_frame = images && (images == priv->images_root ||
                                images == st->frame);
    bool same_pixels = own_frame && st->frame && st->valid &&
                       st->max_size == max_size && st->padding == padding &&
                       st->result.n_pages <= max_pages;
    bool same_place = true;

    // Validate, count, and compare with the previous pack.
    int n = 0;
    for (ASS_Image *img = images; img; img = img->next) {
        if (skip_image(img))
            continue;
        if (img->stride < img->w || n == INT_MAX ||
                img->dst_x > INT_MAX - (img->w - 1) ||
                img->dst_y > INT_MAX - (img->h - 1))
            goto fail;
        if (same_pixels && n >= st->n_images) {
            same_pixels = false;
        } else if (same_pixels) {
            const AtlasImage *prev = &st->images[n];
            if (prev->bitmap != img->bitmap || prev->stride != img->stride ||
                    prev->w != img->w || prev->h != img->h)
                same_pixels = false;
            else if (prev->dst_x != img->dst_x || prev->dst_y != img->dst_y ||
                     prev->color != img->color)
                same_place = false;
        }
        n++;
    }

    if (same_pixels && n == st->n_images) {
        if (!same_place)
            update_placement(st, images);
        st->result.pixels_changed = 0;
        st->result.instances_changed = !same_place;
        set_frame(priv, st, images);
        return &st->result;
    }

    if (!n) {
        st->n_images = 0;
        pack_empty(st);
    } else {
        st->valid = false;
        if (!pack_full(st, images, n, max_size, padding, max_pages))
            goto fail;
    }
    st->valid = true;
    st->max_size = max_size;
    st->padding = padding;
    set_frame(priv, st, own_frame ? images : NULL);
    return &st->result;

fail:
    st->valid = false;
    st->n_images = 0;
    // Keep a frame that only this reference keeps alive (the previously
    // packed one, passed again after a newer ass_render_frame): the caller
    // may fall back to drawing it. It is not compared while !valid.
    set_frame(priv, st, images == st->frame ? images : NULL);
    st->result = (ASS_Atlas) { .pixels_changed = 1, .instances_changed = 1 };
    return NULL;
}
