// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_thread.h>
#include <arch/regs.h>
#include <arch/arm64.h>
#include <arch/arm64/asm.h>
#include <arch/asm_macros.h>
#include <lib/syscalls/arm64.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <zircon/compiler.h>

.text

#define DW_REG_lr   30
#define DW_REG_sp   31
// The "current mode exception link register", which for our purposes is elr_el1.
#define DW_REG_ELR_mode 33

#define lr x30
#define elr1 DW_REG_ELR_mode

// Save the full register state on the stack.
// NOTE: Must stay in sync with the iframe_t definition in arch/regs.h
.macro regsave
// The first push decrements the SP as a side effect. In the case of a stack
// overflow will result in an immediate fault, instead of 'walking the stack'
// downwards in an exception loop if the sp were decremented first.
stp   x0, x1, [sp, -ARM64_IFRAME_SIZE]!
.cfi_adjust_cfa_offset -ARM64_IFRAME_SIZE
stp   x2, x3, [sp, ARM64_IFRAME_OFFSET_R + (2 * 8)]
stp   x4, x5, [sp, ARM64_IFRAME_OFFSET_R + (4 * 8)]
stp   x6, x7, [sp, ARM64_IFRAME_OFFSET_R + (6 * 8)]
stp   x8, x9, [sp, ARM64_IFRAME_OFFSET_R + (8 * 8)]
stp   x10, x11, [sp, ARM64_IFRAME_OFFSET_R + (10 * 8)]
stp   x12, x13, [sp, ARM64_IFRAME_OFFSET_R + (12 * 8)]
stp   x14, x15, [sp, ARM64_IFRAME_OFFSET_R + (14 * 8)]
stp   x16, x17, [sp, ARM64_IFRAME_OFFSET_R + (16 * 8)]
stp   x18, x19, [sp, ARM64_IFRAME_OFFSET_R + (18 * 8)]
stp   x20, x21, [sp, ARM64_IFRAME_OFFSET_R + (20 * 8)]
stp   x22, x23, [sp, ARM64_IFRAME_OFFSET_R + (22 * 8)]
stp   x24, x25, [sp, ARM64_IFRAME_OFFSET_R + (24 * 8)]
stp   x26, x27, [sp, ARM64_IFRAME_OFFSET_R + (26 * 8)]
stp   x28, x29, [sp, ARM64_IFRAME_OFFSET_R + (28 * 8)]
// Stay away from x0-x7, since they may be holding syscall arguments.
mrs   x9, sp_el0
// x10 (containing elr_el1) is used in syscall handler
mrs   x10, elr_el1
mrs   x11, spsr_el1
mrs   x12, mdscr_el1
stp   lr,  x9, [sp, ARM64_IFRAME_OFFSET_LR]
.cfi_rel_offset lr, ARM64_IFRAME_OFFSET_LR
.cfi_rel_offset sp, ARM64_IFRAME_OFFSET_USP
stp   x10, x11, [sp, ARM64_IFRAME_OFFSET_ELR]
.cfi_rel_offset elr1, ARM64_IFRAME_OFFSET_ELR
stp   x12, xzr,[sp, ARM64_IFRAME_OFFSET_MDSCR]
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

// Reverse regsave above, popping the full register state off of the stack.
.macro regrestore, from_lower_el_64
ldp   lr,  x9, [sp, ARM64_IFRAME_OFFSET_LR]
ldp   x10, x11, [sp, ARM64_IFRAME_OFFSET_ELR]
ldr   x12, [sp, ARM64_IFRAME_OFFSET_MDSCR]
msr   sp_el0, x9
msr   elr_el1, x10
msr   spsr_el1, x11
msr   mdscr_el1, x12
ldp   x0, x1, [sp, ARM64_IFRAME_OFFSET_R]
ldp   x2, x3, [sp, ARM64_IFRAME_OFFSET_R + (2 * 8)]
ldp   x4, x5, [sp, ARM64_IFRAME_OFFSET_R + (4 * 8)]
ldp   x6, x7, [sp, ARM64_IFRAME_OFFSET_R + (6 * 8)]
ldp   x8, x9, [sp, ARM64_IFRAME_OFFSET_R + (8 * 8)]
ldp   x10, x11, [sp, ARM64_IFRAME_OFFSET_R + (10 * 8)]
ldp   x12, x13, [sp, ARM64_IFRAME_OFFSET_R + (12 * 8)]
ldp   x14, x15, [sp, ARM64_IFRAME_OFFSET_R + (14 * 8)]
ldp   x16, x17, [sp, ARM64_IFRAME_OFFSET_R + (16 * 8)]
ldp   x18, x19, [sp, ARM64_IFRAME_OFFSET_R + (18 * 8)]
.if \from_lower_el_64
ldp   x20, x21, [sp, ARM64_IFRAME_OFFSET_R + (20 * 8)]
.else
// If restoring from EL1 -> EL1, leave x20 alone since it's holding the current
// per cpu pointer. It may have changed since we originally took the exception
// if we had been rescheduled to another cpu.
ldp   xzr, x21, [sp, ARM64_IFRAME_OFFSET_R + (20 * 8)]
.endif
ldp   x22, x23, [sp, ARM64_IFRAME_OFFSET_R + (22 * 8)]
ldp   x24, x25, [sp, ARM64_IFRAME_OFFSET_R + (24 * 8)]
ldp   x26, x27, [sp, ARM64_IFRAME_OFFSET_R + (26 * 8)]
ldp   x28, x29, [sp, ARM64_IFRAME_OFFSET_R + (28 * 8)]
.cfi_same_value lr
add_to_sp ARM64_IFRAME_SIZE
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
    .cfi_def_cfa sp, ARM64_IFRAME_SIZE
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
    .cfi_offset lr, ARM64_IFRAME_OFFSET_LR
    .cfi_offset sp, ARM64_IFRAME_OFFSET_USP
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

