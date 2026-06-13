/*
 * Copyright (C) 2021-2022 libass contributors
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

#include <stdbool.h>

#include "ass_bitmap_engine.h"
#include "x86/cpuid.h"

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>

/*
 * WebAssembly SIMD128 implementations of the bitmap blend kernels.
 *
 * libass ships hand-written SSE2/AVX2/NEON for these, but none of it is built
 * for wasm32, so the scalar `_c` path runs on every glyph composite. These
 * are bit-exact ports of the `_c` functions in c/c_blend_bitmaps.c: each
 * processes 16 bytes per iteration with a scalar tail for width % 16.
 */

void ass_add_bitmaps_wasm(uint8_t *restrict dst, ptrdiff_t dst_stride,
                          const uint8_t *restrict src, ptrdiff_t src_stride,
                          size_t width, size_t height)
{
    size_t w16 = width & ~(size_t) 15;
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        size_t x = 0;
        for (; x < w16; x += 16) {
            // saturating 8-bit add == FFMIN(dst+src, 255)
            v128_t d = wasm_v128_load(dst + x);
            v128_t s = wasm_v128_load(src + x);
            wasm_v128_store(dst + x, wasm_u8x16_add_sat(d, s));
        }
        for (; x < width; x++) {
            unsigned out = dst[x] + src[x];
            dst[x] = out < 255 ? out : 255;
        }
        dst += dst_stride;
        src += src_stride;
    }
}

// Shared helper: (a * b + 255) >> 8 over 16 bytes, narrowed back to u8.
static inline v128_t mul255_round(v128_t a, v128_t b)
{
    const v128_t c255 = wasm_i16x8_splat(255);
    v128_t lo = wasm_u16x8_extmul_low_u8x16(a, b);
    v128_t hi = wasm_u16x8_extmul_high_u8x16(a, b);
    lo = wasm_u16x8_shr(wasm_i16x8_add(lo, c255), 8);
    hi = wasm_u16x8_shr(wasm_i16x8_add(hi, c255), 8);
    return wasm_u8x16_narrow_i16x8(lo, hi);
}

void ass_imul_bitmaps_wasm(uint8_t *restrict dst, ptrdiff_t dst_stride,
                           const uint8_t *restrict src, ptrdiff_t src_stride,
                           size_t width, size_t height)
{
    const v128_t c255 = wasm_u8x16_splat(255);
    size_t w16 = width & ~(size_t) 15;
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        size_t x = 0;
        for (; x < w16; x += 16) {
            // dst = (dst * (255 - src) + 255) >> 8
            v128_t d = wasm_v128_load(dst + x);
            v128_t inv = wasm_u8x16_sub_sat(c255, wasm_v128_load(src + x));
            wasm_v128_store(dst + x, mul255_round(d, inv));
        }
        for (; x < width; x++)
            dst[x] = (dst[x] * (255 - src[x]) + 255) >> 8;
        dst += dst_stride;
        src += src_stride;
    }
}

void ass_mul_bitmaps_wasm(uint8_t *restrict dst, ptrdiff_t dst_stride,
                          const uint8_t *restrict src1, ptrdiff_t src1_stride,
                          const uint8_t *restrict src2, ptrdiff_t src2_stride,
                          size_t width, size_t height)
{
    size_t w16 = width & ~(size_t) 15;
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        size_t x = 0;
        for (; x < w16; x += 16) {
            // dst = (src1 * src2 + 255) >> 8
            v128_t a = wasm_v128_load(src1 + x);
            v128_t b = wasm_v128_load(src2 + x);
            wasm_v128_store(dst + x, mul255_round(a, b));
        }
        for (; x < width; x++)
            dst[x] = (src1[x] * src2[x] + 255) >> 8;
        dst  += dst_stride;
        src1 += src1_stride;
        src2 += src2_stride;
    }
}

/*
 * Gaussian blur kernels (alignment 16 / STRIPE_WIDTH 8 variant only, which is
 * what ass_render selects without ASS_FLAG_WIDE_STRIPE).
 *
 * Only the "vertical" class is ported here: these operate on whole 8-wide
 * stripes at a fixed lane offset, so they vectorize to one i16x8 (or a pair of
 * i32x4 where intermediates exceed int16) with no cross-lane shuffles. They
 * are bit-exact ports of the corresponding `_c` template instantiations and
 * are validated byte-for-byte against them by build/simd_fuzz.c. The
 * horizontal kernels need shifted/interleaved access and stay scalar C.
 */
#define SW 8  // STRIPE_WIDTH for alignment 16

