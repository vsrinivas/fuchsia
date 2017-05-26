// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>
#include <magenta/mx-syscall-numbers.h>

//
// Macros for preparing ABI conformant calls for syscall wrappers.
//
// syscall_8(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// arg_4 from r10 to rcx
// arg_5 from r8  to r8
// arg_6 from r9  to r9
// arg_7 from r12 to (%rsp)
// arg_8 from r13 to 8(%rsp)
// rip   from rcx to 16(%rsp)
//
.macro pre_8_args
    pre_push 3
    push    %rcx
    push    %r13
    push    %r12

    /* move arg 4 into the proper register for calling convention */
    mov     %r10, %rcx
.endm

.macro post_8_args
    post_pop 3
    jmp     .Lcleanup_and_return
.endm

//
// syscall_7(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// arg_4 from r10 to rcx
// arg_5 from r8  to r8
// arg_6 from r9  to r9
// arg_7 from r12 to (rsp)
// rip   from rcx to 8(rsp)
//
.macro pre_7_args
    pre_push 2
    push    %rcx
    push    %r12
    mov     %r10, %rcx
.endm

.macro post_7_args
    post_pop 2
    jmp     .Lcleanup_and_return
.endm

//
// syscall_6(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// arg_4 from r10 to rcx
// arg_5 from r8  to r8
// arg_6 from r9  to r9
// rip   from rcx to (rsp)
//
.macro pre_6_args
    pre_push 1
    push    %rcx
    mov     %r10, %rcx
.endm

.macro post_6_args
    post_pop 1
    jmp     .Lcleanup_and_return
.endm

//
// syscall_5(arg_1, arg_2, arg_3, arg_4, arg_5, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// arg_4 from r10 to rcx
// arg_5 from r8  to r8
// rip   from rcx to r9
//
.macro pre_5_args
    pre_push 0
    mov     %rcx, %r9
    mov     %r10, %rcx
.endm

.macro post_5_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm

//
// syscall_4(arg_1, arg_2, arg_3, arg_4, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// arg_4 from r10 to rcx
// rip   from rcx to r8
//
.macro pre_4_args
    pre_push 0
    mov     %rcx, %r8
    mov     %r10, %rcx
.endm

.macro post_4_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm

//
// syscall_3(arg_1, arg_2, arg_3, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// arg_3 from rdx to rdx
// rip   from rcx to rcx
//
.macro pre_3_args
    pre_push 0
.endm

.macro post_3_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm

//
// syscall_2(arg_1, arg_2, rip)
//
// arg_1 from rdi to rdi
// arg_2 from rsi to rsi
// rip   from rcx to rdx
//
.macro pre_2_args
    pre_push 0
    mov     %rcx, %rdx
.endm

.macro post_2_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm

//
// syscall_1(arg_1, rip)
//
// arg_1 from rdi to rdi
// rip   from rcx to rsi
//
.macro pre_1_args
    pre_push 0
    mov    %rcx, %rsi
.endm

.macro post_1_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm


//
// syscall_0(rip)
//
// rip   from rcx to rdi
//
.macro pre_0_args
    pre_push 0
    mov    %rcx, %rdi
.endm

.macro post_0_args
    post_pop 0
    jmp     .Lcleanup_and_return
.endm

// x86_syscall (below) leaves the stack misaligned by 8, so the macros
// need to account for that.
.macro pre_push n
.if \n % 2 == 0
    push    $0
.endif
.endm

.macro post_pop n
.if \n % 2 == 0
    add     $(\n + 1) * 8, %rsp
.else
    add     $\n * 8, %rsp
.endif
.endm


// Adds a label for making the syscall and adds it to the jump table.
.macro syscall_dispatch nargs, syscall
    .pushsection .text.syscall-dispatch,"ax",%progbits
    .Lcall_\syscall\():
        pre_\nargs\()_args
        call wrapper_\syscall
        post_\nargs\()_args
    .popsection
    .pushsection .rodata.syscall-table,"a",%progbits
        .quad .Lcall_\syscall
    .popsection
.endm

// Adds the label for the jump table.
.macro start_syscall_dispatch
    .pushsection .rodata.syscall-table,"a",%progbits
    .Lcall_wrapper_table:
    .popsection
.endm

