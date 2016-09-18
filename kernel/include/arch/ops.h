// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* #defines for the cache routines below */
#define ICACHE 1
#define DCACHE 2
#define UCACHE (ICACHE|DCACHE)

#ifndef ASSEMBLY

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* fast routines that most arches will implement inline */
static void arch_enable_ints(void);
static void arch_disable_ints(void);
static bool arch_ints_disabled(void);
static bool arch_in_int_handler(void);

static int atomic_swap(volatile int *ptr, int val);
static int atomic_add(volatile int *ptr, int val);
static int atomic_and(volatile int *ptr, int val);
static int atomic_or(volatile int *ptr, int val);
static bool atomic_cmpxchg(volatile int *ptr, int *oldval, int newval);
static int atomic_load(volatile int *ptr);
static void atomic_store(volatile int *ptr, int newval);

static uint32_t arch_cycle_count(void);

static uint arch_curr_cpu_num(void);
static uint arch_max_num_cpus(void);

/* Use to align structures on cache lines to avoid cpu aliasing. */
#define __CPU_ALIGN __ALIGNED(CACHE_LINE)

void arch_disable_cache(uint flags);
void arch_enable_cache(uint flags);

void arch_clean_cache_range(addr_t start, size_t len);
void arch_clean_invalidate_cache_range(addr_t start, size_t len);
void arch_invalidate_cache_range(addr_t start, size_t len);
void arch_sync_cache_range(addr_t start, size_t len);

/* Used to suspend work on a CPU until it is further shutdown.
 * This will only be invoked with interrupts disabled.  This function
 * must not re-enter the scheduler.
 * flush_done should be signaled after state is flushed. */
typedef struct event event_t;
void arch_flush_state_and_halt(event_t *flush_done) __NO_RETURN;

void arch_idle(void);

/* function to call in spinloops to idle */
static void arch_spinloop_pause(void);
/* function to call when an event happens that may trigger the exit from
 * a spinloop */
static void arch_spinloop_signal(void);

/* arch optimized version of a page zero routine against a page aligned buffer */
void arch_zero_page(void *);

/* give the specific arch a chance to override some routines */
#include <arch/arch_ops.h>

/* if the arch specific code doesn't override these, implement
 * atomics with compiler builtins.
 */
#if ARCH_IMPLEMENTS_ATOMICS
#error "only built-in atomics supported"
#endif

/* strongly ordered versions of the atomic routines as implemented
 * by the compiler with arch-dependent memory barriers.
 */
static inline int atomic_swap(volatile int *ptr, int val)
{
    return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int atomic_add(volatile int *ptr, int val)
{
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int atomic_and(volatile int *ptr, int val)
{
    return __atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int atomic_or(volatile int *ptr, int val)
{
    return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cmpxchg(volatile int *ptr, int *oldval, int newval)
{
    return __atomic_compare_exchange_n(ptr, oldval, newval, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int atomic_load(volatile int *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void atomic_store(volatile int *ptr, int newval)
{
    __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);
}

/* relaxed versions of the above */
static inline int atomic_swap_relaxed(volatile int *ptr, int val)
{
    return __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED);
}

static inline int atomic_add_relaxed(volatile int *ptr, int val)
{
    return __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED);
}

static inline int atomic_and_relaxed(volatile int *ptr, int val)
{
    return __atomic_fetch_and(ptr, val, __ATOMIC_RELAXED);
}

static inline int atomic_or_relaxed(volatile int *ptr, int val)
{
    return __atomic_fetch_or(ptr, val, __ATOMIC_RELAXED);
}

static inline bool atomic_cmpxchg_relaxed(volatile int *ptr, int *oldval, int newval)
{
    return __atomic_compare_exchange_n(ptr, oldval, newval, false,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static int atomic_load_relaxed(volatile int *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static void atomic_store_relaxed(volatile int *ptr, int newval)
{
    __atomic_store_n(ptr, newval, __ATOMIC_RELAXED);
}

static inline int atomic_add_release(volatile int *ptr, int val)
{
    return __atomic_fetch_add(ptr, val, __ATOMIC_RELEASE);
}

static inline void atomic_fence_acquire(void)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

// 64-bit versions. Assumes the compiler/platform is LLP so int is 32 bits.

static inline int64_t atomic_swap_64(volatile int64_t *ptr, int64_t val)
{
    return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_add_64(volatile int64_t *ptr, int64_t val)
{
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_and_64(volatile int64_t *ptr, int64_t val)
{
    return __atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_or_64(volatile int64_t *ptr, int64_t val)
{
    return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cmpxchg_64(volatile int64_t *ptr, int64_t *oldval,
                                     int64_t newval)
{
    return __atomic_compare_exchange_n(ptr, oldval, newval, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_load_64(volatile int64_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_64(volatile int64_t *ptr, int64_t newval)
{
    __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_swap_u64(volatile uint64_t *ptr, uint64_t val)
{
    return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_add_u64(volatile uint64_t *ptr, uint64_t val)
{
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_and_u64(volatile uint64_t *ptr, uint64_t val)
{
    return __atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_or_u64(volatile uint64_t *ptr, uint64_t val)
{
    return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cmpxchg_u64(volatile uint64_t *ptr, uint64_t *oldval,
                                      uint64_t newval)
{
    return __atomic_compare_exchange_n(ptr, oldval, newval, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_load_u64(volatile uint64_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_u64(volatile uint64_t *ptr, uint64_t newval)
{
    __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);
}

__END_CDECLS

#endif // !ASSEMBLY
