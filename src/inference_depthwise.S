/* Full-window depthwise row blocks. Borders stay in the ordered C++ loop. */
#include "asm.inc"

/* rax=input tap row, r9=weights, xmm/ymm0=acc, xmm/ymm5=bias. */
.macro DW_TAP weight, offset, stride, fused, scalar
.if \fused
        vmovss xmm1, dword ptr [r9 + \weight]
        vxorps xmm3, xmm3, xmm3
        vucomiss xmm1, xmm3
.else
        movss xmm1, dword ptr [r9 + \weight]
        xorps xmm3, xmm3
        ucomiss xmm1, xmm3
.endif
        jp .Ldw_nonzero\@
        je .Ldw_skip\@
.Ldw_nonzero\@:
.if \scalar
.if \fused
        vmovss xmm2, dword ptr [rax + \offset]
        vfmadd231ss xmm0, xmm1, xmm2
.else
        movss xmm2, dword ptr [rax + \offset]
        mulss xmm2, xmm1
        addss xmm0, xmm2
.endif
.else
.if \fused
.if \stride == 1
        vmovups ymm2, ymmword ptr [rax + \offset]
.else
        vmovss xmm2, dword ptr [rax + \offset]
        vinsertps xmm2, xmm2, dword ptr [rax + \offset + 8], 0x10
        vinsertps xmm2, xmm2, dword ptr [rax + \offset + 16], 0x20
        vinsertps xmm2, xmm2, dword ptr [rax + \offset + 24], 0x30
        vmovss xmm3, dword ptr [rax + \offset + 32]
        vinsertps xmm3, xmm3, dword ptr [rax + \offset + 40], 0x10
        vinsertps xmm3, xmm3, dword ptr [rax + \offset + 48], 0x20
        vinsertps xmm3, xmm3, dword ptr [rax + \offset + 56], 0x30
        vinsertf128 ymm2, ymm2, xmm3, 1
.endif
        vbroadcastss ymm1, dword ptr [r9 + \weight]
        vfmadd231ps ymm0, ymm1, ymm2
.else
.if \stride == 1
        movups xmm2, xmmword ptr [rax + \offset]
.else
        movss xmm2, dword ptr [rax + \offset]
        movss xmm3, dword ptr [rax + \offset + 8]
        unpcklps xmm2, xmm3
        movss xmm4, dword ptr [rax + \offset + 16]
        movss xmm1, dword ptr [rax + \offset + 24]
        unpcklps xmm4, xmm1
        movlhps xmm2, xmm4
        movss xmm1, dword ptr [r9 + \weight]
.endif
        shufps xmm1, xmm1, 0
        mulps xmm2, xmm1
        addps xmm0, xmm2
.endif
.endif
.Ldw_skip\@:
.endm

.macro DW_WINDOW kernel, stride, fused, scalar
        mov rax, rdx
        .set .Ldw_weight\@, 0
        .rept \kernel
        .set .Ldw_offset\@, 0
        .rept \kernel
        DW_TAP .Ldw_weight\@, .Ldw_offset\@, \stride, \fused, \scalar
        .set .Ldw_weight\@, .Ldw_weight\@ + 4
        .set .Ldw_offset\@, .Ldw_offset\@ + 4
        .endr
        add rax, r10
        .endr
.endm

.macro DW_ROW kernel, stride, fused, lanes
.Ldw_vector\@:
        cmp r11d, \lanes
        jl .Ldw_tail\@
.if \fused
        vmovaps ymm0, ymm5
.else
        movaps xmm0, xmm5
.endif
        DW_WINDOW \kernel, \stride, \fused, 0
.if \fused
        vmovups ymmword ptr [r8], ymm0
.else
        movups xmmword ptr [r8], xmm0
.endif
        add rdx, 4*\lanes*\stride
        add r8, 4*\lanes
        sub r11d, \lanes
        jmp .Ldw_vector\@
.Ldw_tail\@:
        test r11d, r11d
        jle .Ldw_done\@
.if \fused
        vmovaps xmm0, xmm5
.else
        movaps xmm0, xmm5
.endif
        DW_WINDOW \kernel, \stride, \fused, 1
.if \fused
        vmovss dword ptr [r8], xmm0
.else
        movss dword ptr [r8], xmm0
.endif
        add rdx, 4*\stride
        add r8, 4
        dec r11d
        jmp .Ldw_tail\@
.Ldw_done\@:
.if \fused
        vzeroupper
.endif
        ret
.endm

.macro DEPTHWISE name, kernel, fused, lanes
FUNC \name
        ENDPROLOG
        mov rdx, qword ptr [rcx]
        mov r8, qword ptr [rcx + 8]
        mov r9, qword ptr [rcx + 16]
        mov r10, qword ptr [rcx + 24]
        shl r10, 2
        mov r11d, dword ptr [rcx + 32]
.if \fused
        vbroadcastss ymm5, dword ptr [rcx + 40]
.else
        movss xmm5, dword ptr [rcx + 40]
        shufps xmm5, xmm5, 0
.endif
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