.text

    /* kernel side of the SYSCALL instruction
     * state on entry:
     * RCX holds user RIP
     * R11 holds user RFLAGS
     * RSP still holds user stack
     * CS loaded with kernel CS from IA32_STAR
     * SS loaded with kernel CS + 8 from IA32_STAR

     * args passed:
     *  rax - syscall # and return
     *  rbx - saved
     *  rcx - modified as part of syscall instruction
     *  rdx - arg 3
     *  rdi - arg 1
     *  rsi - arg 2
     *  rbp - saved
     *  rsp - saved
     *  r8  - arg 5
     *  r9  - arg 6
     *  r10 - arg 4
     *  r11 - modified as part of syscall instruction
     *  r12 - arg 7
     *  r13 - arg 8
     *  r14 - saved
     *  r15 - saved
     */
FUNCTION(x86_syscall)
    /* swap to the kernel GS register */
    swapgs

    /* save the user stack pointer */
    mov     %rsp, %gs:PERCPU_SAVED_USER_SP_OFFSET

    /* load the kernel stack pointer */
    mov     %gs:PERCPU_KERNEL_SP_OFFSET, %rsp

    /* save away the user stack pointer */
    push    %gs:PERCPU_SAVED_USER_SP_OFFSET

    push    %r11 /* user RFLAGS */
    push    %rcx /* user RIP */

    // Any changes to the stack here need to be reflected in
    // pre_push and post_pop macros above to maintain alignment.
#if WITH_LIB_SYSCALLS
    // Verify the syscall is in range and jump to it.
    cmp     $MX_SYS_COUNT, %eax
    jae     .Lunknown_syscall
    jmp     *.Lcall_wrapper_table(, %rax, 8)
#include <magenta/syscall-kernel-branches.S>
#endif
.Lunknown_syscall:
    pre_0_args
    call    unknown_syscall
    post_0_args

.Lcleanup_and_return:

    /* at this point:
       rax = syscall result
       rdx = non-zero if thread was signaled */

    /* restore the registers from which SYSRET restores user state */
    pop     %rcx /* user RIP */
    pop     %r11 /* user RFLAGS */

    /* zero out trashed arg registers */
    xor     %rdi, %rdi
    xor     %rsi, %rsi
    /*xor     %rdx, %rdx - can't yet, contains "is signaled" indicator */
    xor     %r10, %r10
    xor     %r8, %r8
    xor     %r9, %r9

    cmp     $0, %rdx
    jnz     .Lthread_signaled

    /*xor     %rdx, %rdx - already zero */

.Lreturn_from_syscall:

    /* make sure interrupts are disabled (they already are in the fall-through
       path, but if we took the .Lthread_signaled path they aren't) */
    cli

    /* restore the user stack */
    pop     %rsp

    /* put the user gs back */
    swapgs

    /* This will fault if the return address is non-canonical.  See
     * docs/magenta/sysret_problem.md for how we avoid that. */
    sysretq

.Lthread_signaled:
    /* re-enable interrupts to maintain kernel preemptiveness */
    sti

    /* fill in x86_syscall_general_regs_t
       Because we don't save the regs unless we have to a lot of the original
       values are gone. The user just has to deal with it. One important thing
       to do here is not leak kernel values to userspace. */
    movq    (%rsp), %rdi /* user rsp */
    push    %r11 /* rflags */
    push    %rcx /* rip */
    push    %r15
    push    %r14
    push    %r13
    push    %r12
    push    %r11
    push    %r10
    push    %r9
    push    %r8
    push    %rdi /* rsp */
    push    %rbp
    pushq   $0
    push    %rsi
    pushq   $0 /* instead of signaled flag */
    push    %rcx
    push    %rbx
    push    %rax

    movq    %rsp, %rdi
    call    x86_syscall_process_pending_signals

    pop     %rax
    pop     %rbx
    pop     %rcx
    pop     %rdx
    pop     %rsi
    pop     %rdi
    pop     %rbp
    pop     %r8 /* discard any changed %rsp value - TODO(dje): check ok */
    pop     %r8
    pop     %r9
    pop     %r10
    pop     %r11
    pop     %r12
    pop     %r13
    pop     %r14
    pop     %r15
    pop     %rcx
    pop     %r11
    jmp     .Lreturn_from_syscall

END(x86_syscall)
