/* Windows x64 SSE2 bitmap kernels. See ocr_detect_kernels.h for the ABI. */
#include "asm.inc"

/* rcx=prob, rdx=binary, r8=count, xmm3=threshold. */
FUNC sniplite_detect_threshold_sse2
        ENDPROLOG
        shufps xmm3, xmm3, 0
        pcmpeqd xmm4, xmm4
        psrld xmm4, 31
        cmp r8, 4
        jb .Lthreshold_tail
.Lthreshold_four:
        movups xmm0, xmmword ptr [rcx]
        movaps xmm1, xmm3
        cmpltps xmm1, xmm0             /* ordered threshold < probability */
        pand xmm1, xmm4
        packssdw xmm1, xmm1
        packuswb xmm1, xmm1
        movd dword ptr [rdx], xmm1
        add rcx, 16
        add rdx, 4
        sub r8, 4
        cmp r8, 4
        jae .Lthreshold_four
.Lthreshold_tail:
        test r8, r8
        jz .Lthreshold_done
.Lthreshold_one:
        movss xmm0, dword ptr [rcx]
        ucomiss xmm0, xmm3
        seta byte ptr [rdx]            /* unordered sets CF, so NaN is false */
        add rcx, 4
        inc rdx
        dec r8
        jnz .Lthreshold_one
.Lthreshold_done:
        ret
ENDF

/* rcx=src, rdx=dst, r8d=width, r9d=height.
 * Edge rows alias the center row: duplicated OR operands equal zero padding
 * because the center row is always included. No address leaves its source row.
 */
FUNC sniplite_detect_dilate3x3_sse2
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG rdi
        PUSHREG r12
        PUSHREG r13
        PUSHREG r14
        ENDPROLOG
        test r8d, r8d
        jle .Ldilate_done
        test r9d, r9d
        jle .Ldilate_done
        mov rsi, rcx
        mov rdi, rdx
        mov r12d, r8d
        mov r13d, r9d
        xor r14d, r14d
        lea rbx, [r12 - 16]
.Ldilate_row:
        mov rax, rsi                   /* previous row */
        test r14, r14
        jz .Ldilate_top
        sub rax, r12
.Ldilate_top:
        mov rcx, rsi                   /* next row */
        cmp r13, 1
        je .Ldilate_bottom
        add rcx, r12
.Ldilate_bottom:
        mov r11b, byte ptr [rax]
        or r11b, byte ptr [rsi]
        or r11b, byte ptr [rcx]
        cmp r12, 1
        je .Ldilate_first
        or r11b, byte ptr [rax + 1]
        or r11b, byte ptr [rsi + 1]
        or r11b, byte ptr [rcx + 1]
.Ldilate_first:
        mov byte ptr [rdi], r11b
        mov r10d, 1
        cmp r10, rbx
        jge .Ldilate_tail
.Ldilate_sixteen:
        movdqu xmm0, xmmword ptr [rax + r10 - 1]
        movdqu xmm1, xmmword ptr [rsi + r10 - 1]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rcx + r10 - 1]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rax + r10]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rsi + r10]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rcx + r10]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rax + r10 + 1]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rsi + r10 + 1]
        por xmm0, xmm1
        movdqu xmm1, xmmword ptr [rcx + r10 + 1]
        por xmm0, xmm1
        movdqu xmmword ptr [rdi + r10], xmm0
        add r10, 16
        cmp r10, rbx
        jl .Ldilate_sixteen
.Ldilate_tail:
        cmp r10, r12
        jae .Ldilate_next_row
.Ldilate_one:
        mov r11b, byte ptr [rax + r10 - 1]
        or r11b, byte ptr [rsi + r10 - 1]
        or r11b, byte ptr [rcx + r10 - 1]
        or r11b, byte ptr [rax + r10]
        or r11b, byte ptr [rsi + r10]
        or r11b, byte ptr [rcx + r10]
        lea rdx, [r10 + 1]
        cmp rdx, r12
        jae .Ldilate_store
        or r11b, byte ptr [rax + rdx]
        or r11b, byte ptr [rsi + rdx]
        or r11b, byte ptr [rcx + rdx]
.Ldilate_store:
        mov byte ptr [rdi + r10], r11b
        inc r10
        cmp r10, r12
        jb .Ldilate_one
.Ldilate_next_row:
        add rsi, r12
        add rdi, r12
        inc r14
        dec r13
        jnz .Ldilate_row
.Ldilate_done:
        pop r14
        pop r13
        pop r12
        pop rdi
        pop rsi
        pop rbx
        ret
ENDF
