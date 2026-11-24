/* Ordered CPU matvec and attention kernels. Internal Windows x64 ABI. */
#include "asm.inc"

/* in, rows, out, n, row_stride (floats), row_count. No bias or scale.
 * Four lanes represent four output rows, retaining scalar k order. */
FUNC sniplite_matvec_sse2
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG rdi
        PUSHREG r12
        ENDPROLOG
        mov r12, qword ptr [rsp + 72]
        shl r12, 2
        mov esi, dword ptr [rsp + 80]
        test esi, esi
        jle .Lmv_sse_done
.Lmv_sse_rows:
        cmp esi, 4
        jl .Lmv_sse_single
        lea r10, [rdx + r12]
        lea r11, [r10 + r12]
        lea rbx, [r11 + r12]
        xorps xmm0, xmm0
        xor eax, eax
        test r9d, r9d
        jle .Lmv_sse_store4
.Lmv_sse_k4:
        movss xmm1, dword ptr [rcx + rax*4]
        shufps xmm1, xmm1, 0
        movss xmm2, dword ptr [rdx + rax*4]
        movss xmm3, dword ptr [r10 + rax*4]
        unpcklps xmm2, xmm3
        movss xmm3, dword ptr [r11 + rax*4]
        movss xmm4, dword ptr [rbx + rax*4]
        unpcklps xmm3, xmm4
        movlhps xmm2, xmm3
        mulps xmm2, xmm1
        addps xmm0, xmm2
        inc eax
        cmp eax, r9d
        jl .Lmv_sse_k4
.Lmv_sse_store4:
        movups xmmword ptr [r8], xmm0
        lea rdx, [rbx + r12]
        add r8, 16
        sub esi, 4
        jnz .Lmv_sse_rows
        jmp .Lmv_sse_done
.Lmv_sse_single:
        xorps xmm0, xmm0
        xor eax, eax
        test r9d, r9d
        jle .Lmv_sse_store1
.Lmv_sse_k1:
        movss xmm1, dword ptr [rcx + rax*4]
        mulss xmm1, dword ptr [rdx + rax*4]
        addss xmm0, xmm1
        inc eax
        cmp eax, r9d
        jl .Lmv_sse_k1
.Lmv_sse_store1:
        movss dword ptr [r8], xmm0
        add rdx, r12
        add r8, 4
        dec esi
        jnz .Lmv_sse_single
.Lmv_sse_done:
        pop r12
        pop rdi
        pop rsi
        pop rbx
        ret
ENDF

.macro DOT_STEP off, a, b, c, d, rows
        vmovups ymm8, ymmword ptr [rcx + rdi + \off]
        vfmadd231ps \a, ymm8, ymmword ptr [rdx + rdi + \off]
.if \rows > 1
        vfmadd231ps \b, ymm8, ymmword ptr [r10 + rdi + \off]
.endif
.if \rows > 2
        vfmadd231ps \c, ymm8, ymmword ptr [r11 + rdi + \off]
        vfmadd231ps \d, ymm8, ymmword ptr [rbx + rdi + \off]
.endif
.endm

.macro DOT_REDUCE a, b, c, d, rows, wide
.if \wide
        vaddps \a, \a, \b
        vaddps \c, \c, \d
        vaddps \a, \a, \c
.else
        vaddps \a, \a, \b
.endif
.endm

.macro DOT_OUTPUT y, x, row, ptr
        vextractf128 xmm8, \y, 1
        vaddps \x, \x, xmm8
        vmovhlps xmm8, xmm8, \x
        vaddps \x, \x, xmm8
        vshufps xmm8, \x, \x, 1
        vaddss \x, \x, xmm8
        mov rax, rdi
        /* GCC's ordered tail uses four separately rounded products before
         * the final 0..3 scalar FMAs. Keep the same contraction boundary. */
        mov eax, r9d
        sub eax, edi
        cmp eax, 4
        jl .Ldot_small\@
        vmovups xmm8, xmmword ptr [rcx + rdi*4]
        vmulps xmm8, xmm8, xmmword ptr [\ptr + rdi*4]
        vaddss \x, \x, xmm8
        vshufps xmm1, xmm8, xmm8, 0x55
        vaddss \x, xmm1, \x
        vunpckhps xmm1, xmm8, xmm8
        vaddss \x, xmm1, \x
        vshufps xmm8, xmm8, xmm8, 0xff
        vaddss \x, \x, xmm8
        lea rax, [rdi + 4]
        jmp .Ldot_scalar\@
.Ldot_small\@:
        mov rax, rdi
.Ldot_scalar\@:
        cmp eax, r9d
        jge .Ldot_store\@
        vmovss xmm8, dword ptr [rcx + rax*4]
        vfmadd231ss \x, xmm8, dword ptr [\ptr + rax*4]
        inc eax
        jmp .Ldot_scalar\@
.Ldot_store\@:
        vmovss dword ptr [r8 + 4*\row], \x
