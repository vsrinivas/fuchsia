    .syntax unified
    .arch armv7-a
#ifdef __thumb__
    .thumb
#define PC_OFS 4
#else
    .arm
#define PC_OFS 8
#endif
    .text
    .p2align 2
    .globl _start
    .type _start, %function
_start:
    // Called _start(argument) with argument in r0.
    // Tail-call __libc_start_main(&main, stack_end, argument).
    ldr ip, .L__libc_start_main
    movw r3, #:lower16:_GLOBAL_OFFSET_TABLE_-(.LPIC0+PC_OFS)
    mov r2, r0
    ldr r0, .Lmain
    movt r3, #:upper16:_GLOBAL_OFFSET_TABLE_-(.LPIC0+PC_OFS)
    mov r1, sp
.LPIC0:
    add r3, pc
    ldr ip, [r3, ip]
    ldr r0, [r3, r0]
    bx ip

    .p2align 2
.Lmain:
    .word main(GOT)
.L__libc_start_main:
    .word __libc_start_main(GOT)
