// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/riscv64.h>

#define REGOFF(x) ((x) * 8)

// void riscv64_context_switch(vaddr_t *old_sp, vaddr_t new_sp);
FUNCTION(riscv64_context_switch)
    /* save old frame */
    /* This layout should match struct context_switch_frame */
    addi  sp, sp, -REGOFF(13)
    sd    ra, REGOFF(0)(sp)
    sd    s0, REGOFF(1)(sp)
    sd    s1, REGOFF(2)(sp)
    sd    s2, REGOFF(3)(sp)
    sd    s3, REGOFF(4)(sp)
    sd    s4, REGOFF(5)(sp)
    sd    s5, REGOFF(6)(sp)
    sd    s6, REGOFF(7)(sp)
    sd    s7, REGOFF(8)(sp)
    sd    s8, REGOFF(9)(sp)
    sd    s9, REGOFF(10)(sp)
    sd    s10, REGOFF(11)(sp)
    sd    s11, REGOFF(12)(sp)

    /* save old sp */
    sd    sp, (a0)

    /* load new sp */
    mv    sp, a1

    /* restore new frame */
    ld    ra, REGOFF(0)(sp)
    ld    s0, REGOFF(1)(sp)
    ld    s1, REGOFF(2)(sp)
    ld    s2, REGOFF(3)(sp)
    ld    s3, REGOFF(4)(sp)
    ld    s4, REGOFF(5)(sp)
    ld    s5, REGOFF(6)(sp)
    ld    s6, REGOFF(7)(sp)
    ld    s7, REGOFF(8)(sp)
    ld    s8, REGOFF(9)(sp)
    ld    s9, REGOFF(10)(sp)
    ld    s10, REGOFF(11)(sp)
    ld    s11, REGOFF(12)(sp)
    addi  sp, sp, REGOFF(13)

    ret
END_FUNCTION(riscv64_context_switch)

// top level exception handler for riscv64 in non vectored mode
.balign 4
FUNCTION(riscv64_exception_entry)
    // swap the scratch system register with the current stack pointer
    csrrw  sp, sscratch, sp
    // if we took the exception from kernelspace, sscratch was set to 0. if we
    // took if from userspace, sscratch was set to the thread's kernel stack
    bnez   sp, taken_from_userspace

    // the following two blocks:
    // 1- ensure they have a kernel stack based on where the exception was taken
    // 2- create an iframe on that kernel stack
    // 3- save the previous sscratch on that iframe
    // 4- save the previous sp on that iframe
    // 5- save t0 on that iframe (necessary to retrieve sp in the user case)
    // 6- save the previous thread pointer on that iframe
    // 7- restore the kernel thread pointer
taken_from_kernelspace:
    // 1- we were already running in kernelspace, we just keep the previous sp
    csrrw  sp, sscratch, sp
    // 4- save the existing sp in an iframe on the stack
    sd     sp, -REGOFF(20)(sp)
    // 2- reserve an iframe_t on the stack
    addi   sp, sp, -REGOFF(21)
    // 3- sscratch was 0 so we store x0 in the iframe's field for sscratch
    sd     x0, REGOFF(0)(sp)
    // 5- save the t0 register
    sd     t0, REGOFF(14)(sp)
    // 6- save the tp register
    sd     tp, REGOFF(5)(sp)
    // 7- tp is already set to the kernel version of the thread pointer

    j      save_rest_of_iframe
taken_from_userspace:
    // 1- we just got the current thread's kernel stack out of sscratch
    // 3- sscratch was our current stack, we store it in an iframe on the stack
    sd     sp, -REGOFF(21)(sp)
    // 2- reserve an iframe_t on the stack
    addi   sp, sp, -REGOFF(21)
    // 5- save the t0 register before we need to overwrite it with sscratch
    sd     t0, REGOFF(14)(sp)
    // 4a- retrieve the previous sp in t0. use this opportunity to also set
    // sscratch to 0, in on our convention this means that we are in kernelspace
    csrrw  t0, sscratch, 0
    // 4b- save the existing sp in an iframe on the stack
    sd     t0, REGOFF(1)(sp)
    // 6- save the tp register
    sd     tp, REGOFF(5)(sp)
    // 6- restore tp, saved at the top of the stack by riscv64_uspace_entry
    ld     tp, REGOFF(20)(sp)

    // this is shared by both cases, we store all the callee trashed regs
save_rest_of_iframe:
    // sscratch is already saved
    // sp is already saved
    csrr   t0, sepc
    sd     t0, REGOFF(2)(sp)
    csrr   t0, sstatus
    sd     t0, REGOFF(3)(sp)
    sd     ra, REGOFF(4)(sp)
    // tp is already saved
    sd     a0, REGOFF(6)(sp)
    sd     a1, REGOFF(7)(sp)
    sd     a2, REGOFF(8)(sp)
    sd     a3, REGOFF(9)(sp)
    sd     a4, REGOFF(10)(sp)
    sd     a5, REGOFF(11)(sp)
    sd     a6, REGOFF(12)(sp)
    sd     a7, REGOFF(13)(sp)
    // t0 is already saved
    sd     t1, REGOFF(15)(sp)
    sd     t2, REGOFF(16)(sp)
    sd     t3, REGOFF(17)(sp)
    sd     t4, REGOFF(18)(sp)
    sd     t5, REGOFF(19)(sp)
    sd     t6, REGOFF(20)(sp)

    // TODO(revest): check sstatus.SD and save/restore floating point registers
    // there are other things that need to be done following page 34

call_c_handler:
    csrr   a0, scause
    mv     a1, sp
    jal    riscv64_exception_handler

restore_iframe:
    ld     t0, REGOFF(0)(sp)
    csrw   sscratch, t0
    ld     t0, REGOFF(2)(sp)
    csrw   sepc, t0
    ld     t0, REGOFF(3)(sp)
    csrw   sstatus, t0
    ld     ra, REGOFF(4)(sp)
    ld     a0, REGOFF(6)(sp)
    ld     a1, REGOFF(7)(sp)
    ld     a2, REGOFF(8)(sp)
    ld     a3, REGOFF(9)(sp)
    ld     a4, REGOFF(10)(sp)
    ld     a5, REGOFF(11)(sp)
    ld     a6, REGOFF(12)(sp)
    ld     a7, REGOFF(13)(sp)
    ld     t0, REGOFF(14)(sp)
    ld     t1, REGOFF(15)(sp)
    ld     t2, REGOFF(16)(sp)
    ld     t3, REGOFF(17)(sp)
    ld     t4, REGOFF(18)(sp)
    ld     t5, REGOFF(19)(sp)
    ld     t6, REGOFF(20)(sp)
    // now save the kernelspace's thread pointer at the top of the stack
    sd     tp, REGOFF(20)(sp)
    // and restore the iframe's thread pointer
    ld     tp, REGOFF(5)(sp)
    // finish by restoring the stack (because it overwrites our iframe pointer)
    ld     sp, REGOFF(1)(sp)

    sret
END_FUNCTION(riscv64_exception_entry)
