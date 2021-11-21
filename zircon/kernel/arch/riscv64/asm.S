// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/regs.h>
#include <arch/riscv64.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <zircon/errors.h>

// void riscv64_context_switch(vaddr_t *old_sp, vaddr_t new_sp);
FUNCTION(riscv64_context_switch)
    /* save old frame */
    addi  sp, sp, -SIZEOF_CONTEXT_SWITCH_FRAME
    sd    ra, CONTEXT_SWITCH_FRAME_OFFSET_RA(sp)

    sd    s0, CONTEXT_SWITCH_FRAME_OFFSET_S(0)(sp)
    sd    s1, CONTEXT_SWITCH_FRAME_OFFSET_S(1)(sp)
    sd    s2, CONTEXT_SWITCH_FRAME_OFFSET_S(2)(sp)
    sd    s3, CONTEXT_SWITCH_FRAME_OFFSET_S(3)(sp)
    sd    s4, CONTEXT_SWITCH_FRAME_OFFSET_S(4)(sp)
    sd    s5, CONTEXT_SWITCH_FRAME_OFFSET_S(5)(sp)
    sd    s6, CONTEXT_SWITCH_FRAME_OFFSET_S(6)(sp)
    sd    s7, CONTEXT_SWITCH_FRAME_OFFSET_S(7)(sp)
    sd    s8, CONTEXT_SWITCH_FRAME_OFFSET_S(8)(sp)
    sd    s9, CONTEXT_SWITCH_FRAME_OFFSET_S(9)(sp)
    sd    s10, CONTEXT_SWITCH_FRAME_OFFSET_S(10)(sp)
    sd    s11, CONTEXT_SWITCH_FRAME_OFFSET_S(11)(sp)

    // TODO(tracker #99): This code can be optimized by not saving/restoring
    // the floating registers if FP support is off.  See riscv64_exception_entry
    // below for details.

/*    fsd   fs0, CONTEXT_SWITCH_FRAME_OFFSET_FS(0)(sp)
    fsd   fs1, CONTEXT_SWITCH_FRAME_OFFSET_FS(1)(sp)
    fsd   fs2, CONTEXT_SWITCH_FRAME_OFFSET_FS(2)(sp)
    fsd   fs3, CONTEXT_SWITCH_FRAME_OFFSET_FS(3)(sp)
    fsd   fs4, CONTEXT_SWITCH_FRAME_OFFSET_FS(4)(sp)
    fsd   fs5, CONTEXT_SWITCH_FRAME_OFFSET_FS(5)(sp)
    fsd   fs6, CONTEXT_SWITCH_FRAME_OFFSET_FS(6)(sp)
    fsd   fs7, CONTEXT_SWITCH_FRAME_OFFSET_FS(7)(sp)
    fsd   fs8, CONTEXT_SWITCH_FRAME_OFFSET_FS(8)(sp)
    fsd   fs9, CONTEXT_SWITCH_FRAME_OFFSET_FS(9)(sp)
    fsd   fs10, CONTEXT_SWITCH_FRAME_OFFSET_FS(10)(sp)
    fsd   fs11, CONTEXT_SWITCH_FRAME_OFFSET_FS(11)(sp) */

    /* save old sp */
    sd    sp, (a0)

    /* load new sp */
    mv    sp, a1

    /* restore new frame */
    ld    s0, CONTEXT_SWITCH_FRAME_OFFSET_S(0)(sp)
    ld    s1, CONTEXT_SWITCH_FRAME_OFFSET_S(1)(sp)
    ld    s2, CONTEXT_SWITCH_FRAME_OFFSET_S(2)(sp)
    ld    s3, CONTEXT_SWITCH_FRAME_OFFSET_S(3)(sp)
    ld    s4, CONTEXT_SWITCH_FRAME_OFFSET_S(4)(sp)
    ld    s5, CONTEXT_SWITCH_FRAME_OFFSET_S(5)(sp)
    ld    s6, CONTEXT_SWITCH_FRAME_OFFSET_S(6)(sp)
    ld    s7, CONTEXT_SWITCH_FRAME_OFFSET_S(7)(sp)
    ld    s8, CONTEXT_SWITCH_FRAME_OFFSET_S(8)(sp)
    ld    s9, CONTEXT_SWITCH_FRAME_OFFSET_S(9)(sp)
    ld    s10, CONTEXT_SWITCH_FRAME_OFFSET_S(10)(sp)
    ld    s11, CONTEXT_SWITCH_FRAME_OFFSET_S(11)(sp)