.endm

.macro DOT_BLOCK rows, wide
        vxorps xmm0, xmm0, xmm0
        vxorps xmm1, xmm1, xmm1
        vxorps xmm2, xmm2, xmm2
        vxorps xmm3, xmm3, xmm3
        vxorps xmm4, xmm4, xmm4
        vxorps xmm5, xmm5, xmm5
        vxorps xmm6, xmm6, xmm6
        vxorps xmm7, xmm7, xmm7
        xor edi, edi
        mov esi, r9d
.if \wide
.Ldot_main\@:
        cmp esi, 32
        jl .Ldot_main_end\@
        DOT_STEP 0, ymm0, ymm4, ymm0, ymm0, \rows
        DOT_STEP 32, ymm1, ymm5, ymm0, ymm0, \rows
        DOT_STEP 64, ymm2, ymm6, ymm0, ymm0, \rows
        DOT_STEP 96, ymm3, ymm7, ymm0, ymm0, \rows
        add rdi, 128
        sub esi, 32
        jmp .Ldot_main\@
.Ldot_main_end\@:
        DOT_REDUCE ymm0, ymm1, ymm2, ymm3, \rows, 1
.if \rows > 1
        DOT_REDUCE ymm4, ymm5, ymm6, ymm7, \rows, 1
.endif
.else
.Ldot_main\@:
        cmp esi, 16
        jl .Ldot_main_end\@
        DOT_STEP 0, ymm0, ymm2, ymm4, ymm6, \rows
        DOT_STEP 32, ymm1, ymm3, ymm5, ymm7, \rows
        add rdi, 64
        sub esi, 16
        jmp .Ldot_main\@
.Ldot_main_end\@:
        vaddps ymm0, ymm0, ymm1
.if \rows > 1
        vaddps ymm2, ymm2, ymm3
        vaddps ymm4, ymm4, ymm5
        vaddps ymm6, ymm6, ymm7
.endif
.endif
.Ldot_eight\@:
        cmp esi, 8
        jl .Ldot_eight_end\@
.if \wide
        DOT_STEP 0, ymm0, ymm4, ymm0, ymm0, \rows
.else
        DOT_STEP 0, ymm0, ymm2, ymm4, ymm6, \rows
.endif
        add rdi, 32
        sub esi, 8
        jmp .Ldot_eight\@
.Ldot_eight_end\@:
        shr rdi, 2
        DOT_OUTPUT ymm0, xmm0, 0, rdx
.if \rows > 1
.if \wide
        DOT_OUTPUT ymm4, xmm4, 1, r10
.else
        DOT_OUTPUT ymm2, xmm2, 1, r10
        DOT_OUTPUT ymm4, xmm4, 2, r11
        DOT_OUTPUT ymm6, xmm6, 3, rbx
.endif
.endif
.endm

.macro MATVEC_AVX name, wide, rows
FUNC \name
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG rdi
        PUSHREG r12
        PUSHREG r13
        ALLOC 64
        SAVEXMM xmm6, 0
        SAVEXMM xmm7, 16
        SAVEXMM xmm8, 32
        ENDPROLOG
        mov r12, qword ptr [rsp + 144]
        shl r12, 2
        mov r13d, dword ptr [rsp + 152]
        test r13d, r13d
        jle .Lmatvec_done\@
.Lmatvec_rows\@:
        cmp r13d, \rows
        jl .Lmatvec_tail\@
        lea r10, [rdx + r12]
.if \rows > 2
        lea r11, [r10 + r12]
        lea rbx, [r11 + r12]
.endif
        DOT_BLOCK \rows, \wide
.if \rows > 2
        lea rdx, [rbx + r12]
.else
        lea rdx, [r10 + r12]
.endif
        add r8, 4*\rows
        sub r13d, \rows
        jnz .Lmatvec_rows\@
        jmp .Lmatvec_done\@
.Lmatvec_tail\@:
        DOT_BLOCK 1, \wide
        add rdx, r12
        add r8, 4
        dec r13d
        jnz .Lmatvec_tail\@
.Lmatvec_done\@:
        vmovaps xmm6, xmmword ptr [rsp]
        vmovaps xmm7, xmmword ptr [rsp + 16]
        vmovaps xmm8, xmmword ptr [rsp + 32]
        vzeroupper
        add rsp, 64
        pop r13
        pop r12
        pop rdi
        pop rsi
        pop rbx
        ret
ENDF
.endm

MATVEC_AVX sniplite_matvec_fma, 0, 4
MATVEC_AVX sniplite_matvec_wide_fma, 1, 2

/* scores, V, output, tokens, V row stride (floats), head dimensions.
 * Keep four dimension blocks in registers, updating tokens in original order. */
.macro WEIGHTED_V name, fused, lanes
FUNC \name
        PUSHREG rbx
        PUSHREG rsi
        PUSHREG rdi
        ENDPROLOG
        mov r10, qword ptr [rsp + 64]
        shl r10, 2
        mov r11d, dword ptr [rsp + 72]
