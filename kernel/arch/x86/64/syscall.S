// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>

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

    /* start to push the rest of the args on the stack as per C calling convention */
    push    %rcx /* user RIP */
    push    %rax /* syscall # */
    push    %r13 /* arg 8 */
    push    %r12 /* arg 7 */

    /* move arg 4 into the proper register for calling convention */
    mov     %r10, %rcx

    /* call the high level routine */
    call    x86_64_syscall

    /* at this point:
       rax = syscall result
       rdx = non-zero if thread was signaled */

    /* skip some of the arguments we pushed on the stack (rax, r13 and r12).
     * r12 and r13 are callee-saved, so they won't have been clobbered. */
    add     $8 * 3, %rsp

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