/*    fld   fs0, CONTEXT_SWITCH_FRAME_OFFSET_FS(0)(sp)
    fld   fs1, CONTEXT_SWITCH_FRAME_OFFSET_FS(1)(sp)
    fld   fs2, CONTEXT_SWITCH_FRAME_OFFSET_FS(2)(sp)
    fld   fs3, CONTEXT_SWITCH_FRAME_OFFSET_FS(3)(sp)
    fld   fs4, CONTEXT_SWITCH_FRAME_OFFSET_FS(4)(sp)
    fld   fs5, CONTEXT_SWITCH_FRAME_OFFSET_FS(5)(sp)
    fld   fs6, CONTEXT_SWITCH_FRAME_OFFSET_FS(6)(sp)
    fld   fs7, CONTEXT_SWITCH_FRAME_OFFSET_FS(7)(sp)
    fld   fs8, CONTEXT_SWITCH_FRAME_OFFSET_FS(8)(sp)
    fld   fs9, CONTEXT_SWITCH_FRAME_OFFSET_FS(9)(sp)
    fld   fs10, CONTEXT_SWITCH_FRAME_OFFSET_FS(10)(sp)
    fld   fs11, CONTEXT_SWITCH_FRAME_OFFSET_FS(11)(sp) */

    ld    ra, CONTEXT_SWITCH_FRAME_OFFSET_RA(sp)
    addi  sp, sp, SIZEOF_CONTEXT_SWITCH_FRAME

    ret
END_FUNCTION(riscv64_context_switch)

// void riscv64_uspace_entry(iframe_t* iframe, void *sp) __NO_RETURN;
// Where |sp| is the kernel stack pointer.
FUNCTION(riscv64_uspace_entry)
    // Save the thread pointer and percpu pointer at the top of the stack.
    sd     tp, -REGOFF(1)(a1)
    sd     gp, -REGOFF(2)(a1)

    // Save the kernel stack pointer in the sscratch register
    csrw   sscratch, a1

    // Load the iframe
    ld     t0, IFRAME_T_OFFSET_EPC(a0)
    csrw   sepc, t0
    ld     t0, IFRAME_T_OFFSET_STATUS(a0)
    csrw   sstatus, t0

    ld     a1, IFRAME_T_OFFSET_A(1)(a0)
    ld     a2, IFRAME_T_OFFSET_A(2)(a0)
    ld     a3, IFRAME_T_OFFSET_A(3)(a0)
    ld     a4, IFRAME_T_OFFSET_A(4)(a0)
    ld     a5, IFRAME_T_OFFSET_A(5)(a0)
    ld     a6, IFRAME_T_OFFSET_A(6)(a0)
    ld     a7, IFRAME_T_OFFSET_A(7)(a0)
    ld     t0, IFRAME_T_OFFSET_T(0)(a0)
    ld     t1, IFRAME_T_OFFSET_T(1)(a0)
    ld     t2, IFRAME_T_OFFSET_T(2)(a0)
    ld     t3, IFRAME_T_OFFSET_T(3)(a0)
    ld     t4, IFRAME_T_OFFSET_T(4)(a0)
    ld     t5, IFRAME_T_OFFSET_T(5)(a0)
    ld     t6, IFRAME_T_OFFSET_T(6)(a0)
    ld     sp, IFRAME_T_OFFSET_SP(a0)
    ld     ra, IFRAME_T_OFFSET_RA(a0)
    ld     gp, IFRAME_T_OFFSET_GP(a0)
    ld     tp, IFRAME_T_OFFSET_TP(a0)
    // Load a0 last since it points to the iframe_t.
    ld     a0, IFRAME_T_OFFSET_A(0)(a0)

    li     s0, 0
    li     s1, 0
    li     s2, 0
    li     s3, 0
    li     s4, 0
    li     s5, 0
    li     s6, 0
    li     s7, 0
    li     s8, 0
    li     s9, 0
    li     s10, 0
    li     s11, 0
    li     s11, 0

    sret
