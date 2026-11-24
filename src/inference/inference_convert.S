/* Binary16 to binary32, disjoint buffers; AVX + F16C required by the caller.
 * Match GGML's round-down +0 -> -0 behavior and signaling-NaN status flags.
 */
#include "asm.inc"

.macro HALF_ZERO_FIX width
        vcmpps \width\()2, \width\()0, \width\()1, 0
        vandps \width\()2, \width\()2, \width\()3
        vorps \width\()0, \width\()0, \width\()2
.endm

.macro HALF_ROW down
.Lhalf_eight\@:
        cmp r8, 8
        jb .Lhalf_four\@
        vcvtph2ps ymm0, xmmword ptr [rcx]
.if \down
        HALF_ZERO_FIX ymm
.endif
        vmovups ymmword ptr [rdx], ymm0
        add rcx, 16
        add rdx, 32
        sub r8, 8
        jmp .Lhalf_eight\@
.Lhalf_four\@:
        cmp r8, 4
        jb .Lhalf_tail\@
        vcvtph2ps xmm0, qword ptr [rcx]
.if \down
        HALF_ZERO_FIX xmm
.endif
        vmovups xmmword ptr [rdx], xmm0
        add rcx, 8
        add rdx, 16
        sub r8, 4
.Lhalf_tail\@:
        test r8, r8
        jz .Lhalf_done\@
.Lhalf_one\@:
        movzx eax, word ptr [rcx]
        vmovd xmm0, eax
        vcvtph2ps xmm0, xmm0
.if \down
        HALF_ZERO_FIX xmm
.endif
        vmovss dword ptr [rdx], xmm0
        add rcx, 2
        add rdx, 4
        dec r8
        jnz .Lhalf_one\@
.Lhalf_done\@:
        vzeroupper
        ret
.endm

FUNC sniplite_fp16_to_fp32_f16c
        ENDPROLOG
        test r8, r8
        jz .Lhalf_empty
        stmxcsr dword ptr [rsp + 8]
        mov eax, dword ptr [rsp + 8]
        and eax, 0x6000
        cmp eax, 0x2000
        je .Lhalf_down
        HALF_ROW 0
.Lhalf_down:
        vxorps ymm1, ymm1, ymm1
        vmovups ymm3, ymmword ptr [rip + .Lhalf_sign]
        HALF_ROW 1
.Lhalf_empty:
        ret
ENDF

.section .rdata,"dr"
.p2align 5
.Lhalf_sign:
        .long 0x80000000, 0x80000000, 0x80000000, 0x80000000
        .long 0x80000000, 0x80000000, 0x80000000, 0x80000000
