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

#include "asm.S"

const edge_mask, align=4
    .dcb.b 16, 0xFF
    .dcb.b 16, 0x00
endconst

/*
 * shift_part dst, src, mul, tmp
 * dst = src * shift >> 6 for every byte, with shift in every byte of mul
 */

.macro shift_part dst, src, mul, tmp
    umull2 \tmp\().8h, \src\().16b, \mul\().16b
    umull \dst\().8h, \src\().8b, \mul\().8b
    shrn \dst\().8b, \dst\().8h, 6
    shrn2 \dst\().16b, \tmp\().8h, 6
.endm

/*
 * void ass_shift_horz(uint8_t *buf, ptrdiff_t stride,
 *                     size_t width, size_t height, int shift);
 */

function shift_horz_neon, export=1
    dup v4.16b, w4
    // the last pixel of a row moves nothing to the right
    neg x6, x2
    and x6, x6, 15
    add x6, x6, 1
    movrel x7, edge_mask
    add x7, x7, x6
    ld1 {v5.16b}, [x7]
0:
    movi v6.16b, 0
    mov x7, x0
    subs x6, x2, 16
    b.ls 2f
1:
    ld1 {v0.16b}, [x7]
    shift_part v1, v0, v4, v2
    ext v2.16b, v6.16b, v1.16b, 15
    sub v0.16b, v0.16b, v1.16b
    add v0.16b, v0.16b, v2.16b
    mov v6.16b, v1.16b
    st1 {v0.16b}, [x7], 16
    subs x6, x6, 16
    b.hi 1b
2:
    ld1 {v0.16b}, [x7]
    shift_part v1, v0, v4, v2
    and v1.16b, v1.16b, v5.16b
    ext v2.16b, v6.16b, v1.16b, 15
    sub v0.16b, v0.16b, v1.16b
    add v0.16b, v0.16b, v2.16b
    st1 {v0.16b}, [x7]
    subs x3, x3, 1
    add x0, x0, x1
    b.ne 0b
    ret
endfunc

/*
 * void ass_shift_vert(uint8_t *buf, ptrdiff_t stride,
 *                     size_t width, size_t height, int shift);
 */

function shift_vert_neon, export=1
    subs x3, x3, 1
    b.eq 9f
    dup v4.16b, w4
    // process the bitmap in vertical stripes, from the right one,
    // which is the only one that can be incomplete
    sub x5, x2, 1
    and x5, x5, ~15
    neg x6, x2
    and x6, x6, 15
    movrel x7, edge_mask
    add x7, x7, x6
    ld1 {v5.16b}, [x7]
    mul x6, x3, x1
    add x0, x0, x5
0:
    // walk up from the bottom row keeping the original pixels (v0)
    // and the amount they lose (v6) of the current row
    add x7, x0, x6
    ld1 {v0.16b}, [x7]
    movi v6.16b, 0
    mov x8, x3
1:
    sub x7, x7, x1
    ld1 {v1.16b}, [x7]
    shift_part v7, v1, v4, v2
    and v7.16b, v7.16b, v5.16b
    sub v0.16b, v0.16b, v6.16b
    add v0.16b, v0.16b, v7.16b
    str q0, [x7, x1]
    mov v0.16b, v1.16b
    mov v6.16b, v7.16b
    subs x8, x8, 1
    b.ne 1b
    sub v0.16b, v0.16b, v6.16b
    st1 {v0.16b}, [x7]
    movi v5.16b, 0xFF
    sub x0, x0, 16
    subs x5, x5, 16
    b.hs 0b
9:
    ret
endfunc
