// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

// strongly ordered versions of the atomic routines as implemented
// by the compiler with arch-dependent memory barriers.
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

// relaxed versions of the above
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

static inline uint64_t atomic_load_u64_relaxed(volatile uint64_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static inline void atomic_store_u64(volatile uint64_t *ptr, uint64_t newval)
{
    __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);
}

__END_CDECLS
