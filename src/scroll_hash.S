/* Long-screenshot row hashes, exactly the original two-chain scalar algorithm.
 * Two neighboring strips run together: four independent integer multiply chains
 * hide latency without vector emulation of 64-bit multiplication. All eight
 * strips and all rows share one call and one register-save prologue. */
#include "asm.inc"

/* void scroll_hash_rows(uint32_t *out, const uint32_t *pixels,
 *                       int width, int height, int x0, int strip_width);
 * Positive width/height, strip_width >= 1, x0 >= 0 and
 * x0 + 8 * strip_width <= width. Output is contiguous [height][8].
 * Unaligned qword loads consume four pixels per iteration (two per chain).
 * The remaining 1..3 pixels are individual zero-extended dword loads into a.
 * Stack: next row, rows left, row bytes, qword groups, tail pixels. */
FUNC scroll_hash_rows
        PUSHREG rbx
        PUSHREG rbp
        PUSHREG rdi
        PUSHREG rsi
        PUSHREG r12
        PUSHREG r13
        PUSHREG r14
        PUSHREG r15
        ALLOC 40
        ENDPROLOG

        mov rsi, rcx
        mov eax, dword ptr [rsp + 144]   /* entry rsp+40: x0 */
        lea rdx, [rdx + rax*4]
        mov qword ptr [rsp], rdx
        mov dword ptr [rsp + 8], r9d
        mov eax, r8d
        shl rax, 2
        mov qword ptr [rsp + 16], rax
        mov r8d, dword ptr [rsp + 152]  /* entry rsp+48: strip_width */
        mov eax, r8d
        and eax, 3
        mov dword ptr [rsp + 28], eax
        mov eax, r8d
        shr eax, 2
        mov dword ptr [rsp + 24], eax
        shl r8, 2
        movabs r12, 0x9E3779B97F4A7C15
        movabs r13, 0xC2B2AE3D27D4EB4F
        movabs r14, 0xFF51AFD7ED558CCD
        movabs r15, 0xC4CEB9FE1A85EC53
.Lhash_row:
        mov rdx, qword ptr [rsp]
        mov rax, qword ptr [rsp + 16]
        add qword ptr [rsp], rax
        mov ebp, 4
.Lhash_pair:
        mov rax, r12
        mov rbx, r13
        mov rcx, r12
        mov rdi, r13
        lea r10, [rdx + r8]
        mov r9d, dword ptr [rsp + 24]
        test r9d, r9d
        jz .Lhash_tail
        .balign 16
.Lhash_groups:
        xor rax, qword ptr [rdx]
        xor rcx, qword ptr [r10]
        xor rbx, qword ptr [rdx + 8]
        xor rdi, qword ptr [r10 + 8]
        imul rax, r14
        imul rcx, r14
        imul rbx, r15
        imul rdi, r15
        add rdx, 16
        add r10, 16
        dec r9d
        jnz .Lhash_groups
.Lhash_tail:
        mov r9d, dword ptr [rsp + 28]
        test r9d, r9d
        jz .Lhash_finish
.Lhash_pixel:
        mov r11d, dword ptr [rdx]
        xor rax, r11
        mov r11d, dword ptr [r10]
        xor rcx, r11
        imul rax, r14
        imul rcx, r14
        add rdx, 4
        add r10, 4
        dec r9d
        jnz .Lhash_pixel
.Lhash_finish:
        rol rbx, 32
        rol rdi, 32
        xor rax, rbx
        xor rcx, rdi
        imul rax, r12
        imul rcx, r12
        shr rax, 32
        shr rcx, 32
        cmp eax, 1
        adc eax, 0                    /* h + !h: zero is a free table slot */
        cmp ecx, 1
        adc ecx, 0
        mov dword ptr [rsi], eax
        mov dword ptr [rsi + 4], ecx
        add rsi, 8
        mov rdx, r10                  /* second strip's end = next pair */
        dec ebp
        jnz .Lhash_pair
        dec dword ptr [rsp + 8]
        jnz .Lhash_row

        add rsp, 40
        pop r15
        pop r14
        pop r13
        pop r12
        pop rsi
        pop rdi
        pop rbp
        pop rbx
        ret
ENDF