static int16_t zero_line_w[SW];

static inline const int16_t *get_line_w(const int16_t *ptr, size_t offs, size_t size)
{
    return offs < size ? ptr + offs : zero_line_w;
}

void ass_stripe_unpack16_wasm(int16_t *restrict dst, const uint8_t *restrict src,
                              ptrdiff_t src_stride, size_t width, size_t height)
{
    const v128_t one = wasm_i16x8_splat(1);
    for (size_t y = 0; y < height; y++) {
        int16_t *ptr = dst;
        for (size_t x = 0; x < width; x += SW) {
            // ptr[k] = (uint16_t)(((src<<7)|(src>>1)) + 1) >> 1
            v128_t s = wasm_u16x8_extend_low_u8x16(wasm_v128_load64_zero(src + x));
            v128_t v = wasm_v128_or(wasm_i16x8_shl(s, 7), wasm_u16x8_shr(s, 1));
            v = wasm_u16x8_shr(wasm_i16x8_add(v, one), 1);
            wasm_v128_store(ptr, v);
            ptr += SW * height;
        }
        dst += SW;
        src += src_stride;
    }
}

void ass_stripe_pack16_wasm(uint8_t *restrict dst, ptrdiff_t dst_stride,
                            const int16_t *restrict src, size_t width, size_t height)
{
    const v128_t dither_even = wasm_i16x8_make(8, 40, 8, 40, 8, 40, 8, 40);
    const v128_t dither_odd  = wasm_i16x8_make(56, 24, 56, 24, 56, 24, 56, 24);
    for (size_t x = 0; x < width; x += SW) {
        uint8_t *ptr = dst;
        for (size_t y = 0; y < height; y++) {
            // ptr[k] = (uint16_t)(src - (src>>8) + dither) >> 6
            v128_t s = wasm_v128_load(src);
            v128_t v = wasm_i16x8_sub(s, wasm_i16x8_shr(s, 8));
            v = wasm_i16x8_add(v, (y & 1) ? dither_odd : dither_even);
            v = wasm_u16x8_shr(v, 6);
            wasm_v128_store64_lane(ptr, wasm_u8x16_narrow_i16x8(v, v), 0);
            ptr += dst_stride;
            src += SW;
        }
        dst += SW;
    }
    size_t left = dst_stride - ((width + SW - 1) & ~(size_t)(SW - 1));
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < left; x++)
            dst[x] = 0;
        dst += dst_stride;
    }
}

// shrink_func over an i32x4 lane group (intermediates exceed int16 range)
static inline v128_t shrink4(v128_t p1p, v128_t p1n, v128_t z0p,
                             v128_t z0n, v128_t n1p, v128_t n1n)
{
    v128_t r = wasm_i32x4_shr(wasm_i32x4_add(wasm_i32x4_add(p1p, p1n),
                                             wasm_i32x4_add(n1p, n1n)), 1);
    r = wasm_i32x4_shr(wasm_i32x4_add(wasm_i32x4_add(r, z0p), z0n), 1);
    r = wasm_i32x4_shr(wasm_i32x4_add(wasm_i32x4_add(r, p1n), n1p), 1);
    r = wasm_i32x4_add(wasm_i32x4_add(wasm_i32x4_add(r, z0p), z0n),
                       wasm_i32x4_splat(2));
    return wasm_i32x4_shr(r, 2);
}

void ass_shrink_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                            size_t src_width, size_t src_height)
{
    size_t dst_height = (src_height + 5) >> 1;
    size_t step = SW * src_height;
    for (size_t x = 0; x < src_width; x += SW) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y++) {
            v128_t p1p = wasm_v128_load(get_line_w(src, offs - 4 * SW, step));
            v128_t p1n = wasm_v128_load(get_line_w(src, offs - 3 * SW, step));
            v128_t z0p = wasm_v128_load(get_line_w(src, offs - 2 * SW, step));
            v128_t z0n = wasm_v128_load(get_line_w(src, offs - 1 * SW, step));
            v128_t n1p = wasm_v128_load(get_line_w(src, offs - 0 * SW, step));
            v128_t n1n = wasm_v128_load(get_line_w(src, offs + 1 * SW, step));
            v128_t lo = shrink4(wasm_i32x4_extend_low_i16x8(p1p), wasm_i32x4_extend_low_i16x8(p1n),
                                wasm_i32x4_extend_low_i16x8(z0p), wasm_i32x4_extend_low_i16x8(z0n),
                                wasm_i32x4_extend_low_i16x8(n1p), wasm_i32x4_extend_low_i16x8(n1n));
            v128_t hi = shrink4(wasm_i32x4_extend_high_i16x8(p1p), wasm_i32x4_extend_high_i16x8(p1n),
                                wasm_i32x4_extend_high_i16x8(z0p), wasm_i32x4_extend_high_i16x8(z0n),
                                wasm_i32x4_extend_high_i16x8(n1p), wasm_i32x4_extend_high_i16x8(n1n));
            wasm_v128_store(dst, wasm_i16x8_narrow_i32x4(lo, hi));
            dst  += SW;
            offs += 2 * SW;
        }
        src += step;
    }
}