// Lighter version of restore_fixed_regs for EL1-to-EL1 exceptions. Strictly
// speaking not required, but in case of a firmware call via SMC or HVC accidentally
// trashing the register, preserve this fixed register by reloading it from the
// current thread pointer.
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

// Unhandled exception or irq. Save the full state and pass the which value through to
// the inner routine.
.macro invalid_exception, which
    start_isr_func_cfi
    regsave
    restore_fixed_regs x9
    mov x0, sp
    mov x1, #\which
    bl  arm64_invalid_exception
    b   .
.endm

.macro irq_exception, from_lower_el_64
    start_isr_func_cfi
    regsave
.if \from_lower_el_64
    restore_fixed_regs x9
    mov x1, #ARM64_EXCEPTION_FLAG_LOWER_EL
.else
    restore_percpu_ptr x9
    mov x1, #0
.endif
    mov x0, sp
    bl  arm64_irq
.if \from_lower_el_64
    b   arm64_exc_shared_restore_lower_el
.else
    b   arm64_exc_shared_restore
.endif
.endm

// TODO: find the appropriate place to reenable FIQs here when they're needed.
.macro sync_exception, from_lower_el_64
    start_isr_func_cfi
    regsave
    mrs x9, esr_el1
.if \from_lower_el_64
    restore_fixed_regs x11
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
.if \from_lower_el_64
    // We've just run out of space to fit in the 0x80 bytes of the sync exception vector.
    // branch to another block of code later in the file that will finish getting ready
    // and call arm64_sync_exception.
    b arm64_sync_exception_lower_el
.else
    mov x0, sp
    mov x1, #0
    bl arm64_sync_exception
    b  arm64_exc_shared_restore
.endif
.endm

.macro serror_exception, from_lower_el_64
     start_isr_func_cfi
     regsave
 .if \from_lower_el_64
     restore_fixed_regs x9
     mov x1, #ARM64_EXCEPTION_FLAG_LOWER_EL
 .else
     restore_percpu_ptr x9
     mov x1, #0
 .endif
     mov x0, sp
     bl arm64_serror_exception
 .if \from_lower_el_64
     b   arm64_exc_shared_restore_lower_el
 .else
     b   arm64_exc_shared_restore
 .endif
 .endm

// main vector table, aligned on 2048 byte boundary
.align 11
FUNCTION_LABEL(arm64_el1_exception_base)

// exceptions from current EL, using SP0
// the first 4 are invalid because SP0 is not used in EL1
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

// exceptions from current EL, using SPx
.org 0x200
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_current_el_SPx)
    sync_exception 0 // same EL, arm64
END_FUNCTION(arm64_el1_sync_exc_current_el_SPx)

.org 0x280
LOCAL_FUNCTION_LABEL(arm64_el1_irq_current_el_SPx)
    irq_exception 0 // same EL, arm64
END_FUNCTION(arm64_el1_irq_current_el_SPx)

.org 0x300
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_current_el_SPx)
    invalid_exception 0x12
END_FUNCTION(arm64_el1_fiq_current_el_SPx)

.org 0x380
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_current_el_SPx)
    serror_exception 0 // same EL, arm64
END_FUNCTION(arm64_el1_err_exc_current_el_SPx)

