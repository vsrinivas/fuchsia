// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_ATOMIC_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_ATOMIC_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// strongly ordered versions of the atomic routines as implemented
// by the compiler with arch-dependent memory barriers.

static inline int atomic_load(volatile int* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }

static inline void atomic_store(volatile int* ptr, int newval) {
  __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);
}

// relaxed versions of the above
static inline int atomic_load_relaxed(volatile int* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static inline void atomic_store_relaxed(volatile int* ptr, int newval) {
  __atomic_store_n(ptr, newval, __ATOMIC_RELAXED);
}

static inline uint32_t atomic_load_u32(volatile uint32_t* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

// 64-bit versions. Assumes the compiler/platform is LLP so int is 32 bits.

static inline uint64_t atomic_or_u64(volatile uint64_t* ptr, uint64_t val) {
  return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_load_u64(volatile uint64_t* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_ATOMIC_H_
