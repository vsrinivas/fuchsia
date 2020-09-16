// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_thread.h>
#include <arch/arm64.h>
#include <arch/arm64/asm.h>
#include <arch/asm_macros.h>
#include <lib/syscalls/arm64.h>
#include <lib/syscalls/zx-syscall-numbers.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif

.section .text.boot.vectab,"ax",@progbits
.align 12

#define DW_REG_lr   30
#define DW_REG_sp   31
// The "current mode exception link register", which for our purposes is elr_el1.
#define DW_REG_ELR_mode 33

#define lr x30
#define elr1 DW_REG_ELR_mode

// offset where sp,elr,spsr,lr,mdscr goes in the iframe
#define regsave_special_reg_offset (30 * 8)
// offset where ELR_EL1 is restored from
#define regsave_elr_el1_offset (regsave_special_reg_offset + 16)

.macro regsave
// There are 5 regs @ regsave_special_reg_offset, plus one unused space = 6 to
// maintain 16-byte padding.
sub_from_sp (6*8)
push_regs x28, x29
push_regs x26, x27
push_regs x24, x25
push_regs x22, x23
push_regs x20, x21
push_regs x18, x19
push_regs x16, x17
push_regs x14, x15
push_regs x12, x13
push_regs x10, x11
push_regs x8, x9
push_regs x6, x7
push_regs x4, x5
push_regs x2, x3
push_regs x0, x1
// Preserve x0-x7 for syscall arguments
mrs  x9, sp_el0
// x10 (containing elr_el1) is used in syscall handler
mrs  x10, elr_el1
mrs  x11, spsr_el1
mrs  x12, mdscr_el1
stp  lr, x9, [sp, #regsave_special_reg_offset]
.cfi_rel_offset lr, (regsave_special_reg_offset)
.cfi_rel_offset sp, (regsave_special_reg_offset + 8)
stp  x10, x11, [sp, #regsave_special_reg_offset + 16]
.cfi_rel_offset elr1, (regsave_special_reg_offset + 16)
str x12, [sp, #regsave_special_reg_offset + 32]
.endm

// Once we pop the stack past the saved sp_el0, elr_el1 the userspace values
// are inaccessible.
.macro mark_lr_sp_inaccessible
// TODO(dje): gdb tries to use some value for these even if "undefined",
// as a workaround set their values to zero which will cause gdb to
// terminate the backtrace. Need to revisit, file gdb bug if necessary.
cfi_register_is_zero DW_REG_sp
cfi_register_is_zero DW_REG_ELR_mode
.endm

.macro regrestore
// Preserve x0-x1 for syscall returns (eventually x0-x7)
ldp  lr, x9, [sp, #regsave_special_reg_offset]
.cfi_same_value lr
ldp  x10, x11, [sp, #regsave_special_reg_offset + 16]
ldr  x12, [sp, #regsave_special_reg_offset + 32]
msr  sp_el0, x9
msr  elr_el1, x10
msr  spsr_el1, x11
msr  mdscr_el1, x12
pop_regs x0, x1
pop_regs x2, x3
pop_regs x4, x5
pop_regs x6, x7
pop_regs x8, x9
pop_regs x10, x11
pop_regs x12, x13
pop_regs x14, x15
pop_regs x16, x17
pop_regs x18, x19
pop_regs x20, x21
pop_regs x22, x23
pop_regs x24, x25
pop_regs x26, x27
pop_regs x28, x29
add_to_sp (6*8)
mark_lr_sp_inaccessible
.endm

.macro start_isr_cfi
    .cfi_startproc simple
    .cfi_signal_frame
    // The return address is in elr_el1, not lr.
    .cfi_return_column elr1
    .cfi_def_cfa sp, 0
.endm

.macro start_isr_func_cfi
    start_isr_cfi
    ALL_CFI_SAME_VALUE
    .cfi_undefined elr1
.endm

.macro start_helper_cfi
    .cfi_startproc simple
    .cfi_signal_frame
    .cfi_def_cfa sp, (regsave_special_reg_offset + 4 * 8)
.endm

// The CFA offset of integer register |regno| (regno = 0-29).
#define REG_CFA_OFFSET(regno) .cfi_offset x##regno, -((4 * 8) + ((30 - (regno)) * 8))

// Mark the locations of the registers based on the CFA so that the
// location doesn't change as the regs are popped.
.macro setup_helper_cfi
    REG_CFA_OFFSET(0)
    REG_CFA_OFFSET(1)
    REG_CFA_OFFSET(2)
    REG_CFA_OFFSET(3)
    REG_CFA_OFFSET(4)
    REG_CFA_OFFSET(5)
    REG_CFA_OFFSET(6)
    REG_CFA_OFFSET(7)
    REG_CFA_OFFSET(8)
    REG_CFA_OFFSET(9)
    REG_CFA_OFFSET(10)
    REG_CFA_OFFSET(11)
    REG_CFA_OFFSET(12)
    REG_CFA_OFFSET(13)
    REG_CFA_OFFSET(14)
    REG_CFA_OFFSET(15)
    REG_CFA_OFFSET(16)
    REG_CFA_OFFSET(17)
    REG_CFA_OFFSET(18)
    REG_CFA_OFFSET(19)
    REG_CFA_OFFSET(20)
    REG_CFA_OFFSET(21)
    REG_CFA_OFFSET(22)
    REG_CFA_OFFSET(23)
    REG_CFA_OFFSET(24)
    REG_CFA_OFFSET(25)
    REG_CFA_OFFSET(26)
    REG_CFA_OFFSET(27)
    REG_CFA_OFFSET(28)
    REG_CFA_OFFSET(29)
    .cfi_offset sp, -(3 * 8)
    .cfi_offset lr, -(4 * 8)
.endm

.macro start_helper
    start_helper_cfi
    setup_helper_cfi
.endm

// All normal C code in the kernel expects the invariants that the fixed
// registers assigned to the percpu_ptr and the shadow-call-stack pointer have
// the correct values for the current CPU and kernel thread.  When an exception
// happens in the kernel, only percpu_ptr needs to be reloaded. (In fact, it
// would be disastrous to reload the shadow-call-stack pointer because the
// correct value to reflect the interrupted thread's kernel call stack exists
// only in the register!) But when an exception happens in a lower EL (i.e. user
// mode), these registers must be reloaded from the struct arch_thread
// accessible via TPIDR_EL1 before reaching any C functions.
.macro restore_fixed_regs temp
    mrs \temp, tpidr_el1
#if __has_feature(shadow_call_stack)
# if CURRENT_SCSP_OFFSET != CURRENT_PERCPU_PTR_OFFSET + 8
#  error "shadow_call_sp must follow current_percpu_ptr in struct arch_thread"
# endif
    ldp percpu_ptr, shadow_call_sp, [\temp, #CURRENT_PERCPU_PTR_OFFSET]
#else
    ldr percpu_ptr, [\temp, #CURRENT_PERCPU_PTR_OFFSET]
#endif
.endm

// EL1-to-EL1 exceptions require reloading percpu_ptr, as SMCCC does not
// preserve this fixed register and an exception may pend during an SMC/HVC
// call, leaving the register value unpredictable as the handler is entered.
.macro restore_percpu_ptr temp
    mrs \temp, tpidr_el1
    ldr percpu_ptr, [\temp, #CURRENT_PERCPU_PTR_OFFSET]
.endm

// The shadow-call-stack pointer (x18) is saved/restored in struct arch_thread
// on context switch.  On entry from a lower EL (i.e. user mode), it gets
// reloaded from there via the restore_fixed_regs macro above.  So when
// returning to user mode, we must make sure to write back the current value
// (which should always be the base, since returning to user should be the
// base of the call stack) so that the next kernel entry reloads that instead
// of whatever was current last time this thread context-switched out.
.macro save_shadow_call_sp temp
#if __has_feature(shadow_call_stack)
    mrs \temp, tpidr_el1
    str shadow_call_sp, [\temp, #CURRENT_SCSP_OFFSET]
#endif
.endm

.macro invalid_exception, which
    start_isr_func_cfi
    regsave
    restore_fixed_regs x9
    mov x1, #\which
    mov x0, sp
    bl  arm64_invalid_exception
    b   .
.endm

.macro irq_exception, exception_flags
    start_isr_func_cfi
    regsave
    msr daifclr, #1 /* reenable fiqs once elr and spsr have been saved */
    mov x0, sp
    mov x1, #\exception_flags
.if (\exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)
    restore_fixed_regs x9
.else
    restore_percpu_ptr x9
.endif
    bl  arm64_irq
    msr daifset, #1 /* disable fiqs to protect elr and spsr restore */
.if (\exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)
    b   arm64_exc_shared_restore_lower_el
.else
    b   arm64_exc_shared_restore
.endif
.endm

.macro sync_exception, exception_flags, from_lower_el_64=0
    start_isr_func_cfi
    regsave
    mrs x9, esr_el1
.if \from_lower_el_64
    // If this is a syscall, x0-x7 contain args and x16 contains syscall num.
    // x10 contains elr_el1.
    lsr x11, x9, #26              // shift esr right 26 bits to get ec
    cmp x11, #0x15                // check for 64-bit syscall
    beq arm64_syscall_dispatcher  // and jump to syscall handler
.else
    restore_percpu_ptr x11
.endif
    // Prepare the default sync_exception args
    mov w2, w9
    mov x1, #\exception_flags
.if (\exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)
    b arm64_sync_exception_lower_el
.else
    mov x0, sp
    bl arm64_sync_exception
    b  arm64_exc_shared_restore
.endif
.endm

FUNCTION_LABEL(arm64_el1_exception_base)

/* exceptions from current EL, using SP0 */
.org 0x000
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_current_el_SP0)
    invalid_exception 0
END_FUNCTION(arm64_el1_sync_exc_current_el_SP0)

.org 0x080
LOCAL_FUNCTION_LABEL(arm64_el1_irq_current_el_SP0)
    invalid_exception 1
END_FUNCTION(arm64_el1_irq_current_el_SP0)

.org 0x100
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_current_el_SP0)
    invalid_exception 2
END_FUNCTION(arm64_el1_fiq_current_el_SP0)

.org 0x180
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_current_el_SP0)
    invalid_exception 3
END_FUNCTION(arm64_el1_err_exc_current_el_SP0)

/* exceptions from current EL, using SPx */
.org 0x200
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_current_el_SPx)
    sync_exception 0 /* same EL, arm64 */
END_FUNCTION(arm64_el1_sync_exc_current_el_SPx)

.org 0x280
LOCAL_FUNCTION_LABEL(arm64_el1_irq_current_el_SPx)
    irq_exception 0 /* same EL, arm64 */
END_FUNCTION(arm64_el1_irq_current_el_SPx)

.org 0x300
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_current_el_SPx)
    start_isr_func_cfi
    regsave
    restore_percpu_ptr x9
    mov x0, sp
    bl  platform_fiq
    b  arm64_exc_shared_restore
END_FUNCTION(arm64_el1_fiq_current_el_SPx)

.org 0x380
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_current_el_SPx)
    invalid_exception 0x13
END_FUNCTION(arm64_el1_err_exc_current_el_SPx)

/* exceptions from lower EL, running arm64 */
.org 0x400
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_64)
    sync_exception (ARM64_EXCEPTION_FLAG_LOWER_EL), 1
END_FUNCTION(arm64_el1_sync_exc_lower_el_64)

.org 0x480
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_64)
    irq_exception (ARM64_EXCEPTION_FLAG_LOWER_EL)
END_FUNCTION(arm64_el1_irq_lower_el_64)

.org 0x500
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_lower_el_64)
    start_isr_func_cfi
    regsave
    restore_fixed_regs x9
    mov x0, sp
    bl  platform_fiq
    b  arm64_exc_shared_restore_lower_el
END_FUNCTION(arm64_el1_fiq_lower_el_64)

.org 0x580
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_64)
    invalid_exception 0x23
