// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>
#include <lib/code-patching/asm.h>
#include <arch/code-patches/case-id.h>
#include <lib/syscalls/zx-syscall-numbers.h>

#define DW_REG_rsp        0x7
#define DW_REG_rip        0x10

// These macros ensure the stack pointer remains 16-byte aligned.
.macro pre_push n
.if \n % 2 == 1
    push_value $0
.endif
.endm

.macro post_pop n
    add_to_sp ((\n + (\n % 2)) * 8)
.endm

#define ZERO_COMMON_UNUSED_REGISTERS \
    xorl %eax, %eax;  \
    xorl %ebx, %ebx;  \
    xorl %ebp, %ebp;  \
    xorq %r10, %r10;  \
    xorq %r11, %r11;  \
    xorq %r12, %r12;  \
    xorq %r13, %r13;  \
    xorq %r14, %r14;  \
    xorq %r15, %r15

// Macros for preparing ABI conformant calls for syscall wrappers.
// Shuffles syscall arguments into x86-64 ABI locations before
// calling C++ syscall handlers.
// Zeros unused registers to constrain speculative execution in syscall
// handlers with user-passed register values.
.macro pre_args n
.if \n > 5
    // We use the stack for arguments 6, 7, and 8.
    pre_push (\n - 5)
.endif

.if \n == 0
    // syscall_0(rip)
    //
    // rip   from rcx to rdi
    mov %rcx, %rdi
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorq %r8, %r8
    xorq %r9, %r9
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 1
    // syscall_1(arg_1, rip)
    //
    // arg_1 from rdi to rdi
    // rip   from rcx to rsi
    mov %rcx, %rsi
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorq %r8, %r8
    xorq %r9, %r9
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 2
    // syscall_2(arg_1, arg_2, rip)
    //
    // arg_1 from rdi to rdi
    // arg_2 from rsi to rsi
    // rip   from rcx to rdx
    mov %rcx, %rdx
    xorl %ecx, %ecx
    xorq %r8, %r8
    xorq %r9, %r9
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 3
    // syscall_3(arg_1, arg_2, arg_3, rip)
    //
    // arg_1 from rdi to rdi
    // arg_2 from rsi to rsi
    // arg_3 from rdx to rdx
    // rip   from rcx to rcx
    xorq %r8, %r8
    xorq %r9, %r9
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 4
    // syscall_4(arg_1, arg_2, arg_3, arg_4, rip)
    //
    // arg_1 from rdi to rdi
    // arg_2 from rsi to rsi
    // arg_3 from rdx to rdx
    // arg_4 from r10 to rcx
    // rip   from rcx to r8
    mov %rcx, %r8
    mov %r10, %rcx
    xorq %r9, %r9
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 5
    // syscall_5(arg_1, arg_2, arg_3, arg_4, arg_5, rip)
    //
    // arg_1 from rdi to rdi
    // arg_2 from rsi to rsi
    // arg_3 from rdx to rdx
    // arg_4 from r10 to rcx
    // arg_5 from r8  to r8
    // rip   from rcx to r9
    mov %rcx, %r9
    mov %r10, %rcx
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 6
    // syscall_6(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, rip)
    //
    // arg_1 from rdi to rdi
    // arg_2 from rsi to rsi
    // arg_3 from rdx to rdx
    // arg_4 from r10 to rcx
    // arg_5 from r8  to r8
    // arg_6 from r9  to r9
    // rip   from rcx to (rsp)
    push_value %rcx
    mov  %r10, %rcx
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 7
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
    push_value %rcx
    push_value %r12
    mov  %r10, %rcx
    ZERO_COMMON_UNUSED_REGISTERS
.elseif \n == 8
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
    push_value %rcx
    push_value %r13
    push_value %r12
    mov %r10, %rcx
    ZERO_COMMON_UNUSED_REGISTERS
.endif
.endm

.macro post_args n
.if \n > 5
    // We use the stack for arguments 6, 7, and 8.
    post_pop (\n - 5)
.endif

    JMP_AND_SPECULATION_POSTFENCE(x86_syscall_cleanup_and_return)
.endm

.macro cfi_outermost_frame
    // TODO(dje): IWBN to use .cfi_undefined here, but gdb didn't properly
    // handle initial attempts. Need to try again (or file gdb bug).
    cfi_register_is_zero DW_REG_rsp
    cfi_register_is_zero DW_REG_rip
