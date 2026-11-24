/* Horizontally complete depthwise windows, including clipped vertical rows.
 * SnipliteDepthwiseRow: src=0, dst=8, weights=16, row_stride=24 (floats),
 * length=32, stride=36, bias=40, rows=44. src/weights start at the first
 * valid ky; rows is 1..kernel and weights retain the kernel-wide row pitch.
 */
#include "asm.inc"

/* Three independent output vectors use only volatile xmm/ymm0..5. A weight
 * is tested/broadcast once for all three, preserving ky/kx order per output.
 * rax=input row, r9=weight row, xmm/ymm3=weight, xmm/ymm4..5=input scratch.
 */
.macro DW_INPUT offset, stride, fused, lanes
.if \lanes == 1
.if \fused
        vmovss xmm4, dword ptr [rax + \offset]
.else
        movss xmm4, dword ptr [rax + \offset]
.endif
.elseif \fused && \lanes == 8
        vmovups ymm4, ymmword ptr [rax + \offset]
.if \stride == 2
        /* [0..7] and [7..14]: every load stays within the eight windows. */
        vmovups ymm5, ymmword ptr [rax + \offset + 28]
        vshufps ymm4, ymm4, ymm5, 0xd8
        vpermpd ymm4, ymm4, 0xd8
.endif
.elseif \fused
        vmovups xmm4, xmmword ptr [rax + \offset]
.if \stride == 2
        /* [0..3] and [3..6] need no eighth pixel or masked tail load. */
        vmovups xmm5, xmmword ptr [rax + \offset + 12]
        vshufps xmm4, xmm4, xmm5, 0xd8
.endif
.else
        movups xmm4, xmmword ptr [rax + \offset]
.if \stride == 2
        movups xmm5, xmmword ptr [rax + \offset + 12]
        shufps xmm4, xmm5, 0xd8
.endif
.endif
.endm

.macro DW_ACCUM acc, offset, stride, fused, lanes
        DW_INPUT \offset, \stride, \fused, \lanes
.if \fused
.if \lanes == 1
        vfmadd231ss xmm\acc, xmm3, xmm4
.elseif \lanes == 8
        vfmadd231ps ymm\acc, ymm3, ymm4
.else
        vfmadd231ps xmm\acc, xmm3, xmm4
.endif
.else
.if \lanes == 1
        mulss xmm4, xmm3
        addss xmm\acc, xmm4
.else
        mulps xmm4, xmm3
        addps xmm\acc, xmm4
.endif
.endif
.endm

.macro DW_TAP offset, stride, fused, lanes, groups
.if \fused
.if \lanes == 8
        vbroadcastss ymm3, dword ptr [r9 + \offset]
.elseif \lanes == 4
        vbroadcastss xmm3, dword ptr [r9 + \offset]
.else
        vmovss xmm3, dword ptr [r9 + \offset]
.endif
        vxorps xmm5, xmm5, xmm5
        vucomiss xmm3, xmm5
.else
        movss xmm3, dword ptr [r9 + \offset]
        xorps xmm5, xmm5
        ucomiss xmm3, xmm5
.endif
        /* Ordered equality skips both zero signs, but never skips NaNs. */
        jp .Ldw_nonzero\@
        je .Ldw_skip\@
.Ldw_nonzero\@:
.if !\fused && \lanes != 1
        shufps xmm3, xmm3, 0
.endif
        DW_ACCUM 0, \offset, \stride, \fused, \lanes
.if \groups == 3
        DW_ACCUM 1, (\offset + 4*\lanes*\stride), \stride, \fused, \lanes
        DW_ACCUM 2, (\offset + 8*\lanes*\stride), \stride, \fused, \lanes
.endif
.Ldw_skip\@:
.endm

.macro DW_BLOCK kernel, stride, fused, lanes, groups
.if \fused
.if \lanes == 8
        vbroadcastss ymm0, dword ptr [rcx + 40]
