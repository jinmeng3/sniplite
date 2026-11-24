#include "asm.inc"
#include "ocr_crop_kernels.h"

        .section .rdata,"dr"
        .balign 16
.Lcrop_one:
        .float 1.0, 1.0, 1.0, 1.0
.Lcrop_sign64:
        .quad 0x8000000000000000, 0x8000000000000000
.Lcrop_half64:
        .double 0.5, 0.5

/* The two low lanes carry x/y. Each output coordinate is recomputed from its
 * integer index with divss; no reciprocal approximation or coordinate stepping.
 * Only the three original multiply/add expressions contract in the FMA body.
 * rcx points at sniplite_ocr_crop_params (see its checked layout in the header).
 */
.macro CROP_KERNEL name, fused
FUNC \name
        PUSHREG rbx
        PUSHREG rbp
        PUSHREG rsi
        PUSHREG rdi
        PUSHREG r12
        PUSHREG r13
        PUSHREG r14
        PUSHREG r15
        ALLOC 152
        SAVEXMM xmm6, 0
        SAVEXMM xmm7, 16
        SAVEXMM xmm8, 32
        SAVEXMM xmm9, 48
        SAVEXMM xmm10, 64
        SAVEXMM xmm11, 80
        SAVEXMM xmm12, 96
        SAVEXMM xmm13, 112
        SAVEXMM xmm14, 128
        ENDPROLOG

        mov rbx, rcx
        mov rsi, qword ptr [rbx + SNIPLITE_CROP_SRC]
        mov rdi, qword ptr [rbx + SNIPLITE_CROP_DST]
        mov r12d, dword ptr [rbx + SNIPLITE_CROP_WIDTH]
        mov r13d, dword ptr [rbx + SNIPLITE_CROP_HEIGHT]
        dec r12d
        dec r13d
        mov ebp, dword ptr [rbx + SNIPLITE_CROP_CHANNELS]
        mov r14d, dword ptr [rbx + SNIPLITE_CROP_OUT_WIDTH]
        mov r15d, dword ptr [rbx + SNIPLITE_CROP_OUT_HEIGHT]

        movss xmm6, dword ptr [rbx + SNIPLITE_CROP_X]
        movss xmm0, dword ptr [rbx + SNIPLITE_CROP_Y]
        unpcklps xmm6, xmm0                 /* top origin */
        movss xmm7, dword ptr [rbx + SNIPLITE_CROP_X + 4]
        movss xmm0, dword ptr [rbx + SNIPLITE_CROP_Y + 4]
        unpcklps xmm7, xmm0
        subps xmm7, xmm6                   /* top delta */
        movss xmm8, dword ptr [rbx + SNIPLITE_CROP_X + 12]
        movss xmm0, dword ptr [rbx + SNIPLITE_CROP_Y + 12]
        unpcklps xmm8, xmm0                 /* bottom origin */
        movss xmm9, dword ptr [rbx + SNIPLITE_CROP_X + 8]
        movss xmm0, dword ptr [rbx + SNIPLITE_CROP_Y + 8]
        unpcklps xmm9, xmm0
        subps xmm9, xmm8                   /* bottom delta */

        mov ecx, dword ptr [rbx + SNIPLITE_CROP_PAD]
        add ecx, ecx
        mov eax, r14d
        dec eax
        sub eax, ecx
        mov edx, 1
        cmp eax, edx
        cmovl eax, edx
        pxor xmm10, xmm10
        cvtsi2ss xmm10, eax
        mov eax, r15d
        dec eax
        sub eax, ecx
        cmp eax, edx
        cmovl eax, edx
        pxor xmm14, xmm14
        cvtsi2ss xmm14, eax
        movaps xmm12, xmmword ptr [rip + .Lcrop_one]
        pxor xmm13, xmm13
        xor r11d, r11d

.Lcrop_row_\@:
        mov eax, r11d
        sub eax, dword ptr [rbx + SNIPLITE_CROP_PAD]
        pxor xmm11, xmm11
        cvtsi2ss xmm11, eax
        divss xmm11, xmm14
        maxss xmm11, xmm13
        minss xmm11, xmm12
        shufps xmm11, xmm11, 0
        xor r10d, r10d

