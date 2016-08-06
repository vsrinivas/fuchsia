    .text
    .globl _start
    .type _start, %function
_start:
    // Called _start(argument) with argument in %rdi.  The stack is
    // misaligned down by 8 as per the ABI, but there is no real
    // return address there.
    // Tail-call __libc_start_main(&main, stack_end, argument).
    mov %rdi, %rdx
    lea 8(%rsp), %rsi
    mov main@GOTPCREL(%rip), %rdi
    jmp *__libc_start_main@GOTPCREL(%rip)