END_FUNCTION(riscv64_uspace_entry)

.macro save_regs, user
    addi   sp, sp, -SIZEOF_IFRAME_T
.if \user == 1
    // recover tp from the top of the stack (we saved it here before)
    sd     tp, IFRAME_T_OFFSET_TP(sp)
    ld     tp, (SIZEOF_IFRAME_T-REGOFF(1))(sp) // this is where the top of the stack used to be

    // also recover gp from the top of the stack
    sd     gp, IFRAME_T_OFFSET_GP(sp)
    // save the user stack and zero scratch register
    csrrw  gp, sscratch, zero
    sd     gp, IFRAME_T_OFFSET_SP(sp)
    ld     gp, (SIZEOF_IFRAME_T-REGOFF(2))(sp)
.endif
    sd     t6, IFRAME_T_OFFSET_T(6)(sp)
    sd     t5, IFRAME_T_OFFSET_T(5)(sp)
    sd     t4, IFRAME_T_OFFSET_T(4)(sp)
    sd     t3, IFRAME_T_OFFSET_T(3)(sp)
    sd     t2, IFRAME_T_OFFSET_T(2)(sp)
    sd     t1, IFRAME_T_OFFSET_T(1)(sp)
    sd     t0, IFRAME_T_OFFSET_T(0)(sp)
    sd     a7, IFRAME_T_OFFSET_A(7)(sp)
    sd     a6, IFRAME_T_OFFSET_A(6)(sp)
    sd     a5, IFRAME_T_OFFSET_A(5)(sp)
    sd     a4, IFRAME_T_OFFSET_A(4)(sp)
    sd     a3, IFRAME_T_OFFSET_A(3)(sp)
    sd     a2, IFRAME_T_OFFSET_A(2)(sp)
    sd     a1, IFRAME_T_OFFSET_A(1)(sp)
    sd     a0, IFRAME_T_OFFSET_A(0)(sp)
    sd     ra, IFRAME_T_OFFSET_RA(sp)
    csrr   t0, sepc
    sd     t0, IFRAME_T_OFFSET_EPC(sp)
    csrr   t0, sstatus
    sd     t0, IFRAME_T_OFFSET_STATUS(sp)

    // If the floating point registers are dirty, save them too.
    // Note: t0 at this time holds the value of CSR sstatus.
/*    srli   t1, t0, 13              // Put FS[1:0] into t1[1:0].
    andi   t1, t1, 3               // Mask out all bits but t1[1:0].
    li     t2, 3                   // Load "dirty" value (3) into t2.
    bne    t1, t2, 1f              // Skip saving fp registers if not "dirty".

    slli   t2, t2, 13              // Move t2[1:0] into FS[1:0] position in t2.
    not    t2, t2                  // Create a mask except for FS[1:0] field.
    and    t0, t0, t2              // Clear FS[1:0] in t0.

    li     t2, 2                   // Load "clean" value (2) into t2.
    slli   t2, t2, 13              // Move t2[1:0] into FS[1:0] position in t2.
    or     t0, t0, t2              // Set FS[1:0] in t0 to "clean".

    // Save updated FS[1:0] in sstatus field of iframe.
    sd     t0, IFRAME_T_OFFSET_STATUS(sp)

    // Save floating point control register.
    csrr   t0, fcsr
    sd     t0, IFRAME_T_OFFSET_FCSR(sp)

    fsd    fa0, IFRAME_T_OFFSET_FA(0)(sp)
    fsd    fa1, IFRAME_T_OFFSET_FA(1)(sp)
    fsd    fa2, IFRAME_T_OFFSET_FA(2)(sp)
    fsd    fa3, IFRAME_T_OFFSET_FA(3)(sp)
    fsd    fa4, IFRAME_T_OFFSET_FA(4)(sp)
    fsd    fa5, IFRAME_T_OFFSET_FA(5)(sp)
    fsd    fa6, IFRAME_T_OFFSET_FA(6)(sp)
    fsd    fa7, IFRAME_T_OFFSET_FA(7)(sp)
    fsd    ft0, IFRAME_T_OFFSET_FT(0)(sp)
    fsd    ft1, IFRAME_T_OFFSET_FT(1)(sp)
    fsd    ft2, IFRAME_T_OFFSET_FT(2)(sp)
    fsd    ft3, IFRAME_T_OFFSET_FT(3)(sp)
    fsd    ft4, IFRAME_T_OFFSET_FT(4)(sp)
    fsd    ft5, IFRAME_T_OFFSET_FT(5)(sp)
    fsd    ft6, IFRAME_T_OFFSET_FT(6)(sp)
    fsd    ft7, IFRAME_T_OFFSET_FT(7)(sp)
    fsd    ft8, IFRAME_T_OFFSET_FT(8)(sp)
    fsd    ft9, IFRAME_T_OFFSET_FT(9)(sp)
    fsd    ft10, IFRAME_T_OFFSET_FT(10)(sp)
    fsd    ft11, IFRAME_T_OFFSET_FT(11)(sp)

1: */
    csrr   a0, scause
    mv     a1, sp
    // args are set up for a call into riscv64_exception_handler()
    // a0 = scause
    // a1 = sp
