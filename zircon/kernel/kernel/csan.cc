// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>

typedef char __tsan_atomic8;
typedef short __tsan_atomic16;
typedef int __tsan_atomic32;
typedef long __tsan_atomic64;
typedef __int128 __tsan_atomic128;

// Part of ABI, do not change.
// http://llvm.org/viewvc/llvm-project/libcxx/trunk/include/atomic?view=markup
typedef enum {
  __tsan_memory_order_relaxed,
  __tsan_memory_order_consume,
  __tsan_memory_order_acquire,
  __tsan_memory_order_release,
  __tsan_memory_order_acq_rel,
  __tsan_memory_order_seq_cst
} __tsan_memory_order;

extern "C" {

__attribute__((no_sanitize_thread)) void __tsan_read1(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read2(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read2(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read4(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read4(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read8(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read8(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read16(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_read_range(void *addr, unsigned long size) {}

__attribute__((no_sanitize_thread)) void __tsan_write1(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_write2(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write2(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_write4(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write4(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_write8(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write8(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_write16(void *addr) {}
__attribute__((no_sanitize_thread)) void __tsan_write_range(void *addr, unsigned long size) {}

__attribute__((no_sanitize_thread)) void __tsan_vptr_update(void **vptr_p, void *val) {}
__attribute__((no_sanitize_thread)) void __tsan_vptr_read(void **vptr_p) {}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_load(
    const volatile __tsan_atomic8 *addr, __tsan_memory_order order) {
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic8_store(volatile __tsan_atomic8 *addr,
                                                              __tsan_atomic8 v,
                                                              __tsan_memory_order order) {
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic16 __tsan_atomic16_load(
    const volatile __tsan_atomic16 *addr, __tsan_memory_order order) {
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic16 __tsan_atomic16_fetch_or(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order order) {
  return __atomic_fetch_or(a, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_load(
    const volatile __tsan_atomic32 *addr, __tsan_memory_order order) {
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic32_store(volatile __tsan_atomic32 *addr,
                                                               __tsan_atomic32 v,
                                                               __tsan_memory_order order) {
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_or(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  return __atomic_fetch_or(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_and(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  return __atomic_fetch_and(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_add(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  return __atomic_fetch_add(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_sub(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  return __atomic_fetch_sub(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_load(
    const volatile __tsan_atomic64 *addr, __tsan_memory_order order) {
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic64_store(volatile __tsan_atomic64 *addr,
                                                               __tsan_atomic64 v,
                                                               __tsan_memory_order order) {
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_or(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  return __atomic_fetch_or(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_add(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  return __atomic_fetch_add(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_sub(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  return __atomic_fetch_sub(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic128 __tsan_atomic128_load(
    const volatile __tsan_atomic128 *addr, __tsan_memory_order order) {
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic128_store(volatile __tsan_atomic128 *addr,
                                                                __tsan_atomic128 v,
                                                                __tsan_memory_order order) {
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_exchange(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order order) {
  return __atomic_exchange_n(a, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic8_compare_exchange_strong(
    volatile __tsan_atomic8 *a, __tsan_atomic8 *c, __tsan_atomic8 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_compare_exchange_val(
  volatile __tsan_atomic8 *a, __tsan_atomic8 c, __tsan_atomic8 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  __atomic_compare_exchange_n(a, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_exchange(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order order) {
  return __atomic_exchange_n(a, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic32_compare_exchange_weak(
    volatile __tsan_atomic32 *a, __tsan_atomic32 *c, __tsan_atomic32 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, true, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic32_compare_exchange_strong(
    volatile __tsan_atomic32 *a, __tsan_atomic32 *c, __tsan_atomic32 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_compare_exchange_val(
  volatile __tsan_atomic32 *a, __tsan_atomic32 c, __tsan_atomic32 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  __atomic_compare_exchange_n(a, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_exchange(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order order) {
  return __atomic_exchange_n(a, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic64_compare_exchange_weak(
    volatile __tsan_atomic64 *a, __tsan_atomic64 *c, __tsan_atomic64 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, true, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic64_compare_exchange_strong(
    volatile __tsan_atomic64 *a, __tsan_atomic64 *c, __tsan_atomic64 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_compare_exchange_val(
  volatile __tsan_atomic64 *a, __tsan_atomic64 c, __tsan_atomic64 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  __atomic_compare_exchange_n(a, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) int __tsan_atomic128_compare_exchange_strong(
    volatile __tsan_atomic128 *a, __tsan_atomic128 *c, __tsan_atomic128 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  return __atomic_compare_exchange_n(a, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic128 __tsan_atomic128_compare_exchange_val(
  volatile __tsan_atomic128 *a, __tsan_atomic128 c, __tsan_atomic128 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  __atomic_compare_exchange_n(a, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) void __tsan_atomic_thread_fence(__tsan_memory_order order) {
  __atomic_thread_fence(order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic_signal_fence(__tsan_memory_order order) {
  __atomic_signal_fence(order);
}

void __tsan_init(void) {}
void __tsan_func_entry(void *call_pc) {}
void __tsan_func_exit(void *call_pc) {}

}  // extern "C"