void ass_expand_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                            size_t src_width, size_t src_height)
{
    const v128_t one = wasm_i16x8_splat(1);
    size_t dst_height = 2 * src_height + 4;
    size_t step = SW * src_height;
    for (size_t x = 0; x < src_width; x += SW) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y += 2) {
            v128_t p1 = wasm_v128_load(get_line_w(src, offs - 2 * SW, step));
            v128_t z0 = wasm_v128_load(get_line_w(src, offs - 1 * SW, step));
            v128_t n1 = wasm_v128_load(get_line_w(src, offs - 0 * SW, step));
            // expand_func: all u16 arithmetic, values stay < 0x8000
            v128_t r = wasm_u16x8_shr(wasm_i16x8_add(wasm_u16x8_shr(wasm_i16x8_add(p1, n1), 1), z0), 1);
            v128_t rp = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(wasm_u16x8_shr(wasm_i16x8_add(r, p1), 1), z0), one), 1);
            v128_t rn = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(wasm_u16x8_shr(wasm_i16x8_add(r, n1), 1), z0), one), 1);
            wasm_v128_store(dst, rp);
            wasm_v128_store(dst + SW, rn);
            dst  += 2 * SW;
            offs += 1 * SW;
        }
        src += step;
    }
}

static inline void blur_vert_wasm(int16_t *restrict dst, const int16_t *restrict src,
                                  size_t src_width, size_t src_height,
                                  const int16_t *restrict param, const int n)
{
    size_t dst_height = src_height + 2 * n;
    size_t step = SW * src_height;
    for (size_t x = 0; x < src_width; x += SW) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y++) {
            v128_t acc_lo = wasm_i32x4_splat(0x8000);
            v128_t acc_hi = acc_lo;
            v128_t center = wasm_v128_load(get_line_w(src, offs - n * SW, step));
            for (int i = n; i > 0; i--) {
                v128_t l1 = wasm_v128_load(get_line_w(src, offs - (n + i) * SW, step));
                v128_t l2 = wasm_v128_load(get_line_w(src, offs - (n - i) * SW, step));
                v128_t pv = wasm_i16x8_splat(param[i - 1]);
                v128_t d1 = wasm_i16x8_sub(l1, center);
                v128_t d2 = wasm_i16x8_sub(l2, center);
                acc_lo = wasm_i32x4_add(acc_lo, wasm_i32x4_add(
                             wasm_i32x4_extmul_low_i16x8(d1, pv),
                             wasm_i32x4_extmul_low_i16x8(d2, pv)));
                acc_hi = wasm_i32x4_add(acc_hi, wasm_i32x4_add(
                             wasm_i32x4_extmul_high_i16x8(d1, pv),
                             wasm_i32x4_extmul_high_i16x8(d2, pv)));
            }
            // dst[k] = center[k] + (acc[k] >> 16)
            v128_t lo = wasm_i32x4_add(wasm_i32x4_extend_low_i16x8(center), wasm_i32x4_shr(acc_lo, 16));
            v128_t hi = wasm_i32x4_add(wasm_i32x4_extend_high_i16x8(center), wasm_i32x4_shr(acc_hi, 16));
            wasm_v128_store(dst, wasm_i16x8_narrow_i32x4(lo, hi));
            dst  += SW;
            offs += SW;
        }
        src += step;
    }
}

void ass_blur4_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                           size_t w, size_t h, const int16_t *restrict param)
{ blur_vert_wasm(dst, src, w, h, param, 4); }
void ass_blur5_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                           size_t w, size_t h, const int16_t *restrict param)
{ blur_vert_wasm(dst, src, w, h, param, 5); }
void ass_blur6_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                           size_t w, size_t h, const int16_t *restrict param)
{ blur_vert_wasm(dst, src, w, h, param, 6); }
void ass_blur7_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                           size_t w, size_t h, const int16_t *restrict param)
{ blur_vert_wasm(dst, src, w, h, param, 7); }
void ass_blur8_vert16_wasm(int16_t *restrict dst, const int16_t *restrict src,
                           size_t w, size_t h, const int16_t *restrict param)
{ blur_vert_wasm(dst, src, w, h, param, 8); }
#undef SW
#endif