END_FUNCTION(arm64_el1_err_exc_lower_el_64)

/* exceptions from lower EL, running arm32 */
.org 0x600
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_32)
    sync_exception (ARM64_EXCEPTION_FLAG_LOWER_EL|ARM64_EXCEPTION_FLAG_ARM32)
END_FUNCTION(arm64_el1_sync_exc_lower_el_32)

.org 0x680
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_32)
    irq_exception (ARM64_EXCEPTION_FLAG_LOWER_EL|ARM64_EXCEPTION_FLAG_ARM32)
END_FUNCTION(arm64_el1_irq_lower_el_32)

.org 0x700
LOCAL_FUNCTION(arm64_el1_fiq_lower_el_32)
    b arm64_el1_fiq_lower_el_64
END_FUNCTION(arm64_el1_fiq_lower_el_32)

.org 0x780
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_32)
    invalid_exception 0x33
END_FUNCTION(arm64_el1_err_exc_lower_el_32)

LOCAL_FUNCTION_LABEL(arm64_sync_exception_lower_el)
    start_isr_func_cfi
    mov x0, sp
    restore_fixed_regs x9
    bl  arm64_sync_exception
    b arm64_exc_shared_restore_lower_el
END_FUNCTION(arm64_sync_exception_lower_el)

// CFI is independent of any other concept of function boundary.
// Everything in start_helper applies the same whether entering at
// the first or the second entry point.  The first entry point is used
// when returning to user mode, the second when returning to kernel.
LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore_lower_el)
    start_helper
    save_shadow_call_sp x9
    // Fall through to join the same-EL case.
LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore)
    regrestore
    eret
    // Prevent speculation through ERET
    SPECULATION_POSTFENCE
END_FUNCTION(arm64_exc_shared_restore)
END_DATA(arm64_exc_shared_restore_lower_el)

//
// Syscall args are in x0-x7 already.
// pc is in x10 and needs to go in the next available register,
// or the stack if the regs are full.
//
.macro syscall_dispatcher nargs, syscall
.balign 16
.if \nargs == 8
    push_regs x10, x11 // push twice to maintain alignment
    bl  wrapper_\syscall
    pop_regs x10, x11
.else
    mov x\nargs, x10
    bl  wrapper_\syscall
.endif
    b .Lpost_syscall
.endm

//
// Expected state prior to arm64_syscall_dispatcher branch...
//
// x0-x7 - contains syscall arguments
// x9    - contains esr_el1
// x10   - contains elr_el1
// x16   - contains syscall_num
// sp    - points to base of frame (frame->r[0])
//
// Expected state prior to branching to syscall_dispatcher macro
//
// x0-x7  - contains syscall arguments
// x10    - contains userspace pc
//
LOCAL_FUNCTION_LABEL(arm64_syscall_dispatcher)
    start_isr_func_cfi
    restore_fixed_regs x11
    // Verify syscall number and call the unknown handler if bad.
    cmp  x16, #ZX_SYS_COUNT
    bhs  .Lunknown_syscall
    // Spectre V1: If syscall number >= ZX_SYS_COUNT, replace it with zero. The branch/test above
    // means this can only occur in wrong-path speculative executions.
    csel x16, xzr, x16, hs
    csdb
    // Jump to the right syscall wrapper. The syscall table is an
    // array of 16 byte aligned routines for each syscall. Each routine
    // marshalls some arguments, bls to the routine, and then branches
    // back to .Lpost_syscall (see syscall_dispatcher macro above).
    adr  x12, .Lsyscall_table
    add  x12, x12, x16, lsl #4
    br   x12
    // Prevent speculation through BR
    SPECULATION_POSTFENCE