.endm

.macro restore_regs, user
    // put everything back
    ld     t0, IFRAME_T_OFFSET_EPC(sp)
    csrw   sepc, t0
    ld     t0, IFRAME_T_OFFSET_STATUS(sp)
    csrw   sstatus, t0

    // Restore floating point registers if needed.  If the state is "initial"
    // set register values to a known state (all zeroes).  If the state is
    // "clean" load the register values from the iframe.  It would be an error
    // for the state to be "dirty" since the kernel does not use floating point
    // instructions.
/*    srli   t1, t0, 13             // Put FS[1:0] into t1[1:0].
    andi   t1, t1, 3              // Mask out all bits but t1[1:0].

    // If state is "off" then no floating point registers are in use.
    // Skip to restoring the integer registers.
    beq    t1, zero, 2f           // Branch if state is "off".

    // Restore floating point control register.
    ld     t0, IFRAME_T_OFFSET_FCSR(sp)
    csrw   fcsr, t0

    // If state is "initial" then floating point registers are in use
    // and must be set to the known initial state.
    li     t2, 1                  // Load "initial" value (1) into t2.
    beq    t1, t2, 1f             // Branch if state is "initial".

    // At this point assume the value is "clean" and not "dirty".
    // TODO: Do we want to check for "dirty" and panic or something?
    fld    fa0, IFRAME_T_OFFSET_FA(0)(sp)
    fld    fa1, IFRAME_T_OFFSET_FA(1)(sp)
    fld    fa2, IFRAME_T_OFFSET_FA(2)(sp)
    fld    fa3, IFRAME_T_OFFSET_FA(3)(sp)
    fld    fa4, IFRAME_T_OFFSET_FA(4)(sp)
    fld    fa5, IFRAME_T_OFFSET_FA(5)(sp)
    fld    fa6, IFRAME_T_OFFSET_FA(6)(sp)
    fld    fa7, IFRAME_T_OFFSET_FA(7)(sp)
    fld    ft0, IFRAME_T_OFFSET_FT(0)(sp)
    fld    ft1, IFRAME_T_OFFSET_FT(1)(sp)
    fld    ft2, IFRAME_T_OFFSET_FT(2)(sp)
    fld    ft3, IFRAME_T_OFFSET_FT(3)(sp)
    fld    ft4, IFRAME_T_OFFSET_FT(4)(sp)
    fld    ft5, IFRAME_T_OFFSET_FT(5)(sp)
    fld    ft6, IFRAME_T_OFFSET_FT(6)(sp)
    fld    ft7, IFRAME_T_OFFSET_FT(7)(sp)
    fld    ft8, IFRAME_T_OFFSET_FT(8)(sp)
    fld    ft9, IFRAME_T_OFFSET_FT(9)(sp)
    fld    ft10, IFRAME_T_OFFSET_FT(10)(sp)
    fld    ft11, IFRAME_T_OFFSET_FT(11)(sp)
    j      2f

1:
    // Set all floating point registers to zero.
    fcvt.d.l fa0, zero
    fcvt.d.l fa1, zero
    fcvt.d.l fa2, zero
    fcvt.d.l fa3, zero
    fcvt.d.l fa4, zero
    fcvt.d.l fa5, zero
    fcvt.d.l fa6, zero
    fcvt.d.l fa7, zero
    fcvt.d.l ft0, zero
    fcvt.d.l ft1, zero
    fcvt.d.l ft2, zero
    fcvt.d.l ft3, zero
    fcvt.d.l ft4, zero
    fcvt.d.l ft5, zero
    fcvt.d.l ft6, zero
    fcvt.d.l ft7, zero
    fcvt.d.l ft8, zero
    fcvt.d.l ft9, zero
    fcvt.d.l ft10, zero
    fcvt.d.l ft11, zero

2: */
    ld     ra, IFRAME_T_OFFSET_RA(sp)
    ld     a0, IFRAME_T_OFFSET_A(0)(sp)
    ld     a1, IFRAME_T_OFFSET_A(1)(sp)
    ld     a2, IFRAME_T_OFFSET_A(2)(sp)
    ld     a3, IFRAME_T_OFFSET_A(3)(sp)
    ld     a4, IFRAME_T_OFFSET_A(4)(sp)
    ld     a5, IFRAME_T_OFFSET_A(5)(sp)
    ld     a6, IFRAME_T_OFFSET_A(6)(sp)
    ld     a7, IFRAME_T_OFFSET_A(7)(sp)
    ld     t0, IFRAME_T_OFFSET_T(0)(sp)
    ld     t1, IFRAME_T_OFFSET_T(1)(sp)
    ld     t2, IFRAME_T_OFFSET_T(2)(sp)
    ld     t3, IFRAME_T_OFFSET_T(3)(sp)
    ld     t4, IFRAME_T_OFFSET_T(4)(sp)
    ld     t5, IFRAME_T_OFFSET_T(5)(sp)
    ld     t6, IFRAME_T_OFFSET_T(6)(sp)

