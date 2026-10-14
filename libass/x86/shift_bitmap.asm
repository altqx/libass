;******************************************************************************
;* shift_bitmap.asm: SSE2 and AVX2 subpixel bitmap shift
;******************************************************************************
;* Copyright (C) 2026 libass contributors
;*
;* This file is part of libass.
;*
;* Permission to use, copy, modify, and distribute this software for any
;* purpose with or without fee is hereby granted, provided that the above
;* copyright notice and this permission notice appear in all copies.
;*
;* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
;* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
;* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
;* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
;* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
;* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
;* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;******************************************************************************

%include "x86/utils.asm"

SECTION_RODATA 32

%if ARCH_X86_64 || !PIC
times 32 db 0xFF
edge_mask: times 32 db 0x00
%endif

SECTION .text

;------------------------------------------------------------------------------
; LOAD_EDGE_MASK 1:m_dst, 2:n, 3:tmp
; Set n last bytes of xmm/ymm register to zero and other bytes to 255
;------------------------------------------------------------------------------

%macro LOAD_EDGE_MASK 3
%if !PIC
    movu m%1, [edge_mask + %2 - mmsize]
%elif ARCH_X86_64
    lea %3, [rel edge_mask]
    movu m%1, [%3 + %2 - mmsize]
%elif mmsize <= STACK_ALIGNMENT
    %assign %%pad -(stack_offset + gprsize) & (mmsize - 1)
    pxor m%1, m%1
    mova [rsp - %%pad - mmsize], m%1
    pcmpeqb m%1, m%1
    mova [rsp - %%pad - 2 * mmsize], m%1
    movu m%1, [rsp + %2 - %%pad - 2 * mmsize]
%else
    mov %3, rsp
    and %3, -mmsize
    pxor m%1, m%1
    mova [%3 - mmsize], m%1
    pcmpeqb m%1, m%1
    mova [%3 - 2 * mmsize], m%1
    movu m%1, [%3 + %2 - 2 * mmsize]
%endif
%endmacro

;------------------------------------------------------------------------------
; SHIFT_PART 1:m_dst, 2:m_src, 3:m_tmp, 4:m_zero, 5:m_mul
; dst = src * shift >> 6 for every byte, with mul = shift << 10 in every word
;------------------------------------------------------------------------------

%macro SHIFT_PART 5
    punpckhbw m%3, m%2, m%4
    punpcklbw m%1, m%2, m%4
    pmulhuw m%3, m%5
    pmulhuw m%1, m%5
    packuswb m%1, m%3
%endmacro

;------------------------------------------------------------------------------
; SHIFT_HORZ_BLOCK
; m0 += m1 moved up by one byte (with the last byte of m6 in front) - m1,
; destroys m2 and m7
;------------------------------------------------------------------------------

%macro SHIFT_HORZ_BLOCK 0
%if mmsize == 32
    vperm2i128 m7, m6, m1, 0x21
    palignr m2, m1, m7, 15
%else
    pslldq m2, m1, 1
    psrldq m7, m6, 15
    por m2, m7
%endif
    psubb m0, m1
    paddb m0, m2
%endmacro

;------------------------------------------------------------------------------
; SHIFT_HORZ
; void shift_horz(uint8_t *buf, ptrdiff_t stride,
;                 size_t width, size_t height, int shift);
;------------------------------------------------------------------------------

%macro SHIFT_HORZ 0
cglobal shift_horz, 5,7,8
    shl r4d, 10
    BCASTW 4, r4d
    pxor m3, m3
    lea r0, [r0 + r2]
    neg r2
    ; the last pixel of a row moves nothing to the right
    mov r4, r2
    and r4, mmsize - 1
    add r4, 1
    LOAD_EDGE_MASK 5, r4, r6
    jmp .row_entry

.width_loop:
    SHIFT_PART 1, 0, 2, 3, 4
    SHIFT_HORZ_BLOCK
    mova m6, m1
    mova [r0 + r4 - mmsize], m0
.loop_entry:
    mova m0, [r0 + r4]
    add r4, mmsize
    jnc .width_loop
    SHIFT_PART 1, 0, 2, 3, 4
    pand m1, m5
    SHIFT_HORZ_BLOCK
    mova [r0 + r4 - mmsize], m0
    add r0, r1
.row_entry:
    pxor m6, m6
    mov r4, r2
    sub r3, 1
    jnc .loop_entry
    RET
%endmacro

INIT_XMM sse2
SHIFT_HORZ
INIT_YMM avx2
SHIFT_HORZ

;------------------------------------------------------------------------------
; SHIFT_VERT
; void shift_vert(uint8_t *buf, ptrdiff_t stride,
;                 size_t width, size_t height, int shift);
;------------------------------------------------------------------------------

%macro SHIFT_VERT 0
cglobal shift_vert, 5,7,8
    shl r4d, 10
    BCASTW 4, r4d
    pxor m3, m3
    ; process the bitmap in vertical stripes, from the right one,
    ; which is the only one that can be incomplete
    lea r4, [r2 - 1]
    and r4, ~(mmsize - 1)
    neg r2
    and r2, mmsize - 1
    LOAD_EDGE_MASK 5, r2, r6
    sub r3, 1
    jz .end
    mov r2, r3
    imul r2, r1
    add r0, r4

    ; walk up from the bottom row keeping the original pixels (m0)
    ; and the amount they lose (m6) of the current row
.stripe_loop:
    lea r5, [r0 + r2]
    mova m0, [r5]
    pxor m6, m6
    mov r6, r3
.height_loop:
    sub r5, r1
    mova m1, [r5]
    SHIFT_PART 7, 1, 2, 3, 4
    pand m7, m5
    psubb m0, m6
    paddb m0, m7
    mova [r5 + r1], m0
    mova m0, m1
    mova m6, m7
    sub r6, 1
    jnz .height_loop
    psubb m0, m6
    mova [r5], m0
    pcmpeqb m5, m5
    sub r0, mmsize
    sub r4, mmsize
    jnc .stripe_loop
.end:
    RET
%endmacro

INIT_XMM sse2
SHIFT_VERT
INIT_YMM avx2
SHIFT_VERT