.Lunknown_syscall:
    mov x0, x16 // move the syscall number into the 0 arg slot
    mov x1, x10 // pc into arg 1
    bl  unknown_syscall
    // fall through

.Lpost_syscall:
    // Upon return from syscall, x0 = status, x1 = thread signalled
    // Move the status to frame->r[0] for return to userspace.
    str  x0, [sp]
    // Spectre: ARM64 CPUs may speculatively execute instructions after an SVC instruction.
    // The userspace entry code has a speculation barrier; advance ELR_EL1 past in on the return
    // since it has already done its job.
.ifne ARM64_SYSCALL_SPECULATION_BARRIER_SIZE - 8
.error "Syscall speculation barrier must be 8 bytes"
.endif
    ldr  x10, [sp, #(regsave_elr_el1_offset)]
    add  x10, x10, ARM64_SYSCALL_SPECULATION_BARRIER_SIZE
    str  x10, [sp, #(regsave_elr_el1_offset)]
    // Check for pending signals. If none, just return.
    cbz  x1, arm64_exc_shared_restore_lower_el
    mov  x0, sp
    bl   arch_iframe_process_pending_signals
    b    arm64_exc_shared_restore_lower_el

// Emit a small trampoline to branch to the wrapper routine for the syscall.
.macro syscall_dispatch nargs, syscall
    syscall_dispatcher \nargs, \syscall
.endm

// Adds the label for the jump table.
.macro start_syscall_dispatch
    .balign 16
    .Lsyscall_table:
.endm

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

END_FUNCTION(arm64_syscall_dispatcher)
