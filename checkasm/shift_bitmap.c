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

#include "ass_compat.h"
#include "ass_utils.h"

#include "checkasm.h"

#include <string.h>

#define MAX_HEIGHT 8
#define MAX_WIDTH 64
#define MIN_WIDTH 1
#define MAX_EXTRA_BLOCKS 2
#define MAX_STRIDE (MAX_WIDTH + MAX_EXTRA_BLOCKS * 32)

static void check_shift_bitmap(BitmapShiftFunc func, const char *name, int align)
{
    ALIGN(uint8_t buf_ref[MAX_STRIDE * MAX_HEIGHT], 32);
    ALIGN(uint8_t buf_new[MAX_STRIDE * MAX_HEIGHT], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride,
                 size_t width, size_t height, int shift);

    if (check_func(func, name)) {
        for (int i = 0; i < (MAX_WIDTH - MIN_WIDTH + 1) * MAX_HEIGHT; i++) {
            int w = MIN_WIDTH + i / MAX_HEIGHT, h = 1 + i % MAX_HEIGHT;
            // from no padding at all up to a few extra aligned blocks
            ptrdiff_t stride = ass_align(align, w) + rnd() % (MAX_EXTRA_BLOCKS + 1) * align;
            int shift = rnd() % 64;
            for (int j = 0; j < sizeof(buf_ref); j++)
                buf_ref[j] = buf_new[j] = rnd();

            call_ref(buf_ref, stride, w, h, shift);
            call_new(buf_new, stride, w, h, shift);

            if (memcmp(buf_ref, buf_new, sizeof(buf_ref))) {
                fail();
                break;
            }
        }

        bench_new(buf_new, MAX_WIDTH, MAX_WIDTH, MAX_HEIGHT, 37);
    }

    report(name);
}

void checkasm_check_shift_bitmap(unsigned cpu_flag)
{
    BitmapEngine engine = ass_bitmap_engine_init(cpu_flag);
    int align = 1 << engine.align_order;
    check_shift_bitmap(engine.shift_horz, "shift_horz", align);
    check_shift_bitmap(engine.shift_vert, "shift_vert", align);
}
