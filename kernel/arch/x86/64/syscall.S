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
     *  r12 - arg 7 (callee saved in user space, must be copied from stack)
     *  r13 - arg 8 (callee saved in user space, must be copied from stack)
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
    push     %gs:PERCPU_SAVED_USER_SP_OFFSET

    /* callee saved */
    push    %rbx
    push    %rbp
    push    %r14
    push    %r15

    push    %r11 /* user RFLAGS */

    /* start to push the rest of the args on the stack as per C calling convention */
    push    %rcx /* user RIP */
    push    %rax /* syscall # */
    push    %r13 /* arg 8 */
    push    %r12 /* arg 7 */

    /* fixup arg 4 into the proper register for calling convention */
    mov     %r10, %rcx

    /* call the high level routine */
    call    x86_64_syscall

    /* clean up the stack */
    pop     %r12
    pop     %r13
    add     $0x8, %rsp

    /* put everything back */
    pop     %rcx
    pop     %r11

    pop     %r15
    pop     %r14
    pop     %rbp
    pop     %rbx

    /* make sure interrupts are disabled */
    cli

    /* restore the user stack */
    pop     %rsp

    /* put the user gs back */
    swapgs

    /* This will fault if the return address is non-canonical.  See
     * docs/magenta/sysret_problem.md for how we avoid that. */
    sysretq

END(x86_syscall)

