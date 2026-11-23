/* Internal CPU kernels for the OCR and FormulaNet DLLs (Windows x64). */
#include "asm.inc"

/* One k step: reuse eight weights across four or eight positions. */
.macro CONV_POSITIONS base, x, c0, c1, c2, c3
        vbroadcastss ymm5, dword ptr [\base + \x]
        vfmadd231ps \c0, ymm4, ymm5
        vbroadcastss ymm5, dword ptr [\base + \x + 4]
        vfmadd231ps \c1, ymm4, ymm5
        vbroadcastss ymm5, dword ptr [\base + \x + 8]
        vfmadd231ps \c2, ymm4, ymm5
        vbroadcastss ymm5, dword ptr [\base + \x + 12]
        vfmadd231ps \c3, ymm4, ymm5
.endm

.macro CONV_STEP w, x, pair
        vmovups ymm4, ymmword ptr [rcx + \w]
        CONV_POSITIONS rdx, \x, ymm0, ymm1, ymm2, ymm3
.if \pair
        CONV_POSITIONS rdx+r10, \x, ymm6, ymm7, ymm8, ymm9
.endif
.endm

/* Weights[k][8], one or two consecutive position blocks [k][4],
 * output [4][8] or [8][8]. Each output lane keeps its original k order. */
.macro CONV_MK name, pair
FUNC \name
.if \pair
        ALLOC 72
        SAVEXMM xmm6, 0
        SAVEXMM xmm7, 16
        SAVEXMM xmm8, 32
        SAVEXMM xmm9, 48
.endif
        ENDPROLOG
        vxorps xmm0, xmm0, xmm0
        vxorps xmm1, xmm1, xmm1
        vxorps xmm2, xmm2, xmm2
        vxorps xmm3, xmm3, xmm3
.if \pair
        mov r10d, r8d
        shl r10, 4                    /* offset of the second position block */
        vxorps xmm6, xmm6, xmm6
        vxorps xmm7, xmm7, xmm7
        vxorps xmm8, xmm8, xmm8
        vxorps xmm9, xmm9, xmm9
.endif
        mov eax, r8d
        shr eax, 2
        jz .Lconv_tail\@
        .p2align 4
.Lconv_four\@:
        CONV_STEP 0, 0, \pair
        CONV_STEP 32, 16, \pair
        CONV_STEP 64, 32, \pair
        CONV_STEP 96, 48, \pair
        add rcx, 128
        add rdx, 64
        dec eax
        jnz .Lconv_four\@
.Lconv_tail\@:
        and r8d, 3
        jz .Lconv_store\@
.Lconv_one\@:
        CONV_STEP 0, 0, \pair
        add rcx, 32
        add rdx, 16
        dec r8d
        jnz .Lconv_one\@
.Lconv_store\@:
        vmovups ymmword ptr [r9], ymm0
        vmovups ymmword ptr [r9 + 32], ymm1
        vmovups ymmword ptr [r9 + 64], ymm2
        vmovups ymmword ptr [r9 + 96], ymm3
.if \pair
        vmovups ymmword ptr [r9 + 128], ymm6
        vmovups ymmword ptr [r9 + 160], ymm7
        vmovups ymmword ptr [r9 + 192], ymm8
        vmovups ymmword ptr [r9 + 224], ymm9
        vmovaps xmm6, xmmword ptr [rsp]
        vmovaps xmm7, xmmword ptr [rsp + 16]
        vmovaps xmm8, xmmword ptr [rsp + 32]
        vmovaps xmm9, xmmword ptr [rsp + 48]
.endif
        vzeroupper
.if \pair
        add rsp, 72
.endif
        ret
ENDF
.endm

CONV_MK sniplite_conv2d_mk_block8, 0
CONV_MK sniplite_conv2d_mk_block8_pair, 1

/* Each function writes one output plane. Stride 2 and a 2x2 kernel mean
 * the blocks from adjacent input pixels never overlap. Keep both output
 * rows of a block in registers and walk input channels in ascending order.
 * The SSE2 and FMA variants match the contraction rules of their DLL builds.
 * Arguments: input, weights for output channel, output, input width,
 *            floats per input plane, floats between input-channel weights,
 *            input channel count. Width, plane size and channel count > 0. */
