// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef ASSEMBLY

#include <stdbool.h>
#include <compiler.h>
#include <reg.h>
#include <arch/arm.h>

__BEGIN_CDECLS

#if ARM_ISA_ARMV7 || (ARM_ISA_ARMV6 && !__thumb__)
#define ENABLE_CYCLE_COUNTER 1

// override of some routines
static inline void arch_enable_ints(void)
{
    CF;
    __asm__ volatile("cpsie i");
}

static inline void arch_disable_ints(void)
{
    __asm__ volatile("cpsid i");
    CF;
}

static inline bool arch_ints_disabled(void)
{
    unsigned int state;

#if ARM_ISA_ARMV7M
    __asm__ volatile("mrs %0, primask" : "=r"(state));
    state &= 0x1;
#else
    __asm__ volatile("mrs %0, cpsr" : "=r"(state));
    state &= (1<<7);
#endif

    return !!state;
}

static inline void arch_enable_fiqs(void)
{
    CF;
    __asm__ volatile("cpsie f");
}

static inline void arch_disable_fiqs(void)
{
    __asm__ volatile("cpsid f");
    CF;
}

static inline bool arch_fiqs_disabled(void)
{
    unsigned int state;

    __asm__ volatile("mrs %0, cpsr" : "=r"(state));
    state &= (1<<6);

    return !!state;
}

static inline bool arch_in_int_handler(void)
{
    /* set by the interrupt glue to track that the cpu is inside a handler */
    extern bool __arm_in_handler;

    return __arm_in_handler;
}

static inline void arch_spinloop_pause(void)
{
    __asm__ volatile("wfe");
}

static inline void arch_spinloop_signal(void)
{
    __asm__ volatile("sev");
}

static inline uint32_t arch_cycle_count(void)
{
#if ARM_ISA_ARMV7M
#if ENABLE_CYCLE_COUNTER
#define DWT_CYCCNT (0xE0001004)
    return *REG32(DWT_CYCCNT);
#else
    return 0;
#endif
#elif ARM_ISA_ARMV7
    uint32_t count;
    __asm__ volatile("mrc		p15, 0, %0, c9, c13, 0"
                     : "=r" (count)
                    );
    return count;
#else
//#warning no arch_cycle_count implementation
    return 0;
#endif
}

#if WITH_SMP && ARM_ISA_ARMV7
static inline uint arch_curr_cpu_num(void)
{
    uint32_t mpidr = arm_read_mpidr();
    return ((mpidr & ((1U << SMP_CPU_ID_BITS) - 1)) >> 8 << SMP_CPU_CLUSTER_SHIFT) | (mpidr & 0xff);
}

extern uint arm_num_cpus;
static inline uint arch_max_num_cpus(void)
{
    return arm_num_cpus;
}
#else
static inline uint arch_curr_cpu_num(void)
{
    return 0;
}
static inline uint arch_max_num_cpus(void)
{
    return 1;
}
#endif

/* defined in kernel/thread.h */

#if !ARM_ISA_ARMV7M
/* use the cpu local thread context pointer to store current_thread */
static inline struct thread *get_current_thread(void)
{
    return (struct thread *)arm_read_tpidrprw();
}

static inline void set_current_thread(struct thread *t)
{
    arm_write_tpidrprw((uint32_t)t);
}
#else // ARM_ISA_ARM7M

/* use a global pointer to store the current_thread */
extern struct thread *_current_thread;

static inline struct thread *get_current_thread(void)
{
    return _current_thread;
}

static inline void set_current_thread(struct thread *t)
{
    _current_thread = t;
}

#endif // !ARM_ISA_ARMV7M

#elif ARM_ISA_ARMV6M // cortex-m0 cortex-m0+

/* the builtins are not implemented in this case, so implement them manually here */
#define ARCH_IMPLEMENTS_ATOMICS 1

static inline void arch_enable_fiqs(void)
{
    CF;
    __asm__ volatile("cpsie f");
}

static inline void arch_disable_fiqs(void)
{
    __asm__ volatile("cpsid f");
    CF;
}

static inline bool arch_fiqs_disabled(void)
{
    unsigned int state;

    __asm__ volatile("mrs %0, cpsr" : "=r"(state));
    state &= (1<<6);

    return !!state;
}

static inline void arch_enable_ints(void)
{
    CF;
    __asm__ volatile("cpsie i");
}
static inline void arch_disable_ints(void)
{
    __asm__ volatile("cpsid i");
    CF;
}

static inline bool arch_ints_disabled(void)
{
    unsigned int state;

    __asm__ volatile("mrs %0, primask" : "=r"(state));
    state &= 0x1;
    return !!state;
}

static inline int atomic_add(volatile int *ptr, int val)
{
    int temp;
    bool state;

    state = arch_ints_disabled();
    arch_disable_ints();
    temp = *ptr;
    *ptr = temp + val;
    if (!state)
        arch_enable_ints();
    return temp;
}

static inline  int atomic_and(volatile int *ptr, int val)
{
    int temp;
    bool state;

    state = arch_ints_disabled();
    arch_disable_ints();
    temp = *ptr;
    *ptr = temp & val;
    if (!state)
        arch_enable_ints();
    return temp;
}