.if \groups == 3
        vmovaps ymm1, ymm0
        vmovaps ymm2, ymm0
.endif
.elseif \lanes == 4
        vbroadcastss xmm0, dword ptr [rcx + 40]
.else
        vmovss xmm0, dword ptr [rcx + 40]
.endif
.else
        movss xmm0, dword ptr [rcx + 40]
.if \lanes != 1
        shufps xmm0, xmm0, 0
.endif
.if \groups == 3
        movaps xmm1, xmm0
        movaps xmm2, xmm0
.endif
.endif
        mov rax, rdx
        mov r9, qword ptr [rcx + 16]
        mov ebx, dword ptr [rcx + 44]
.Ldw_row\@:
        .set .Ldw_offset\@, 0
        .rept \kernel
        DW_TAP .Ldw_offset\@, \stride, \fused, \lanes, \groups
        .set .Ldw_offset\@, .Ldw_offset\@ + 4
        .endr
        add rax, r10
        add r9, 4*\kernel
        dec ebx
        jnz .Ldw_row\@
.if \fused
.if \lanes == 8
        vmovups ymmword ptr [r8], ymm0
.if \groups == 3
        vmovups ymmword ptr [r8 + 32], ymm1
        vmovups ymmword ptr [r8 + 64], ymm2
.endif
.elseif \lanes == 4
        vmovups xmmword ptr [r8], xmm0
.else
        vmovss dword ptr [r8], xmm0
.endif
.else
.if \lanes == 1
        movss dword ptr [r8], xmm0
.else
        movups xmmword ptr [r8], xmm0
.if \groups == 3
        movups xmmword ptr [r8 + 16], xmm1
        movups xmmword ptr [r8 + 32], xmm2
.endif
.endif
.endif
        add rdx, 4*\lanes*\groups*\stride
        add r8, 4*\lanes*\groups
        sub r11d, \lanes*\groups
.endm

.macro DW_ROW kernel, stride, fused, lanes
.Ldw_block\@:
        cmp r11d, 3*\lanes
        jl .Ldw_vector\@
        DW_BLOCK \kernel, \stride, \fused, \lanes, 3
        jmp .Ldw_block\@
.Ldw_vector\@:
        cmp r11d, \lanes
        jl .Ldw_four\@
        DW_BLOCK \kernel, \stride, \fused, \lanes, 1
        jmp .Ldw_vector\@
.Ldw_four\@:
.if \fused
        cmp r11d, 4
        jl .Ldw_scalar\@
        DW_BLOCK \kernel, \stride, \fused, 4, 1
.endif
.Ldw_scalar\@:
        test r11d, r11d
        jle .Ldw_done\@
        DW_BLOCK \kernel, \stride, \fused, 1, 1
        jmp .Ldw_scalar\@
.Ldw_done\@:
.if \fused
        vzeroupper
.endif
        pop rbx
        ret
.endm

.macro DEPTHWISE name, kernel, fused, lanes
FUNC \name
        /* One saved GPR keeps the vertical loop compact; SIMD stays volatile. */
        PUSHREG rbx
        ENDPROLOG
        mov rdx, qword ptr [rcx]
        mov r8, qword ptr [rcx + 8]
        mov r10, qword ptr [rcx + 24]
        shl r10, 2
        mov r11d, dword ptr [rcx + 32]
        cmp dword ptr [rcx + 36], 2
        je .Ldw_stride2\@
        DW_ROW \kernel, 1, \fused, \lanes
.Ldw_stride2\@:
        DW_ROW \kernel, 2, \fused, \lanes
ENDF
.endm

DEPTHWISE sniplite_depthwise3_sse2, 3, 0, 4
DEPTHWISE sniplite_depthwise5_sse2, 5, 0, 4
DEPTHWISE sniplite_depthwise3_fma, 3, 1, 8
DEPTHWISE sniplite_depthwise5_fma, 5, 1, 8