.endm

// Adds a label for making the syscall and adds it to the jump table.
.macro syscall_dispatch nargs, name
    .pushsection .text.syscall-dispatch,"ax",%progbits
    .balign 16
    LOCAL_FUNCTION(x86_syscall_call_\name)
        // See x86_syscall for why this is here.
        cfi_outermost_frame
        pre_args \nargs
        call wrapper_\name
        post_args \nargs
    END_FUNCTION(x86_syscall_call_\name)
    .popsection
    .pushsection .rodata.syscall-table,"a",%progbits
        .quad x86_syscall_call_\name
    .popsection
.endm

// Adds the label for the jump table.
.macro start_syscall_dispatch
    .pushsection .rodata.syscall-table,"a",%progbits
    .balign 8
    .Lcall_wrapper_table:
    .popsection
.endm

.text

    // kernel side of the SYSCALL instruction
    // state on entry:
    // RCX holds user RIP
    // R11 holds user RFLAGS
    // RSP still holds user stack
    // CS loaded with kernel CS from IA32_STAR
    // SS loaded with kernel CS + 8 from IA32_STAR

    // args passed:
    //  rax - syscall # and return
    //  rbx - saved
    //  rcx - modified as part of syscall instruction
    //  rdx - arg 3
    //  rsi - arg 2
    //  rdi - arg 1
    //  rbp - saved
    //  rsp - saved
    //  r8  - arg 5
    //  r9  - arg 6
    //  r10 - arg 4
    //  r11 - modified as part of syscall instruction
    //  r12 - arg 7
    //  r13 - arg 8
    //  r14 - saved
    //  r15 - saved
    //
.balign 16
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

    // swap to the kernel GS register
    swapgs

    // save the user stack pointer
    mov     %rsp, %gs:PERCPU_SAVED_USER_SP_OFFSET

    // load the kernel stack pointer
    mov     %gs:PERCPU_KERNEL_SP_OFFSET, %rsp
    .cfi_def_cfa %rsp, 0

    // Save all the general purpose registers in a syscall_regs_t
    // struct on the kernel's stack.
    //
    // By saving (and later restoring) all of the registers rather than just
    // the bare minimum, we ensure that kernel data is not inadvertently
    // leaked back to user mode.
    push_value %gs:PERCPU_SAVED_USER_SP_OFFSET  // User stack
    push_value %r11  // RFLAGS
    push_value %rcx  // RIP
    push_value %r15
    push_value %r14
    push_value %r13
    push_value %r12
    push_value $0    // R11 was trashed by the syscall instruction.
    push_value %r10
    push_value %r9
    push_value %r8
    push_value %rbp
    push_value %rdi
    push_value %rsi
    push_value %rdx
    push_value $0    // RCX was trashed by the syscall instruction.
    push_value %rbx
    push_value %rax

    // At this point:
    //   rsp points at a syscall_regs_t struct
    //   rsp is 16-byte aligned
    //
    // Any changes to the stack here need to be reflected in
    // pre_push and post_pop macros above to maintain alignment.

    // check to see if we're in restricted mode
    cmpl    $0, %gs:PERCPU_IN_RESTRICTED_MODE
    jne     .Lrestricted_syscall

    // Bounds-check system call number and jump to handler.
    xorq    %r11, %r11
    cmp     $ZX_SYS_COUNT, %rax
    jae     .Lunknown_syscall
    // Spectre V1: If syscall number >= ZX_SYS_COUNT, replace it with zero. The test/branch above
    // means this can only occur in wrong-path speculative executions.  It's critical to the
    // correctness of the mitigation that the following comparison is performed using cmov rather
    // than a test / conditional branch.
    cmovge   %r11, %rax
    leaq    .Lcall_wrapper_table(%rip), %r11
    movq    (%r11,%rax,8), %r11
    // Spectre V2: Use retpoline to invoke system call handler.
    JMP_AND_SPECULATION_POSTFENCE(__x86_indirect_thunk_r11)
.Lunknown_syscall:
    mov     %rax, %rdi // move the syscall number into the 0 arg slot
    mov     %rcx, %rsi // pc into arg 1
    call    unknown_syscall
    JMP_AND_SPECULATION_POSTFENCE(x86_syscall_cleanup_and_return)

