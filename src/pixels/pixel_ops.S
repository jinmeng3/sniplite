/* Exact-bounds BGRA conversions, Windows x64 baseline SSE2.
 * rcx = dst, rdx = src, r8 = pixel count. Only volatile registers are used.
 * Vector iterations read 16 bytes for four pixels; tails use scalar accesses.
 */
#include "asm.inc"

FUNC pixel_bgra_to_rgb
        ENDPROLOG
        cmp r8, 4
        jb .Lrgb_tail
        movdqa xmm4, xmmword ptr [rip + .Lpixel_byte_mask]
        movdqa xmm5, xmmword ptr [rip + .Lpixel_green_mask]
.Lrgb_four:
        movdqu xmm0, xmmword ptr [rdx]
        movdqa xmm1, xmm0
        movdqa xmm2, xmm0
        psrld xmm1, 16
        pand xmm1, xmm4                /* R in byte 0 of each dword */
        pand xmm0, xmm5                /* G in byte 1 */
        pand xmm2, xmm4
        pslld xmm2, 16                 /* B in byte 2 */
        por xmm0, xmm1
        por xmm0, xmm2                 /* RGB0 0 RGB1 0 RGB2 0 RGB3 0 */

        movdqa xmm1, xmm0
        psrlq xmm1, 8
        pand xmm0, xmmword ptr [rip + .Lpixel_pair_first]
        pand xmm1, xmmword ptr [rip + .Lpixel_pair_second]
        por xmm0, xmm1                 /* RGB0 RGB1 0 0 RGB2 RGB3 0 0 */
        movdqa xmm1, xmm0
        psrldq xmm1, 8
        movq xmm0, xmm0                /* clear high qword */
        pslldq xmm1, 6
        por xmm0, xmm1                 /* twelve tightly packed RGB bytes */
        movq qword ptr [rcx], xmm0
        psrldq xmm0, 8
        movd dword ptr [rcx + 8], xmm0 /* never write a thirteenth byte */
        add rdx, 16
        add rcx, 12
        sub r8, 4
        cmp r8, 4
        jae .Lrgb_four
.Lrgb_tail:
        test r8, r8
        jz .Lrgb_done
.Lrgb_one:
        movzx eax, word ptr [rdx + 1]  /* G,R; alpha is not needed */
        rol ax, 8
        mov word ptr [rcx], ax         /* R,G */
        mov al, byte ptr [rdx]
        mov byte ptr [rcx + 2], al
        add rdx, 4
        add rcx, 3
        dec r8
        jnz .Lrgb_one
.Lrgb_done:
        ret
ENDF

FUNC pixel_bgra_to_gray
        ENDPROLOG
        cmp r8, 4
        jb .Lgray_tail
        pxor xmm5, xmm5
        movdqa xmm4, xmmword ptr [rip + .Lpixel_gray_weights]
.Lgray_four:
        movdqu xmm0, xmmword ptr [rdx]
        movdqa xmm1, xmm0
        punpcklbw xmm0, xmm5
        punpckhbw xmm1, xmm5
        pmaddwd xmm0, xmm4             /* BG0,R0,BG1,R1 weighted partial sums */
        pmaddwd xmm1, xmm4             /* BG2,R2,BG3,R3 */
        movdqa xmm2, xmm0
        shufps xmm0, xmm1, 0x88        /* BG0,BG1,BG2,BG3 */
        shufps xmm2, xmm1, 0xdd        /* R0,R1,R2,R3 (alpha coefficient zero) */
        paddd xmm0, xmm2               /* maximum 65280; no rounding offset */
        psrld xmm0, 8
        packssdw xmm0, xmm0
        packuswb xmm0, xmm0
        movd dword ptr [rcx], xmm0
        add rdx, 16
        add rcx, 4
        sub r8, 4
        cmp r8, 4
        jae .Lgray_four
.Lgray_tail:
        test r8, r8
        jz .Lgray_done
.Lgray_one:
        movzx eax, byte ptr [rdx + 2]
        movzx r9d, byte ptr [rdx + 1]
        movzx r10d, byte ptr [rdx]
        imul eax, eax, 77
        imul r9d, r9d, 150
        imul r10d, r10d, 29
        add eax, r9d
        add eax, r10d
        shr eax, 8
        mov byte ptr [rcx], al
        add rdx, 4
        inc rcx
        dec r8
        jnz .Lgray_one
.Lgray_done:
        ret
ENDF

        .section .rdata,"dr"
        .balign 16
.Lpixel_byte_mask:
        .long 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff
.Lpixel_green_mask:
        .long 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00
.Lpixel_pair_first:
        .quad 0x0000000000ffffff, 0x0000000000ffffff
.Lpixel_pair_second:
        .quad 0x0000ffffff000000, 0x0000ffffff000000
.Lpixel_gray_weights:
        .short 29, 150, 77, 0, 29, 150, 77, 0