.if \user == 1
    // before we run out of registers, save tp and gp to the top of the kernel
    // stack and put the kernel stack in the scratch register
    sd     tp, (SIZEOF_IFRAME_T - REGOFF(1))(sp)
    sd     gp, (SIZEOF_IFRAME_T - REGOFF(2))(sp)

    addi   gp, sp, SIZEOF_IFRAME_T
    csrw   sscratch, gp

    ld     tp, IFRAME_T_OFFSET_TP(sp)
    ld     gp, IFRAME_T_OFFSET_GP(sp)
    ld     sp, IFRAME_T_OFFSET_SP(sp)
.else
    addi   sp, sp, SIZEOF_IFRAME_T
.endif
.endm

LOCAL_FUNCTION(kernel_exception_entry)
    // we came from kernel space so tp and gp are okay
    save_regs 0
    jal    riscv64_exception_handler
    restore_regs 0

    sret
END_FUNCTION(kernel_exception_entry)

LOCAL_FUNCTION(user_exception_entry)
    // we came from user space, assume gp and tp have been trashed
    save_regs 1
    jal    riscv64_exception_handler
    restore_regs 1

    sret
END_FUNCTION(user_exception_entry)

// top level exception handler for riscv in non vectored mode
.balign 4
FUNCTION(riscv64_exception_entry)
    // check to see if we came from user space
    csrrw   sp, sscratch, sp
    bnez    sp, 1f

    // put the stack back
    csrrw   sp, sscratch, sp
    j       kernel_exception_entry

1:
    // came from user space
    j       user_exception_entry
END_FUNCTION(riscv64_exception_entry)

//
// Syscall args are in a0-a7 already.
// pc is in t1 and needs to go in the next available register,
// or the stack if the regs are full.
//
.macro syscall_dispatcher nargs, syscall
.balign 16
.if \nargs == 8
    addi   sp, sp, -8
    sd     t1, (sp)
    jal    wrapper_\syscall
    addi   sp, sp, 8
.else
    mv a\nargs, t1
    jal    wrapper_\syscall
.endif
    j .Lpost_syscall
.endm

