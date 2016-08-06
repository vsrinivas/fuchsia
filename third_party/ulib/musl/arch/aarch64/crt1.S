    .text
    .p2align 2
    .globl _start
    .type _start, %function
_start:
    // Called _start(argument) with argument in x0.
    // Tail-call __libc_start_main(&main, stack_end, argument).
    adrp x16, :got:__libc_start_main
    adrp x17, :got:main
    mov x2, x0
    ldr x16, [x16, #:got_lo12:__libc_start_main]
    ldr x0, [x17, #:got_lo12:main]
    mov x1, sp
    br x16