#define RASTERIZER_PROTOTYPES(tile_size, suffix) \
    FillSolidTileFunc     ass_fill_solid_tile     ## tile_size ## _ ## suffix; \
    FillHalfplaneTileFunc ass_fill_halfplane_tile ## tile_size ## _ ## suffix; \
    FillGenericTileFunc   ass_fill_generic_tile   ## tile_size ## _ ## suffix; \
    MergeTileFunc         ass_merge_tile          ## tile_size ## _ ## suffix;

#define RASTERIZER_FUNCTION(name, suffix) \
    engine.name = mask & ASS_FLAG_LARGE_TILES ? \
        ass_ ## name ## _tile32_ ## suffix : \
        ass_ ## name ## _tile16_ ## suffix;

#define RASTERIZER_FUNCTIONS(suffix) \
    RASTERIZER_FUNCTION(fill_solid,     suffix) \
    RASTERIZER_FUNCTION(fill_halfplane, suffix) \
    RASTERIZER_FUNCTION(fill_generic,   suffix) \
    RASTERIZER_FUNCTION(merge,          suffix)


#define GENERIC_PROTOTYPES(suffix) \
    BitmapBlendFunc ass_add_bitmaps_  ## suffix; \
    BitmapBlendFunc ass_imul_bitmaps_ ## suffix; \
    BitmapMulFunc   ass_mul_bitmaps_  ## suffix; \
    BeBlurFunc      ass_be_blur_      ## suffix;

#define GENERIC_FUNCTION(name, suffix) \
    engine.name = ass_ ## name ## _ ## suffix;

#define GENERIC_FUNCTIONS(suffix) \
    GENERIC_FUNCTION(add_bitmaps,  suffix) \
    GENERIC_FUNCTION(imul_bitmaps, suffix) \
    GENERIC_FUNCTION(mul_bitmaps,  suffix) \
    GENERIC_FUNCTION(be_blur,      suffix)


#define PARAM_BLUR_SET(suffix) \
    ass_blur4_ ## suffix, \
    ass_blur5_ ## suffix, \
    ass_blur6_ ## suffix, \
    ass_blur7_ ## suffix, \
    ass_blur8_ ## suffix

#define BLUR_PROTOTYPES(stripe_width, suffix) \
    Convert8to16Func ass_stripe_unpack  ## stripe_width ## _ ## suffix; \
    Convert16to8Func ass_stripe_pack    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_shrink_horz    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_shrink_vert    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_expand_horz    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_expand_vert    ## stripe_width ## _ ## suffix; \
    ParamFilterFunc PARAM_BLUR_SET(horz ## stripe_width ## _ ## suffix); \
    ParamFilterFunc PARAM_BLUR_SET(vert ## stripe_width ## _ ## suffix);

#define BLUR_FUNCTION(name, alignment, suffix) \
    engine.name = ass_ ## name ## alignment ## _ ## suffix;

#define PARAM_BLUR_FUNCTION(dir, alignment, suffix) \
    engine.blur_ ## dir[0] = ass_blur4_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[1] = ass_blur5_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[2] = ass_blur6_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[3] = ass_blur7_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[4] = ass_blur8_ ## dir ## alignment ## _ ## suffix;

#define BLUR_FUNCTIONS(align_order_, alignment, suffix) \
    BLUR_FUNCTION(stripe_unpack, alignment, suffix) \
    BLUR_FUNCTION(stripe_pack,   alignment, suffix) \
    BLUR_FUNCTION(shrink_horz,   alignment, suffix) \
    BLUR_FUNCTION(shrink_vert,   alignment, suffix) \
    BLUR_FUNCTION(expand_horz,   alignment, suffix) \
    BLUR_FUNCTION(expand_vert,   alignment, suffix) \
    PARAM_BLUR_FUNCTION(horz, alignment, suffix) \
    PARAM_BLUR_FUNCTION(vert, alignment, suffix) \
    engine.align_order = align_order_;


#define ALL_PROTOTYPES(alignment, suffix) \
    RASTERIZER_PROTOTYPES(16, suffix) \
    RASTERIZER_PROTOTYPES(32, suffix) \
    GENERIC_PROTOTYPES(suffix) \
    BLUR_PROTOTYPES(alignment, suffix)

#define ALL_FUNCTIONS(align_order_, alignment, suffix) \
    RASTERIZER_FUNCTIONS(suffix) \
    GENERIC_FUNCTIONS(suffix) \
    BLUR_FUNCTIONS(align_order_, alignment, suffix)


unsigned ass_get_cpu_flags(unsigned mask)
{
    unsigned flags = ASS_CPU_FLAG_NONE;

#if CONFIG_ASM && ARCH_X86

    if (!ass_has_cpuid())
        return flags & mask;

    uint32_t eax = 0, ebx, ecx, edx;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    bool avx = false;
    if (max_leaf >= 1) {
        eax = 1;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);
        if (edx & (1 << 26)) {  // SSE2
            flags |= ASS_CPU_FLAG_X86_SSE2;
            if (ecx & (1 << 0) &&  // SSE3
                ecx & (1 << 9))    // SSSE3
                    flags |= ASS_CPU_FLAG_X86_SSSE3;
        }

        if (ecx & (1 << 27) &&  // OSXSAVE
            ecx & (1 << 28)) {  // AVX
            uint32_t xcr0l, xcr0h;
            ass_get_xgetbv(0, &xcr0l, &xcr0h);
            if (xcr0l & (1 << 1) &&  // XSAVE for XMM
                xcr0l & (1 << 2))    // XSAVE for YMM
                    avx = true;
        }
    }

    if (max_leaf >= 7) {
        eax = 7;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);
        if (avx && ebx & (1 << 5))  // AVX2
            flags |= ASS_CPU_FLAG_X86_AVX2;
    }

#endif

#if ARCH_AARCH64
    flags = ASS_CPU_FLAG_ARM_NEON;
#endif

    return flags & mask;
}