.macro DECONV2 name, fused, pixels
FUNC \name
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG r12
        PUSHREG r13
        PUSHREG r14
        PUSHREG r15
        ENDPROLOG
        mov r9d, r9d
        lea r12, [r9 * 8]             /* output row stride in bytes */
        mov r13, qword ptr [rsp + 88]
        shl r13, 2                    /* input plane stride in bytes */
        mov r14, qword ptr [rsp + 96]
        shl r14, 2                    /* weight stride in bytes */
        lea r15, [rcx + r13]          /* end of first input plane */
        mov esi, dword ptr [rsp + 104]
.Ldeconv_row\@:
        mov ebx, r9d
        cmp ebx, \pixels
        jb .Ldeconv_tail\@
.Ldeconv_block\@:
        mov r10, rcx
        mov r11, rdx
        mov eax, esi
.if \fused
        vxorps xmm0, xmm0, xmm0
        vxorps xmm1, xmm1, xmm1
.else
        xorps xmm0, xmm0
        xorps xmm1, xmm1
.endif
.Ldeconv_channels\@:
.if \fused
        vmovups xmm2, xmmword ptr [r10]
        vpermilps xmm3, xmm2, 0xfa
        vpermilps xmm2, xmm2, 0x50
        vinsertf128 ymm2, ymm2, xmm3, 1 /* x0 x0 x1 x1 x2 x2 x3 x3 */
        vbroadcastsd ymm3, qword ptr [r11]
        vfmadd231ps ymm0, ymm2, ymm3
        vbroadcastsd ymm3, qword ptr [r11 + 8]
        vfmadd231ps ymm1, ymm2, ymm3
.else
        movq xmm2, qword ptr [r10]
        unpcklps xmm2, xmm2           /* x0 x0 x1 x1 */
        movq xmm3, qword ptr [r11]
        movlhps xmm3, xmm3            /* w00 w01 w00 w01 */
        mulps xmm3, xmm2
        addps xmm0, xmm3
        movq xmm3, qword ptr [r11 + 8]
        movlhps xmm3, xmm3
        mulps xmm3, xmm2
        addps xmm1, xmm3
.endif
        add r10, r13
        add r11, r14
        dec eax
        jnz .Ldeconv_channels\@
.if \fused
        vmovups ymmword ptr [r8], ymm0
        vmovups ymmword ptr [r8 + r12], ymm1
.else
        movups xmmword ptr [r8], xmm0
        movups xmmword ptr [r8 + r12], xmm1
.endif
        add rcx, 4 * \pixels
        add r8, 8 * \pixels
        sub ebx, \pixels
        cmp ebx, \pixels
        jae .Ldeconv_block\@
.Ldeconv_tail\@:
        test ebx, ebx
        jz .Ldeconv_next_row\@
.Ldeconv_pixel\@:
        mov r10, rcx
        mov r11, rdx
        mov eax, esi
.if \fused
        vxorps xmm0, xmm0, xmm0
.else
        xorps xmm0, xmm0
.endif
.Ldeconv_tail_channels\@:
        /* One pixel: lanes are w00, w01, w10, w11, split on store. */
.if \fused
        vbroadcastss xmm2, dword ptr [r10]
        vfmadd231ps xmm0, xmm2, xmmword ptr [r11]
.else
        movss xmm2, dword ptr [r10]
        shufps xmm2, xmm2, 0
        movups xmm3, xmmword ptr [r11]
        mulps xmm2, xmm3
        addps xmm0, xmm2
.endif
        add r10, r13
        add r11, r14
        dec eax
        jnz .Ldeconv_tail_channels\@
.if \fused
        vmovlps qword ptr [r8], xmm0
        vmovhps qword ptr [r8 + r12], xmm0
.else
        movlps qword ptr [r8], xmm0
        movhps qword ptr [r8 + r12], xmm0
.endif
        add rcx, 4
        add r8, 8
        dec ebx
        jnz .Ldeconv_pixel\@
.Ldeconv_next_row\@:
        add r8, r12
        cmp rcx, r15
        jb .Ldeconv_row\@
.if \fused
        vzeroupper
.endif
        pop r15
        pop r14
        pop r13
        pop r12
        pop rsi
        pop rbx
        ret
ENDF
.endm

DECONV2 sniplite_deconv2_sse2, 0, 2
DECONV2 sniplite_deconv2_fma, 1, 4
