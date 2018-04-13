// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>
#include <arch/arch_thread.h>
#include <arch/arm64.h>
#include <arch/arm64/exceptions.h>
#include <zircon/zx-syscall-numbers.h>

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

// offset where x20-x29 goes in the iframe
#define regsave_high_reg_offset (20 * 8)

.macro regsave_long
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

.macro regsave_short
// Align the same as the long version above. The 10 extra words correspond to
// x20 through x29 which are not saved here.
sub_from_sp ((6*8) + (10*8))
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
// Preserve x0-x7 to mirror regsave_long
mrs  x9, sp_el0
mrs  x10, elr_el1
mrs  x11, spsr_el1
stp  lr, x9, [sp, #regsave_special_reg_offset]
.cfi_rel_offset lr, (regsave_special_reg_offset)
.cfi_rel_offset sp, (regsave_special_reg_offset + 8)
stp  x10, x11, [sp, #regsave_special_reg_offset + 16]
.cfi_rel_offset elr1, (regsave_special_reg_offset + 16)
.endm

// convert a short iframe to a long one by patching in the additional 10 words to save
.macro regsave_short_to_long
stp  x20, x21, [sp, #regsave_high_reg_offset]
stp  x22, x23, [sp, #regsave_high_reg_offset + 0x10]
stp  x24, x25, [sp, #regsave_high_reg_offset + 0x20]
stp  x26, x27, [sp, #regsave_high_reg_offset + 0x30]
stp  x28, x29, [sp, #regsave_high_reg_offset + 0x40]
mrs  x12, mdscr_el1
str  x12, [sp, #regsave_special_reg_offset + 32]
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

.macro regrestore_long
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

.macro regrestore_short
// Preserve x0-x7 to mirror regrestore_long
ldp  lr, x9, [sp, #regsave_special_reg_offset]
.cfi_same_value lr
ldp  x10, x11, [sp, #regsave_special_reg_offset + 16]
msr  sp_el0, x9
msr  elr_el1, x10
msr  spsr_el1, x11
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
add_to_sp ((6*8) + (10*8))
mark_lr_sp_inaccessible
.endm

.macro start_isr_cfi
    .cfi_startproc simple
    .cfi_signal_frame
    // The return address is in elr_el1, not lr.
    .cfi_return_column elr1
    .cfi_def_cfa sp, 0
.endm

.macro start_isr_func
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
.macro setup_short_helper_cfi
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
    .cfi_offset sp, -(3 * 8)
    .cfi_offset lr, -(4 * 8)
.endm

.macro setup_long_helper_cfi
    setup_short_helper_cfi
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
.endm

.macro start_short_helper
    start_helper_cfi
    setup_short_helper_cfi
.endm

.macro start_long_helper
    start_helper_cfi
    setup_long_helper_cfi
.endm

.macro invalid_exception, which
    start_isr_func
    regsave_long
    mov x1, #\which
    mov x0, sp
    bl  arm64_invalid_exception
    b   .
.endm

.macro irq_exception, exception_flags
    start_isr_func
    regsave_short
    msr daifclr, #1 /* reenable fiqs once elr and spsr have been saved */
    mov x0, sp
    mov x1, \exception_flags
    bl  arm64_irq
    cbnz x0, arm64_finish_user_irq_wrapper /* anything extra to do? */
    msr daifset, #1 /* disable fiqs to protect elr and spsr restore */
    b   arm64_exc_shared_restore_short
.endm

.macro sync_exception, exception_flags, from_lower_el_64=0
    start_isr_func
    regsave_long
    mrs x9, esr_el1
.if \from_lower_el_64
    // If this is a syscall, x0-x7 contain args and x16 contains syscall num.
    // x10 contains elr_el1.
    lsr x11, x9, #26              // shift esr right 26 bits to get ec
    cmp x11, #0x15                // check for 64-bit syscall
    beq arm64_syscall_dispatcher  // and jump to syscall handler
.endif
    // Prepare the default sync_exception args
    mov x0, sp
    mov x1, \exception_flags
    mov w2, w9
    bl  arm64_sync_exception
    b  arm64_exc_shared_restore_long
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
    sync_exception #0 /* same EL, arm64 */
END_FUNCTION(arm64_el1_sync_exc_current_el_SPx)

.org 0x280
LOCAL_FUNCTION_LABEL(arm64_el1_irq_current_el_SPx)
    irq_exception #0 /* same EL, arm64 */
END_FUNCTION(arm64_el1_irq_current_el_SPx)

.org 0x300
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_current_el_SPx)
    start_isr_func
    regsave_short
    mov x0, sp
    bl  platform_fiq
    b  arm64_exc_shared_restore_short
END_FUNCTION(arm64_el1_fiq_current_el_SPx)

.org 0x380
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_current_el_SPx)
    invalid_exception 0x13
END_FUNCTION(arm64_el1_err_exc_current_el_SPx)

/* exceptions from lower EL, running arm64 */
.org 0x400
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_64)
    sync_exception #(ARM64_EXCEPTION_FLAG_LOWER_EL), 1
END_FUNCTION(arm64_el1_sync_exc_lower_el_64)

.org 0x480
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_64)
    irq_exception #(ARM64_EXCEPTION_FLAG_LOWER_EL)
END_FUNCTION(arm64_el1_irq_lower_el_64)

.org 0x500
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_lower_el_64)
    start_isr_func
    regsave_short
    mov x0, sp
    bl  platform_fiq
    b  arm64_exc_shared_restore_short
END_FUNCTION(arm64_el1_fiq_lower_el_64)

.org 0x580
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_64)
    invalid_exception 0x23
END_FUNCTION(arm64_el1_err_exc_lower_el_64)

/* exceptions from lower EL, running arm32 */
.org 0x600
LOCAL_FUNCTION_LABEL(arm64_el1_sync_exc_lower_el_32)
    sync_exception #(ARM64_EXCEPTION_FLAG_LOWER_EL|ARM64_EXCEPTION_FLAG_ARM32)
END_FUNCTION(arm64_el1_sync_exc_lower_el_32)

.org 0x680
LOCAL_FUNCTION_LABEL(arm64_el1_irq_lower_el_32)
    irq_exception #(ARM64_EXCEPTION_FLAG_LOWER_EL|ARM64_EXCEPTION_FLAG_ARM32)
END_FUNCTION(arm64_el1_irq_lower_el_32)

.org 0x700
LOCAL_FUNCTION_LABEL(arm64_el1_fiq_lower_el_32)
    start_isr_func
    regsave_short
    mov x0, sp
    bl  platform_fiq
    b  arm64_exc_shared_restore_short
END_FUNCTION(arm64_el1_fiq_lower_el_32)

.org 0x780
LOCAL_FUNCTION_LABEL(arm64_el1_err_exc_lower_el_32)
    invalid_exception 0x33
END_FUNCTION(arm64_el1_err_exc_lower_el_32)

/* If an IRQ happened in userspace, and either the thread was signaled or
   needs to be rescheduled, then we end up here after arm64_irq returns.
   Suspending the thread requires constructing a long iframe in order to
   provide the values of all regs to any debugger that wishes to access
   them, but we can't do that until arm64_irq returns as we rely on the
   compiler to save/restore callee-saved regs. */
LOCAL_FUNCTION_LABEL(arm64_finish_user_irq_wrapper)
    start_short_helper
    /* if we're only here because of a need to reschedule then we don't
       need to construct a long iframe */
    cmp x0, #ARM64_IRQ_EXIT_RESCHEDULE
    bne 1f
    mov x1, #0 /* don't need an iframe, just pass NULL */
    bl  arm64_finish_user_irq
    msr daifset, #1 /* disable fiqs to protect elr and spsr restore */
    b   arm64_exc_shared_restore_short
1:
    /* convert the short to a long frame */
    regsave_short_to_long
    mov x1, sp
    bl  arm64_finish_user_irq
    msr daifset, #1 /* disable fiqs to protect elr and spsr restore */
    b   arm64_exc_shared_restore_long
END_FUNCTION(arm64_finish_user_irq_wrapper)

LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore_long)
    start_long_helper
    regrestore_long
    eret
END_FUNCTION(arm64_exc_shared_restore_long)

LOCAL_FUNCTION_LABEL(arm64_exc_shared_restore_short)
    start_short_helper
    regrestore_short
    eret
END_FUNCTION(arm64_exc_shared_restore_short)

//
// Syscall args are in x0-x7 already.
// pc is in x10 and needs to go in the next available register,
// or the stack if the regs are full.
//
.macro pre_args, nargs
.if \nargs == 8
    push_regs x10, x11 // push twice to maintain alignment
.else
    mov x\nargs, x10
.endif
.endm

.macro post_args, nargs
.if \nargs == 8
    pop_regs x10, x11
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
// Expected state prior to unknown_syscall and wrapper_syscall...
//
// x0-x7  - contains syscall arguments
// x10    - contains userspace pc
//
LOCAL_FUNCTION_LABEL(arm64_syscall_dispatcher)
    start_isr_func
    // Restore per cpu pointer
    mrs  x11, tpidr_el1
    ldr  x18, [x11, #CURRENT_PERCPU_PTR_OFFSET]
    // Verify syscall number and call the unknown handler if bad.
    cmp  x16, #ZX_SYS_COUNT
    bhs  .Lunknown_syscall
    // Jump to the right syscall wrapper.
    adr  x12, .Lcall_wrapper_table
    ldr  x12, [x12, x16, LSL#3]
    br   x12
.Lunknown_syscall:
    mov  x0, x16
    pre_args 1
    bl   unknown_syscall
    post_args 1
.Lpost_syscall:
    // Upon return from syscall, x0 = status, x1 = thread signalled
    // Move the status to frame->r[0] for return to userspace.
    str  x0, [sp]
    // Check for pending signals. If none, just return.
    cbz  x1, arm64_exc_shared_restore_long
    mov  x0, sp
    bl   arm64_thread_process_pending_signals
    b    arm64_exc_shared_restore_long
END_FUNCTION(arm64_syscall_dispatcher)

// Adds a label for making the syscall and adds it to the jump table.
.macro syscall_dispatch nargs, syscall
    .pushsection .text.syscall-dispatch,"ax",%progbits
    LOCAL_FUNCTION(.Lcall_\syscall\())
        pre_args \nargs
        bl wrapper_\syscall
        post_args \nargs
    END_FUNCTION(.Lcall_\syscall\())
    .popsection
    .pushsection .rodata.syscall-table,"a",%progbits
        .quad .Lcall_\syscall
    .popsection
.endm

// Adds the label for the jump table.
.macro start_syscall_dispatch
    .pushsection .rodata.syscall-table,"a",%progbits
    .balign 8
    .Lcall_wrapper_table:
    .popsection
.endm

#include <zircon/syscall-kernel-branches.S>