BitmapEngine ass_bitmap_engine_init(unsigned mask)
{
    ALL_PROTOTYPES(16, c)
    BLUR_PROTOTYPES(32, c)
    BitmapEngine engine = {0};
    engine.tile_order = mask & ASS_FLAG_LARGE_TILES ? 5 : 4;

#if CONFIG_ASM
    unsigned flags = ass_get_cpu_flags(mask);
#if ARCH_X86
    if (flags & ASS_CPU_FLAG_X86_AVX2) {
        ALL_PROTOTYPES(32, avx2)
        ALL_FUNCTIONS(5, 32, avx2)
        return engine;
    } else if (flags & ASS_CPU_FLAG_X86_SSE2) {
        ALL_PROTOTYPES(16, sse2)
        ALL_FUNCTIONS(4, 16, sse2)
        if (flags & ASS_CPU_FLAG_X86_SSSE3) {
            ALL_PROTOTYPES(16, ssse3)
            RASTERIZER_FUNCTION(fill_generic, ssse3)
            GENERIC_FUNCTION(be_blur, ssse3)
            BLUR_FUNCTION(shrink_horz, 16, ssse3)
            BLUR_FUNCTION(expand_horz, 16, ssse3)
            PARAM_BLUR_FUNCTION(horz, 16, ssse3)
        }
        return engine;
    }
#elif ARCH_AARCH64
    if (flags & ASS_CPU_FLAG_ARM_NEON) {
        ALL_PROTOTYPES(16, neon)
        ALL_FUNCTIONS(4, 16, neon)
        return engine;
    }
#endif
#endif

    ALL_FUNCTIONS(4, 16, c)
    if (mask & ASS_FLAG_WIDE_STRIPE) {
        BLUR_FUNCTIONS(5, 32, c)
    }

#if defined(__wasm_simd128__)
    engine.add_bitmaps  = ass_add_bitmaps_wasm;
    engine.imul_bitmaps = ass_imul_bitmaps_wasm;
    engine.mul_bitmaps  = ass_mul_bitmaps_wasm;
    if (!(mask & ASS_FLAG_WIDE_STRIPE)) {
        engine.stripe_unpack = ass_stripe_unpack16_wasm;
        engine.stripe_pack   = ass_stripe_pack16_wasm;
        engine.shrink_vert   = ass_shrink_vert16_wasm;
        engine.expand_vert   = ass_expand_vert16_wasm;
        engine.blur_vert[0]  = ass_blur4_vert16_wasm;
        engine.blur_vert[1]  = ass_blur5_vert16_wasm;
        engine.blur_vert[2]  = ass_blur6_vert16_wasm;
        engine.blur_vert[3]  = ass_blur7_vert16_wasm;
        engine.blur_vert[4]  = ass_blur8_vert16_wasm;
    }
#endif

    return engine;
}