// void riscv64_syscall_dispatcher(iframe_t* iframe);
// Registers in the iframe are parsed using the following convention:
//
//   a0-a7 - contains syscall arguments
//   t0    - contains syscall_num
//
FUNCTION(riscv64_syscall_dispatcher)
    addi   sp, sp, -8
    sd     ra, (sp)

    ld     t1, IFRAME_T_OFFSET_EPC(a0)
    ld     t0, IFRAME_T_OFFSET_T(0)(a0)
    // Load a0 last since it points to the iframe_t.
    ld     a1, IFRAME_T_OFFSET_A(1)(a0)
    ld     a2, IFRAME_T_OFFSET_A(2)(a0)
    ld     a3, IFRAME_T_OFFSET_A(3)(a0)
    ld     a4, IFRAME_T_OFFSET_A(4)(a0)
    ld     a5, IFRAME_T_OFFSET_A(5)(a0)
    ld     a6, IFRAME_T_OFFSET_A(6)(a0)
    ld     a7, IFRAME_T_OFFSET_A(7)(a0)
    ld     a0, IFRAME_T_OFFSET_A(0)(a0)

    // Verify syscall number and call the unknown handler if bad.
    li     t2, ZX_SYS_COUNT
    bge    t0, t2, .Lunknown_syscall

    // Jump to the right syscall wrapper. The syscall table is an
    // array of 16 byte aligned routines for each syscall. Each routine
    // marshalls some arguments, jumps to the routine, and then branches
    // back to .Lpost_syscall (see syscall_dispatcher macro above).
    slli   t0, t0, 4
    lla    t2, .Lsyscall_table
    add    t2, t2, t0
    jr     t2

.Lunknown_syscall:
    mv     a0, t0 // move the syscall number into the 0 arg slot
    mv     a1, t1 // pc into arg 1
    jal    unknown_syscall
    // fall through

// Adds the label for the jump table.
.balign 16
.Lsyscall_table:

// One of these macros is invoked by kernel.inc for each syscall.
// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  syscall_dispatcher nargs, name
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

#include <lib/syscalls/kernel.inc>

#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL
#undef VDSO_SYSCALL

.Lpost_syscall:
    ld     ra, (sp)
    addi   sp, sp, 8
    ret

END_FUNCTION(riscv64_syscall_dispatcher)

FUNCTION(mexec_asm)
    ret
END_FUNCTION(mexec_asm)

DATA(mexec_asm_end)

// Riscv64UserCopyRet _riscv64_user_copy(void *dst, const void *src, size_t len, uint64_t *fault_return)
.balign 64 // Align to cache line.  This code fits in one cache line.
FUNCTION(_riscv64_user_copy)
    addi   sp, sp, -24
    sd     s1, 16(sp)
    sd     s2, 8(sp)
    sd     s3, (sp)

    // Allow supervisor accesses to user memory
    li     s1, (1 << 18)
    csrs   sstatus, s1

    // Save fault_return and the ra register
    mv     s2, a3
    mv     s3, ra

    // Set *fault_return to fault_from_user
    la     t0, .Lfault_from_user
    sd     t0, (a3)

    // Just call our normal memcpy.  The caller has ensured that the
    // address range is in the user portion of the address space.
    // While fault_return_ptr is set, userspace data faults will be
    // redirected to .Lfault_from_user, below.
    //
    // NOTE! We make important assumptions here about what the memcpy
    // code does: it never moves the stack pointer, and it never touches a6 and
    // a7 where we saved fault_return and ra
    call   memcpy

    // Store a successful status for the return. In this case since we do not set x1 the value of
    // the fault address in the return struct is undefined.
    li     a0, ZX_OK

.Luser_copy_return:
    // Restore *fault_return and the ra register
    sd     x0, (s2)
    mv     ra, s3

    // Disable supervisor accesses to user memory
    csrc   sstatus, s1

    ld     s1, 16(sp)
    ld     s2, 8(sp)
    ld     s3, (sp)
    addi   sp, sp, 24
    ret
END_FUNCTION(_riscv64_user_copy)

// If we are capturing faults the exception handler will have placed the faulting virtual address
// for us in a1 and the flags in a2. We do not touch a1 and rely on the caller to know if the value
// is meaningful based on whether it specified fault capture or not, we just need to construct a
// valid a0 before jmping to user_copy_return.
.Lfault_from_user:
    li     a0, ZX_ERR_INVALID_ARGS
    // If we are capturing faults the flags will have been placed in a2 and we want them placed in
    // the high bits of a0. If not capturing faults then we will copy some garbage bits which will
    // be ignored by the caller.
    //bfi a0, a2, 32, 32
    j      .Luser_copy_return