.Lrestricted_syscall:
    mov     %rsp, %rdi
    call     syscall_from_restricted
    // There is no path that returns from this call, but if it did, trap.
    ud2

END_FUNCTION(x86_syscall)

.balign 16
    // All the syscall wrapper routines return to here.
LOCAL_FUNCTION_LABEL(x86_syscall_cleanup_and_return)
    .cfi_startproc simple
    // At this point:
    //   rax = syscall result
    //   rdx = non-zero if thread was signaled
    //   rsp = address of syscall_regs_t

    // Save syscall result to the syscall_regs_t on the stack to ensure it's not trashed
    // by upcoming function calls and to ensure debuggers can see and modify it if the thread was
    // suspened.
    movq    %rax, (%rsp)

    // Move the thread-signaled indicator to a callee-saved register to ensure it's not trashed by
    // upcoming function calls.
    movq    %rdx, %r12

    // Spectre V1: If the syscall is going to return certain errors, flush the L1D$
    // TODO(fxbug.dev/33667): Can this be folded together w/ MD_CLEAR below?
    test    %rax, %rax
    jz      1f
    movq    %rax, %rdi
    call    x86_cpu_maybe_l1d_flush
1:

    // Was the thread signaled?
    test    %r12, %r12
    jnz     .Lthread_signaled

.Lreturn_from_syscall:
#if LK_DEBUGLEVEL > 2
    // Ensure that interrupts are disabled on all paths to here.
    // If they are not, enter a spinloop.
    pushf
    popq    %rax
    bt      $9, %rax  // RFLAGS.IF
0:
    jc      0b       // Loop if we found RFLAGS.IF set (interrupts enabled)
#endif

    // If we are affected by the MDS speculative execution vulnerability, flush microarchitectural
    // buffers via mds_buff_overwrite(). Patching will NOP out the flush where it is not required.
.global syscall_maybe_mds_buff_overwrite
syscall_maybe_mds_buff_overwrite:
    // Mitigates MDS/TAA bugs. See <arch/code-patches/case-id.h>
    .code_patching.start CASE_ID_MDS_TAA_MITIGATION
    call mds_buff_overwrite
    .code_patching.end

    // Restore general purpose registers just before returning.
    //
    // It is critical that all registers are reset.  The callee-saved registers must be restored per
    // the ABI.  The other registers might contain private kernel data that must not be leaked to
    // user mode.  To ensure data is not leaked in call-clobbered registers, we restored them to
    // their previous values.  Alternatively, we could simply zero them out to ensure data is not
    // leaked.   However, this code path is shared with the path taken by a thread returning to user
    // mode after its registers have been modified by a debugger so we restore them all to keep it
    // simple (except for RCX and R11 which are clobbered by the SYSRET instruction).
    //
    // TODO(fxbug.dev/62790): Make the restored register state completely capture the thread's state
    // and make syscalls act more like atomic instructions.
    pop_value %rax
    pop_value %rbx
    pop_value %rcx  // Will be overwritten with RIP later on.
    pop_value %rdx
    pop_value %rsi
    pop_value %rdi
    pop_value %rbp
    pop_value %r8
    pop_value %r9
    pop_value %r10
    pop_value %r11  // Will be overwritten with RFLAGS later on.
    pop_value %r12
    pop_value %r13
    pop_value %r14
    pop_value %r15
    pop_value %rcx  // RIP
    pop_value %r11  // RFLAGS
    pop_value %rsp  // User stack

    // put the user gs back
    swapgs

    // This will fault if the return address is non-canonical.  See
    // docs/sysret_problem.md for how we avoid that.
    sysretq

.Lthread_signaled:
    // re-enable interrupts to maintain kernel preemptiveness
    sti

    // Pass a pointer to the syscall_regs_t struct as first arg.
    movq    %rsp, %rdi
    call    x86_syscall_process_pending_signals

    cli
    JMP_AND_SPECULATION_POSTFENCE(.Lreturn_from_syscall)
END_FUNCTION(x86_syscall_cleanup_and_return)

// One of these macros is invoked by kernel.inc for each syscall.

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  syscall_dispatch nargs, name
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

start_syscall_dispatch

#include <lib/syscalls/kernel.inc>

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL
