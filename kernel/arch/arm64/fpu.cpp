// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <assert.h>
#include <bits.h>
#include <kernel/thread.h>
#include <trace.h>

#define LOCAL_TRACE 0

/* FPEN bits in the cpacr register
 * 0 means all fpu instructions fault
 * 3 means no faulting at all EL levels
 * other values are not useful to us
 */
#define FPU_ENABLE_MASK (3 << 20)

static inline bool is_fpu_enabled(uint32_t cpacr) {
    return !!(BITS(cpacr, 21, 20) != 0);
}

static void arm64_fpu_load_state(struct thread* t) {
    struct fpstate* fpstate = &t->arch.fpstate;

    LTRACEF("cpu %u, thread %s, load fpstate %p\n", arch_curr_cpu_num(), t->name, fpstate);

    static_assert(sizeof(fpstate->regs) == 16 * 32, "");
    __asm__ volatile("ldp     q0, q1, [%0, #(0 * 32)]\n"
                     "ldp     q2, q3, [%0, #(1 * 32)]\n"
                     "ldp     q4, q5, [%0, #(2 * 32)]\n"
                     "ldp     q6, q7, [%0, #(3 * 32)]\n"
                     "ldp     q8, q9, [%0, #(4 * 32)]\n"
                     "ldp     q10, q11, [%0, #(5 * 32)]\n"
                     "ldp     q12, q13, [%0, #(6 * 32)]\n"
                     "ldp     q14, q15, [%0, #(7 * 32)]\n"
                     "ldp     q16, q17, [%0, #(8 * 32)]\n"
                     "ldp     q18, q19, [%0, #(9 * 32)]\n"
                     "ldp     q20, q21, [%0, #(10 * 32)]\n"
                     "ldp     q22, q23, [%0, #(11 * 32)]\n"
                     "ldp     q24, q25, [%0, #(12 * 32)]\n"
                     "ldp     q26, q27, [%0, #(13 * 32)]\n"
                     "ldp     q28, q29, [%0, #(14 * 32)]\n"
                     "ldp     q30, q31, [%0, #(15 * 32)]\n"
                     "msr     fpcr, %1\n"
                     "msr     fpsr, %2\n" ::"r"(fpstate->regs),
                     "r"((uint64_t)fpstate->fpcr),
                     "r"((uint64_t)fpstate->fpsr));
}

__NO_SAFESTACK static void arm64_fpu_save_state(struct thread* t) {
    struct fpstate* fpstate = &t->arch.fpstate;

    LTRACEF("cpu %u, thread %s, save fpstate %p\n", arch_curr_cpu_num(), t->name, fpstate);

    __asm__ volatile("stp     q0, q1, [%0, #(0 * 32)]\n"
                     "stp     q2, q3, [%0, #(1 * 32)]\n"
                     "stp     q4, q5, [%0, #(2 * 32)]\n"
                     "stp     q6, q7, [%0, #(3 * 32)]\n"
                     "stp     q8, q9, [%0, #(4 * 32)]\n"
                     "stp     q10, q11, [%0, #(5 * 32)]\n"
                     "stp     q12, q13, [%0, #(6 * 32)]\n"
                     "stp     q14, q15, [%0, #(7 * 32)]\n"
                     "stp     q16, q17, [%0, #(8 * 32)]\n"
                     "stp     q18, q19, [%0, #(9 * 32)]\n"
                     "stp     q20, q21, [%0, #(10 * 32)]\n"
                     "stp     q22, q23, [%0, #(11 * 32)]\n"
                     "stp     q24, q25, [%0, #(12 * 32)]\n"
                     "stp     q26, q27, [%0, #(13 * 32)]\n"
                     "stp     q28, q29, [%0, #(14 * 32)]\n"
                     "stp     q30, q31, [%0, #(15 * 32)]\n" ::"r"(fpstate->regs));

    // These are 32-bit values, but the msr instruction always uses a
    // 64-bit destination register.
    uint64_t fpcr, fpsr;
    __asm__("mrs %0, fpcr\n"
            : "=r"(fpcr));
    __asm__("mrs %0, fpsr\n"
            : "=r"(fpsr));
    fpstate->fpcr = (uint32_t)fpcr;
    fpstate->fpsr = (uint32_t)fpsr;

    LTRACEF("thread %s, fpcr %x, fpsr %x\n", t->name, fpstate->fpcr, fpstate->fpsr);
}

/* save fpu state if the thread had dirtied it and disable the fpu */
__NO_SAFESTACK void arm64_fpu_context_switch(struct thread* oldthread,
                                             struct thread* newthread) {
    uint64_t cpacr = ARM64_READ_SYSREG(cpacr_el1);
    if (is_fpu_enabled((uint32_t)cpacr)) {
        LTRACEF("saving state on thread %s\n", oldthread->name);

        /* save the state */
        arm64_fpu_save_state(oldthread);

        /* disable the fpu again */
        ARM64_WRITE_SYSREG(cpacr_el1, cpacr & ~FPU_ENABLE_MASK);
    }
}

/* called because of a fpu instruction used exception */
void arm64_fpu_exception(struct arm64_iframe_long* iframe, uint exception_flags) {
    LTRACEF("cpu %u, thread %s, flags 0x%x\n", arch_curr_cpu_num(), get_current_thread()->name, exception_flags);

    /* only valid to be called if exception came from lower level */
    DEBUG_ASSERT(exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL);

    uint64_t cpacr = ARM64_READ_SYSREG(cpacr_el1);
    DEBUG_ASSERT(!is_fpu_enabled((uint32_t)cpacr));

    /* enable the fpu */
    cpacr |= FPU_ENABLE_MASK;
    ARM64_WRITE_SYSREG(cpacr_el1, cpacr);

    /* load the state from the current cpu */
    thread_t* t = get_current_thread();
    if (likely(t))
        arm64_fpu_load_state(t);
}