.Lwv_block\@:
        cmp r11d, 4*\lanes
        jl .Lwv_vector\@
.if \fused
        vxorps xmm0, xmm0, xmm0
        vxorps xmm1, xmm1, xmm1
        vxorps xmm2, xmm2, xmm2
        vxorps xmm3, xmm3, xmm3
.else
        xorps xmm0, xmm0
        xorps xmm1, xmm1
        xorps xmm2, xmm2
        xorps xmm3, xmm3
.endif
        mov rbx, rdx
        mov rsi, rcx
        mov edi, r9d
        test edi, edi
        jle .Lwv_store4\@
.Lwv_tokens4\@:
.if \fused
        vbroadcastss ymm4, dword ptr [rsi]
        vfmadd231ps ymm0, ymm4, ymmword ptr [rbx]
        vfmadd231ps ymm1, ymm4, ymmword ptr [rbx + 32]
        vfmadd231ps ymm2, ymm4, ymmword ptr [rbx + 64]
        vfmadd231ps ymm3, ymm4, ymmword ptr [rbx + 96]
.else
        movss xmm4, dword ptr [rsi]
        shufps xmm4, xmm4, 0
        .irp off,0,16,32,48
        movups xmm5, xmmword ptr [rbx + \off]
        mulps xmm5, xmm4
.if \off == 0
        addps xmm0, xmm5
.elseif \off == 16
        addps xmm1, xmm5
.elseif \off == 32
        addps xmm2, xmm5
.else
        addps xmm3, xmm5
.endif
        .endr
.endif
        add rbx, r10
        add rsi, 4
        dec edi
        jnz .Lwv_tokens4\@
.Lwv_store4\@:
.if \fused
        vmovups ymmword ptr [r8], ymm0
        vmovups ymmword ptr [r8 + 32], ymm1
        vmovups ymmword ptr [r8 + 64], ymm2
        vmovups ymmword ptr [r8 + 96], ymm3
.else
        movups xmmword ptr [r8], xmm0
        movups xmmword ptr [r8 + 16], xmm1
        movups xmmword ptr [r8 + 32], xmm2
        movups xmmword ptr [r8 + 48], xmm3
.endif
        add rdx, 16*\lanes
        add r8, 16*\lanes
        sub r11d, 4*\lanes
        jmp .Lwv_block\@
.Lwv_vector\@:
        cmp r11d, \lanes
        jl .Lwv_scalar\@
.if \fused
        vxorps xmm0, xmm0, xmm0
.else
        xorps xmm0, xmm0
.endif
        mov rbx, rdx
        mov rsi, rcx
        mov edi, r9d
        test edi, edi
        jle .Lwv_store\@
.Lwv_tokens\@:
.if \fused
        vbroadcastss ymm4, dword ptr [rsi]
        vfmadd231ps ymm0, ymm4, ymmword ptr [rbx]
.else
        movss xmm4, dword ptr [rsi]
        shufps xmm4, xmm4, 0
        movups xmm5, xmmword ptr [rbx]
        mulps xmm5, xmm4
        addps xmm0, xmm5
.endif
        add rbx, r10
        add rsi, 4
        dec edi
        jnz .Lwv_tokens\@
.Lwv_store\@:
.if \fused
        vmovups ymmword ptr [r8], ymm0
.else
        movups xmmword ptr [r8], xmm0
.endif
        add rdx, 4*\lanes
        add r8, 4*\lanes
        sub r11d, \lanes
        jmp .Lwv_vector\@
.Lwv_scalar\@:
        test r11d, r11d
        jle .Lwv_done\@
.if \fused
        vxorps xmm0, xmm0, xmm0
.else
        xorps xmm0, xmm0
.endif
        mov rbx, rdx
        mov rsi, rcx
        mov edi, r9d
        test edi, edi
        jle .Lwv_scalar_store\@
.Lwv_scalar_tokens\@:
.if \fused
        vmovss xmm4, dword ptr [rsi]
        vfmadd231ss xmm0, xmm4, dword ptr [rbx]
.else
        movss xmm4, dword ptr [rsi]
        mulss xmm4, dword ptr [rbx]
        addss xmm0, xmm4
.endif
        add rbx, r10
        add rsi, 4
        dec edi
        jnz .Lwv_scalar_tokens\@
.Lwv_scalar_store\@:
.if \fused
        vmovss dword ptr [r8], xmm0
.else
        movss dword ptr [r8], xmm0
.endif
        add rdx, 4
        add r8, 4
        dec r11d
        jmp .Lwv_scalar\@
.Lwv_done\@:
.if \fused
        vzeroupper
.endif
        pop rdi
        pop rsi
        pop rbx
        ret
ENDF
.endm

WEIGHTED_V sniplite_weighted_v_sse2, 0, 4
WEIGHTED_V sniplite_weighted_v_fma, 1, 8