.Lcrop_pixel_\@:
        mov eax, r10d
        sub eax, dword ptr [rbx + SNIPLITE_CROP_PAD]
        pxor xmm0, xmm0
        cvtsi2ss xmm0, eax
        divss xmm0, xmm10
        maxss xmm0, xmm13
        minss xmm0, xmm12
        shufps xmm0, xmm0, 0
.if \fused
        movaps xmm1, xmm6
        vfmadd231ps xmm1, xmm0, xmm7        /* top = origin + u * delta */
        movaps xmm2, xmm8
        vfmadd231ps xmm2, xmm0, xmm9        /* bottom = origin + u * delta */
        subps xmm2, xmm1
        vfmadd231ps xmm1, xmm11, xmm2       /* position = top + v * (bottom - top) */
.else
        movaps xmm1, xmm0
        mulps xmm1, xmm7
        addps xmm1, xmm6
        movaps xmm2, xmm0
        mulps xmm2, xmm9
        addps xmm2, xmm8
        subps xmm2, xmm1
        mulps xmm2, xmm11
        addps xmm1, xmm2
.endif

        /* Widening preserves every float exactly. The signed double 0.5 bias
         * cannot cross an incorrect int32 rounding boundary; truncation then
         * implements lroundf (ties away from zero), independently of MXCSR's
         * integer rounding mode. The float just below 0.5 still rounds to zero.
         */
        cvtps2pd xmm1, xmm1
        movapd xmm2, xmm1
        andpd xmm2, xmmword ptr [rip + .Lcrop_sign64]
        orpd xmm2, xmmword ptr [rip + .Lcrop_half64]
        addpd xmm1, xmm2
        cvttpd2dq xmm1, xmm1
        movd eax, xmm1
        pshufd xmm1, xmm1, 0x55
        movd edx, xmm1
        xor ecx, ecx
        test eax, eax
        cmovs eax, ecx
        cmp eax, r12d
        cmovg eax, r12d
        test edx, edx
        cmovs edx, ecx
        cmp edx, r13d
        cmovg edx, r13d

        mov ecx, dword ptr [rbx + SNIPLITE_CROP_WIDTH]
        imul rdx, rcx
        add rdx, rax
        imul rdx, rbp
        add rdx, rsi
        cmp ebp, 1
        je .Lcrop_copy1_\@
        cmp ebp, 3
        je .Lcrop_copy3_\@
        cmp ebp, 4
        je .Lcrop_copy4_\@
        xor ecx, ecx
.Lcrop_copy_other_\@:
        mov al, byte ptr [rdx + rcx]
        mov byte ptr [rdi + rcx], al
        inc rcx
        cmp rcx, rbp
        jb .Lcrop_copy_other_\@
        jmp .Lcrop_copied_\@
.Lcrop_copy1_\@:
        mov al, byte ptr [rdx]
        mov byte ptr [rdi], al
        jmp .Lcrop_copied_\@
.Lcrop_copy3_\@:
        mov ax, word ptr [rdx]
        mov word ptr [rdi], ax
        mov al, byte ptr [rdx + 2]
        mov byte ptr [rdi + 2], al
        jmp .Lcrop_copied_\@
.Lcrop_copy4_\@:
        mov eax, dword ptr [rdx]
        mov dword ptr [rdi], eax
.Lcrop_copied_\@:
        add rdi, rbp
        inc r10d
        cmp r10d, r14d
        jl .Lcrop_pixel_\@
        inc r11d
        cmp r11d, r15d
        jl .Lcrop_row_\@

.if \fused
        vzeroupper
.endif
        movaps xmm6, xmmword ptr [rsp]
        movaps xmm7, xmmword ptr [rsp + 16]
        movaps xmm8, xmmword ptr [rsp + 32]
        movaps xmm9, xmmword ptr [rsp + 48]
        movaps xmm10, xmmword ptr [rsp + 64]
        movaps xmm11, xmmword ptr [rsp + 80]
        movaps xmm12, xmmword ptr [rsp + 96]
        movaps xmm13, xmmword ptr [rsp + 112]
        movaps xmm14, xmmword ptr [rsp + 128]
        add rsp, 152
        pop r15
        pop r14
        pop r13
        pop r12
        pop rdi
        pop rsi
        pop rbp
        pop rbx
        ret
ENDF
.endm

CROP_KERNEL sniplite_ocr_crop_sse2, 0
CROP_KERNEL sniplite_ocr_crop_fma, 1