static inline int atomic_or(volatile int *ptr, int val)
{
    int temp;
    bool state;

    state = arch_ints_disabled();
    arch_disable_ints();
    temp = *ptr;
    *ptr = temp | val;
    if (!state)
        arch_enable_ints();
    return temp;
}

static inline int atomic_swap(volatile int *ptr, int val)
{
    int temp;
    bool state;

    state = arch_ints_disabled();
    arch_disable_ints();
    temp = *ptr;
    *ptr = val;
    if (!state)
        arch_enable_ints();
    return temp;
}

static inline bool atomic_cmpxchg(volatile int *ptr, int *oldval, int newval)
{
    int temp;
    bool state;
    bool success;

    state = arch_ints_disabled();
    arch_disable_ints();
    temp = *ptr;
    if (temp == oldval) {
        *ptr = newval;
        success = true;
    } else {
        *oldval = temp;
        success = false;
    }
    if (!state)
        arch_enable_ints();
    return success;
}

static inline int atomic_load(volatile int *ptr)
{
    return *ptr;
}

static inline void atomic_store(volatile int *ptr, int newval)
{
    *ptr = newval;
}

static inline int atomic_swap_relaxed(volatile int *ptr, int val)
{
    return atomic_swap(ptr, val);
}

static inline int atomic_add_relaxed(volatile int *ptr, int val)
{
    return atomic_add(ptr, val);
}

static inline int atomic_and_relaxed(volatile int *ptr, int val)
{
    return atomic_and(ptr, val);
}

static inline int atomic_or_relaxed(volatile int *ptr, int val)
{
    return atomic_or(ptr, val);
}

static inline bool atomic_cmpxchg_relaxed(volatile int *ptr, int *oldval, int newval)
{
    return atomic_cmpxchg(ptr, oldval, newval);
}

static int atomic_load_relaxed(volatile int *ptr)
{
    return atomic_load(ptr);
}

static void atomic_store_relaxed(volatile int *ptr, int newval)
{
    atomic_store(ptr, newval);
}

static inline uint32_t arch_cycle_count(void)
{
    return 0;
}

static inline uint arch_curr_cpu_num(void)
{
    return 0;
}

/* use a global pointer to store the current_thread */
extern struct thread *_current_thread;

static inline struct thread *get_current_thread(void)
{
    return _current_thread;
}

static inline void set_current_thread(struct thread *t)
{
    _current_thread = t;
}

#else // pre-armv6 || (armv6 & thumb)

/* for pre-armv6 the bodies of these are too big to inline, call an assembly stub version */
void _arch_enable_ints(void);
void _arch_disable_ints(void);

/* the builtins are not implemented in this case, so implement them manually here */
#define ARCH_IMPLEMENTS_ATOMICS 1

int _atomic_add(volatile int *ptr, int val);
int _atomic_and(volatile int *ptr, int val);
int _atomic_or(volatile int *ptr, int val);
int _atomic_add(volatile int *ptr, int val);
int _atomic_swap(volatile int *ptr, int val);
bool _atomic_cmpxchg(volatile int *ptr, int *oldval, int newval);

uint32_t _arch_cycle_count(void);

static inline int atomic_add(volatile int *ptr, int val) { return _atomic_add(ptr, val); }
static inline int atomic_and(volatile int *ptr, int val) { return _atomic_and(ptr, val); }
static inline int atomic_or(volatile int *ptr, int val) { return _atomic_or(ptr, val); }
static inline int atomic_swap(volatile int *ptr, int val) { return _atomic_swap(ptr, val); }
static inline bool atomic_cmpxchg(volatile int *ptr, int *oldval, int newval) { return _atomic_cmpxchg(ptr, oldval, newval); }
static inline void atomic_store(volatile int *ptr, int newval) { DMB; *ptr = newval; DMB; }
static inline int atomic_load(volatile int *ptr) { int v = *ptr; DMB; return v; }
static inline int atomic_add_relaxed(volatile int *ptr, int val) { return _atomic_add(ptr, val); }
static inline int atomic_and_relaxed(volatile int *ptr, int val) { return _atomic_and(ptr, val); }
static inline int atomic_or_relaxed(volatile int *ptr, int val) { return _atomic_or(ptr, val); }
static inline int atomic_swap_relaxed(volatile int *ptr, int val) { return _atomic_swap(ptr, val); }
static inline bool atomic_cmpxchg_relaxed(volatile int *ptr, int *oldval, int newval) { return _atomic_cmpxchg(ptr, oldval, newval); }
static inline void atomic_store_relaxed(volatile int *ptr, int newval) { *ptr = newval; }
static inline int atomic_load_relaxed(volatile int *ptr) { return *ptr; }

static inline void arch_enable_ints(void) { _arch_enable_ints(); }
static inline void arch_disable_ints(void) { _arch_disable_ints(); }

static inline uint32_t arch_cycle_count(void) { return _arch_cycle_count(); }

#endif

#define mb()        DSB
#define wmb()       DSB
#define rmb()       DSB

#ifdef WITH_SMP
#define smp_mb()    DMB
#define smp_wmb()   DMB
#define smp_rmb()   DMB
#else
#define smp_mb()    CF
#define smp_wmb()   CF
#define smp_rmb()   CF
#endif

__END_CDECLS

#endif // ASSEMBLY
