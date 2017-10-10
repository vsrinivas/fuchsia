    .text
    .p2align 2
    .globl _start
    .type _start, %function
_start:
    // Called _start(argument) with argument in x0.
    // Tail-call __libc_start_main(argument, &main).
    adrp x16, :got:__libc_start_main
    adrp x17, :got:main
    ldr x16, [x16, #:got_lo12:__libc_start_main]
    ldr x1, [x17, #:got_lo12:main]
    br x16
