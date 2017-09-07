// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>
#include <magenta/mx-syscall-numbers.h>

#define DW_REG_rsp        0x7
#define DW_REG_rip        0x10

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
    push_value %rcx
    push_value %r13
    push_value %r12

    /* move arg 4 into the proper register for calling convention */
    mov      %r10, %rcx
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
    push_value %rcx
    push_value %r12
    mov      %r10, %rcx
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
    push_value %rcx
    mov      %r10, %rcx
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
    push_value $0
.endif
.endm

.macro post_pop n
.if \n % 2 == 0
    add_to_sp ((\n + 1) * 8)
.else
    add_to_sp (\n * 8)
.endif
.endm

.macro cfi_outermost_frame
    // TODO(dje): IWBN to use .cfi_undefined here, but gdb didn't properly
    // handle initial attempts. Need to try again (or file gdb bug).
    cfi_register_is_zero DW_REG_rsp
    cfi_register_is_zero DW_REG_rip
.endm

// Adds a label for making the syscall and adds it to the jump table.
.macro syscall_dispatch nargs, syscall
    .pushsection .text.syscall-dispatch,"ax",%progbits
    LOCAL_FUNCTION(.Lcall_\syscall\())
        // See x86_syscall for why this is here.
        cfi_outermost_frame
        pre_\nargs\()_args
        call wrapper_\syscall
        post_\nargs\()_args
    END_FUNCTION(.Lcall_\syscall\())
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
FUNCTION_LABEL(x86_syscall)
    .cfi_startproc simple
    // CFI tracking here doesn't (currently) try to support backtracing from
    // kernel space to user space. This is left for later. For now just say
    // %rsp and %rip of the previous frame are zero, mark all the other
    // registers as undefined, and have all register push/pop just specify
    // stack adjustments and not how to find the register's value.
    cfi_outermost_frame
    // The default for caller-saved regs is "undefined", but for completeness
    // sake mark them all as undefined.
    ALL_CFI_UNDEFINED

    /* swap to the kernel GS register */
    swapgs

    /* save the user stack pointer */
    mov     %rsp, %gs:PERCPU_SAVED_USER_SP_OFFSET

    /* load the kernel stack pointer */
    mov     %gs:PERCPU_KERNEL_SP_OFFSET, %rsp
    .cfi_def_cfa %rsp, 0

    /* save away the user stack pointer */
    push_value %gs:PERCPU_SAVED_USER_SP_OFFSET

    push_value %r11 /* user RFLAGS */
    push_value %rcx /* user RIP */

    // Any changes to the stack here need to be reflected in
    // pre_push and post_pop macros above to maintain alignment.
#if WITH_LIB_SYSCALLS
    // Verify the syscall is in range and jump to it.
    cmp     $MX_SYS_COUNT, %rax
    jae     .Lunknown_syscall
    jmp     *.Lcall_wrapper_table(, %rax, 8)
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
    pop_value %rcx /* user RIP */
    pop_value %r11 /* user RFLAGS */

    /* zero out trashed arg registers */
    xorl    %edi, %edi
    xorl    %esi, %esi
    /* Don't zero %rdx yet -- it contains the "is_signaled" indicator */
    xorl    %r10d, %r10d
    xorl    %r8d, %r8d
    xorl    %r9d, %r9d

    cmp     $0, %rdx
    jnz     .Lthread_signaled

    /*xor     %rdx, %rdx - already zero */

.Lreturn_from_syscall:

    /* make sure interrupts are disabled (they already are in the fall-through
       path, but if we took the .Lthread_signaled path they aren't) */
    cli

    /* restore the user stack */
    pop_value %rsp

    /* put the user gs back */
    swapgs

    /* This will fault if the return address is non-canonical.  See
     * docs/sysret_problem.md for how we avoid that. */
    sysretq

.Lthread_signaled:
    /* re-enable interrupts to maintain kernel preemptiveness */
    sti

    /* fill in x86_syscall_general_regs_t
       Because we don't save the regs unless we have to a lot of the original
       values are gone. The user just has to deal with it. One important thing
       to do here is not leak kernel values to userspace. */
    movq    (%rsp), %rdi /* user rsp */
    push_value %r11 /* rflags */
    push_value %rcx /* rip */
    push_value %r15
    push_value %r14
    push_value %r13
    push_value %r12
    push_value %r11
    push_value %r10
    push_value %r9
    push_value %r8
    push_value %rdi /* rsp */
    push_value %rbp
    push_value $0
    push_value %rsi
    push_value $0 /* instead of signaled flag */
    push_value %rcx
    push_value %rbx
    push_value %rax

    movq    %rsp, %rdi
    call    x86_syscall_process_pending_signals

    pop_value %rax
    pop_value %rbx
    pop_value %rcx
    pop_value %rdx
    pop_value %rsi
    pop_value %rdi
    pop_value %rbp
    pop_value %r8 /* discard any changed %rsp value - TODO(dje): check ok */
    pop_value %r8
    pop_value %r9
    pop_value %r10
    pop_value %r11
    pop_value %r12
    pop_value %r13
    pop_value %r14
    pop_value %r15
    pop_value %rcx
    pop_value %r11
    jmp     .Lreturn_from_syscall

END_FUNCTION(x86_syscall)

#if WITH_LIB_SYSCALLS
#include <magenta/syscall-kernel-branches.S>
#endif
