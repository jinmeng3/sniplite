/* Process entry point (link with -e start). There is no C runtime: the loader
 * jumps here, the program's main function runs, and its result is the process
 * exit code.
 *   default:    wWinMain(module, NULL, NULL, SW_SHOWDEFAULT)   (SnapRail.exe, setup)
 *   -DCONSOLE:  wmain(argc, argv), argv split from GetCommandLineW by the same
 *               rules as the C runtime (cmdline_split, util.c)  (SnapRail.com) */
#include "asm.inc"

#ifndef CONSOLE
FUNC start
        ALLOC 40
        ENDPROLOG
        xor ecx, ecx
        API GetModuleHandleW
        mov rcx, rax
        xor edx, edx
        xor r8d, r8d
        mov r9d, 10                     /* SW_SHOWDEFAULT */
        call wWinMain
        mov ecx, eax
        API ExitProcess
        int3
ENDF
#else
FUNC start
        ALLOC 56                        /* argc at [rsp+40] */
        ENDPROLOG
        API GetCommandLineW
        mov rcx, rax
        lea rdx, [rsp + 40]
        call cmdline_split
        mov ecx, 1                      /* out of memory: exit code 1 */
        test rax, rax
        jz 1f
        mov rdx, rax
        mov ecx, dword ptr [rsp + 40]
        call wmain
        mov ecx, eax
1:      API ExitProcess
        int3
ENDF
#endif

/* The load configuration (IMAGE_LOAD_CONFIG_DIRECTORY64, 0x140 bytes), for one field: the DLLs the program
 * imports are looked for in System32 only, not in the program's folder first (dwmapi.dll is not one of those
 * Windows always takes from System32). In start's section, so that the linker keeps it. */
        .balign 8
        .globl _load_config_used
_load_config_used:
        .long 0x140                     /* Size */
        .fill 0x4E - 4, 1, 0
        .short 0x800                    /* DependentLoadFlags: LOAD_LIBRARY_SEARCH_SYSTEM32 */
        .fill 0x140 - 0x50, 1, 0
