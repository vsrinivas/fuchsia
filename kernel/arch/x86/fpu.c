// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <trace.h>
#include <assert.h>
#include <arch/fpu.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <string.h>
#include <kernel/thread.h>

#define LOCAL_TRACE 0

#define FPU_MASK_ALL_EXCEPTIONS 1

static int fp_supported;

static void set_fpu_enabled(bool enabled);

/* FXSAVE area comprises 512 bytes starting with 16-byte aligned */
static uint8_t __ALIGNED(16) fpu_init_states[512]= {0};

void fpu_init(void)
{
    uint16_t fcw;
    uint32_t mxcsr;

#ifdef ARCH_X86_64
    uint64_t x;
#else
    uint32_t x;
#endif

    if (!x86_feature_test(X86_FEATURE_FPU) ||
        !x86_feature_test(X86_FEATURE_SSE) ||
        !x86_feature_test(X86_FEATURE_SSE2) ||
        !x86_feature_test(X86_FEATURE_SSE3) ||
        !x86_feature_test(X86_FEATURE_SSSE3) ||
        !x86_feature_test(X86_FEATURE_SSE4_1) ||
        !x86_feature_test(X86_FEATURE_SSE4_2) ||
        !x86_feature_test(X86_FEATURE_FXSR))
        return;

    fp_supported = 1;

    /* No x87 emul, monitor co-processor */

    x = x86_get_cr0();
    x &= ~X86_CR0_EM;
    x |= X86_CR0_NE;
    x |= X86_CR0_MP;
    x86_set_cr0(x);

    /* Init x87 */
    __asm__ __volatile__ ("finit");
    __asm__ __volatile__("fstcw %0" : "=m" (fcw));
#if FPU_MASK_ALL_EXCEPTIONS
    /* mask all exceptions */
    fcw |= 0x3f;
#else
    /* unmask all exceptions */
    fcw &= 0xffc0;
#endif
    __asm__ __volatile__("fldcw %0" : : "m" (fcw));

    /* Init SSE */
    x = x86_get_cr4();
    x |= X86_CR4_OSXMMEXPT;
    x |= X86_CR4_OSFXSR;
    x &= ~X86_CR4_OSXSAVE;
    x86_set_cr4(x);

    __asm__ __volatile__("stmxcsr %0" : "=m" (mxcsr));
#if FPU_MASK_ALL_EXCEPTIONS
    /* mask all exceptions */
    mxcsr = (0x3f << 7);
#else
    /* unmask all exceptions */
    mxcsr &= 0x0000003f;
#endif
    __asm__ __volatile__("ldmxcsr %0" : : "m" (mxcsr));

    /* save fpu initial states, and used when new thread creates */
    /* TODO: construct this statically instead of making a copy from the state of the cpu here */
    __asm__ __volatile__("fxsave %0" : "=m" (fpu_init_states));

    /* disable the fpu by default */
    set_fpu_enabled(false);
}

void fpu_init_thread_states(thread_t *t)
{
    t->arch.fpu_states = (vaddr_t *)ROUNDUP(((vaddr_t)t->arch.fpu_buffer), 16);
    memcpy(t->arch.fpu_states, fpu_init_states, sizeof(fpu_init_states));
}

static inline void set_fpu_enabled(bool enabled)
{
    DEBUG_ASSERT(arch_ints_disabled());

    if (enabled) {
        __asm__ volatile("clts");
    } else {
        x86_set_cr0(x86_get_cr0() | X86_CR0_TS);
    }
}

static inline bool is_fpu_enabled(void)
{
    return !(x86_get_cr0() & X86_CR0_TS);
}

void fpu_context_switch(thread_t *old_thread, thread_t *new_thread)
{
    if (unlikely(fp_supported == 0))
        return;

    if (is_fpu_enabled()) {
        LTRACEF("need to save state on thread %s, state ptr %p\n", old_thread->name, old_thread->arch.fpu_states);
        __asm__ __volatile__("fxsave %0" : "=m" (*old_thread->arch.fpu_states));
        set_fpu_enabled(false);
    }
}

void fpu_dev_na_handler(void)
{
    if (fp_supported == 0)
        return;

    LTRACEF("thread %p '%s' cpu %u\n", get_current_thread(), get_current_thread()->name, arch_curr_cpu_num());

    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!is_fpu_enabled());

    /* restore the thread fpu state */
    set_fpu_enabled(true);
    thread_t *t = get_current_thread();
    __asm__ __volatile__("fxrstor %0" : : "m" (*t->arch.fpu_states));
}

/* End of file */