// exceptions from lower EL, running arm64
.org 0x400
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_64)
    sync_exception 1 // lower EL, arm64
END_FUNCTION(arm64_el1_sync_exc_lower_el_64)

.org 0x480
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_64)
    irq_exception 1 // lower EL, arm64
END_FUNCTION(arm64_el1_irq_lower_el_64)

.org 0x500
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_lower_el_64)
    invalid_exception 0x22
END_FUNCTION(arm64_el1_fiq_lower_el_64)

.org 0x580
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_64)
    serror_exception 1 // lower EL, arm64
END_FUNCTION(arm64_el1_err_exc_lower_el_64)

// exceptions from lower EL, running arm32
// all of these are invalid since there is no arm32 user space
.org 0x600
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_32)
    invalid_exception 0x30
END_FUNCTION(arm64_el1_sync_exc_lower_el_32)

.org 0x680
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_32)
    invalid_exception 0x31
END_FUNCTION(arm64_el1_irq_lower_el_32)

.org 0x700
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_lower_el_32)
    invalid_exception 0x32
END_FUNCTION(arm64_el1_fiq_lower_el_32)

.org 0x780
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_32)
    invalid_exception 0x33
END_FUNCTION(arm64_el1_err_exc_lower_el_32)

LOCAL_FUNCTION_LABEL(arm64_sync_exception_lower_el)
    start_isr_func_cfi
    mov x0, sp
    mov x1, #ARM64_EXCEPTION_FLAG_LOWER_EL
    bl  arm64_sync_exception
    b arm64_exc_shared_restore_lower_el
END_FUNCTION(arm64_sync_exception_lower_el)

// The first entry point is used when returning to user mode,
// the second when returning to kernel.
LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore_lower_el)
    start_helper
    save_shadow_call_sp x9
    regrestore 1 // lower EL
    eret
    // Prevent speculation through ERET
    SPECULATION_POSTFENCE
END_FUNCTION(arm64_exc_shared_restore_lower_el)

LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore)
    start_helper
    regrestore 0 // same EL
    eret
    // Prevent speculation through ERET
    SPECULATION_POSTFENCE
END_FUNCTION(arm64_exc_shared_restore)

//
// Syscall args are in x0-x7 already.
// pc is in x10 and needs to go in the next available register,
// or the stack if the regs are full.
//
.macro syscall_dispatcher nargs, syscall
.balign 16
.if \nargs == 8
    push_regs x10, xzr // push twice to maintain alignment
    bl  wrapper_\syscall
    add_to_sp 16
.else
    mov x\nargs, x10
    bl  wrapper_\syscall
.endif
    b .Lpost_syscall
.endm

.macro speculation_clear_gprs
    // x0 - x7 holds syscall args
    mov x8, xzr
    // x9 holds esr_el1
    // x10 holds elr_el1
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    // x16 holds syscall_num
    mov x17, xzr
    // x18 holds shadow stack
    mov x19, xzr
    // x20 holds percpu_ptr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov lr, xzr
.endm

//
// Expected state prior to arm64_syscall_dispatcher branch:
//
// percpu and shadow call stack registers have been restored
//
// x0-x7 - contains syscall arguments
// x9    - contains esr_el1
// x10   - contains elr_el1
// x16   - contains syscall_num
// sp    - points to base of iframe
//
// Expected state prior to branching to syscall_dispatcher macro:
//
// x0-x7  - contains syscall arguments
// x10    - contains userspace pc
//
LOCAL_FUNCTION_LABEL(arm64_syscall_dispatcher)
    start_isr_func_cfi
    // Clear unused GPRs to constrain speculative execution with user-controlled values.
    // While this is not a mitigation for any single vulnerability, this does make
    //   constructing attacks more difficult - speculatively executed gadgets will
    //   execute with most register state clear.
    // TODO(fxbug.dev/12540): Add to synchronous exception path as well.
    speculation_clear_gprs
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
    str  x0, [sp, ARM64_IFRAME_OFFSET_R]
    // Spectre: ARM64 CPUs may speculatively execute instructions after an SVC instruction.
    // The userspace entry code has a speculation barrier; advance ELR_EL1 past in on the return
    // since it has already done its job.
.ifne ARM64_SYSCALL_SPECULATION_BARRIER_SIZE - 12
.error "Syscall speculation barrier must be 12 bytes"
.endif
    ldr  x10, [sp, ARM64_IFRAME_OFFSET_ELR]
    add  x10, x10, ARM64_SYSCALL_SPECULATION_BARRIER_SIZE
    str  x10, [sp, ARM64_IFRAME_OFFSET_ELR]
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
