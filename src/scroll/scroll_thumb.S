/* Integer box averages for the long-screenshot preview. SSE2 is baseline x64.
 * Pixel sums wrap as uint32_t, division truncates, and output alpha stays zero. */
#include "asm.inc"

#define NEXT_ROW 0
#define ROWS_LEFT 8
#define Y_REM 12
#define HEIGHT 16
#define X_REM 20
#define WIDTH 24
#define AREA 28

/* void scroll_thumb_rows(uint32_t *dst, const uint32_t *src, int width,
 *                        int thumb_width, int first, int end);
 * dst starts at thumbnail row first; src starts at canvas row zero.
 * Contract: width >= thumb_width > 0, 0 <= first < end; all source rows exist.
 * Boundaries are floor(index * width / thumb_width). Quotient/remainder steps
 * reproduce those boundaries exactly without dividing for each destination x. */
FUNC scroll_thumb_rows
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG rdi
        PUSHREG r12
        PUSHREG r13
        PUSHREG r14
        PUSHREG r15
        ALLOC 32
        ENDPROLOG
        mov rdi, rcx
        mov rsi, rdx
        mov r12d, r9d
        mov r15d, r8d
        shl r15, 2                     /* source stride in bytes */
        mov r10d, dword ptr [rsp + 128] /* first */
        mov r11d, dword ptr [rsp + 136] /* end */
        sub r11d, r10d
        mov dword ptr [rsp + ROWS_LEFT], r11d
        mov eax, r8d
        xor edx, edx
        div r12d
        mov r13d, eax                  /* width / thumb_width */
        mov r14d, edx                  /* width % thumb_width */
        mov eax, r8d
        imul rax, r10
        xor edx, edx
        div r12
        mov dword ptr [rsp + Y_REM], edx
        imul rax, r15
        add rsi, rax                   /* source row floor(first * width / tw) */
        pxor xmm5, xmm5
.Lthumb_row:
        mov ecx, r13d
        mov eax, dword ptr [rsp + Y_REM]
        add eax, r14d
        cmp eax, r12d
        jb .Lthumb_y_ready
        sub eax, r12d
        inc ecx
.Lthumb_y_ready:
        mov dword ptr [rsp + Y_REM], eax
        mov dword ptr [rsp + HEIGHT], ecx
        mov eax, ecx
        imul rax, r15
        add rax, rsi
        mov qword ptr [rsp + NEXT_ROW], rax
        mov ebx, r12d
        mov dword ptr [rsp + X_REM], 0
.Lthumb_cell:
        mov ecx, r13d
        mov eax, dword ptr [rsp + X_REM]
        add eax, r14d
        cmp eax, r12d
        jb .Lthumb_x_ready
        sub eax, r12d
        inc ecx
.Lthumb_x_ready:
        mov dword ptr [rsp + X_REM], eax
        mov dword ptr [rsp + WIDTH], ecx
        imul ecx, dword ptr [rsp + HEIGHT]
        mov dword ptr [rsp + AREA], ecx
        mov r9d, dword ptr [rsp + HEIGHT]
        mov r10, rsi
        pxor xmm0, xmm0                /* uint32 sums B, G, R, A */
.Lthumb_source_row:
        mov rcx, r10
        mov r8d, dword ptr [rsp + WIDTH]
.Lthumb_four:
        cmp r8d, 4
        jb .Lthumb_tail
        movdqu xmm1, xmmword ptr [rcx]
        movdqa xmm2, xmm1
        punpcklbw xmm1, xmm5
        punpckhbw xmm2, xmm5
        paddw xmm1, xmm2               /* pixels 0+2 and 1+3, at most 510 */
        movdqa xmm2, xmm1
        psrldq xmm2, 8
        paddw xmm1, xmm2               /* four pixels, at most 1020 */
        punpcklwd xmm1, xmm5
        paddd xmm0, xmm1
        add rcx, 16
        sub r8d, 4
        jmp .Lthumb_four
.Lthumb_tail:
        test r8d, r8d
        jz .Lthumb_source_next
.Lthumb_one:
        movd xmm1, dword ptr [rcx]     /* never read past the region's edge */
        punpcklbw xmm1, xmm5
        punpcklwd xmm1, xmm5
        paddd xmm0, xmm1
        add rcx, 4
        dec r8d
        jnz .Lthumb_one
.Lthumb_source_next:
        add r10, r15
        dec r9d
        jnz .Lthumb_source_row
        movd eax, xmm0
        xor edx, edx
        div dword ptr [rsp + AREA]
        mov r11d, eax                  /* B */
        psrldq xmm0, 4
        movd eax, xmm0
        xor edx, edx
        div dword ptr [rsp + AREA]
        shl eax, 8
        or r11d, eax                   /* G */
        psrldq xmm0, 4
        movd eax, xmm0
        xor edx, edx
        div dword ptr [rsp + AREA]
        shl eax, 16
        or eax, r11d                   /* R, alpha zero */
        mov dword ptr [rdi], eax
        add rdi, 4
        mov eax, dword ptr [rsp + WIDTH]
        lea rsi, [rsi + rax * 4]
        dec ebx
        jnz .Lthumb_cell
        mov rsi, qword ptr [rsp + NEXT_ROW]
        dec dword ptr [rsp + ROWS_LEFT]
        jnz .Lthumb_row
        add rsp, 32
        pop r15
        pop r14
        pop r13
        pop r12
        pop rdi
        pop rsi
        pop rbx
        ret
ENDF
