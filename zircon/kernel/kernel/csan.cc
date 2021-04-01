// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>
#include <stdint.h>
#include <kernel/thread.h>
#include <zircon/assert.h>

#include <arch/arch_ops.h>

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

static bool g_disable = true;
static bool g_percpu_disable[SMP_MAX_CPUS] = {};
static unsigned long g_addrs[SMP_MAX_CPUS];

namespace {

__attribute__((no_sanitize_thread)) static void spin_for_a_bit(void) {
  for (int i = 0; i < 12; i++) {
    arch::Yield();
  }
}

__attribute__((no_sanitize_thread)) static void arm(cpu_num_t local_cpu, uintptr_t addr, size_t size) {
  __atomic_store_n(&g_addrs[local_cpu], addr, __ATOMIC_RELEASE);
}

__attribute__((no_sanitize_thread)) static void disarm(cpu_num_t local_cpu, uintptr_t addr, size_t size) {
  __atomic_store_n(&g_addrs[local_cpu], 0, __ATOMIC_RELEASE);
}

__attribute__((no_sanitize_thread)) static bool match(cpu_num_t local_cpu, unsigned long addr, size_t size) {
  return false;
  if (g_disable || g_percpu_disable[local_cpu]) {
    return false;
  }
  for (uint i = 0; i < arch_max_num_cpus(); i++) {
    if (i == local_cpu)
      continue;
    if (__atomic_load_n(&g_addrs[i], __ATOMIC_ACQUIRE) == addr)
      return true;
  }
  return false;
}

template<typename T> __attribute__((no_sanitize_address)) void kcsan_read(void* addr) {
  const cpu_num_t local_cpu = arch_curr_cpu_num();
  if (g_disable || g_percpu_disable[local_cpu]) {
    return;
  }
  Thread::Current::preemption_state().PreemptDisable();
  ZX_ASSERT(match(local_cpu, reinterpret_cast<uintptr_t>(addr), sizeof(T)) == false);
  volatile T t0 = *static_cast<volatile T*>(addr);
  spin_for_a_bit();
  volatile T t1 = *static_cast<volatile T*>(addr);
  ZX_ASSERT_MSG(t0 == t1, "read: t0=%x t1=%x access size %d", t0, t1, sizeof(T));
  Thread::Current::preemption_state().PreemptReenableNoResched();
}

template<typename T> __attribute__((no_sanitize_address)) void kcsan_write(void* addr) {
  const cpu_num_t local_cpu = arch_curr_cpu_num();
  if (g_disable || g_percpu_disable[local_cpu]) {
    return;
  }
  Thread::Current::preemption_state().PreemptDisable();
  arm(local_cpu, reinterpret_cast<uintptr_t>(addr), sizeof(T));
  volatile T t0 = *static_cast<volatile T*>(addr);
  spin_for_a_bit();
  volatile T t1 = *static_cast<volatile T*>(addr);
  ZX_ASSERT_MSG(t0 == t1, "write: t0=%x t1=%x access size %d", t0, t1, sizeof(T));
  disarm(local_cpu, reinterpret_cast<uintptr_t>(addr), sizeof(T));
  Thread::Current::preemption_state().PreemptReenableNoResched();
}

}  // anonymous namespace

__attribute__((no_sanitize_thread)) void kcsan_enable() {
  __atomic_store_n(&g_disable, false, __ATOMIC_RELEASE);
}
__attribute__((no_sanitize_thread)) void kcsan_disable() {
  __atomic_store_n(&g_disable, true, __ATOMIC_RELEASE);
}
__attribute__((no_sanitize_thread)) void kcsan_disable_percpu() {
  Thread::Current::preemption_state().PreemptDisable();
  __atomic_store_n(&g_percpu_disable[arch_curr_cpu_num()], true, __ATOMIC_RELEASE);
}
__attribute__((no_sanitize_thread)) void kcsan_enable_percpu() {
  __atomic_store_n(&g_percpu_disable[arch_curr_cpu_num()], false, __ATOMIC_RELEASE);
  Thread::Current::preemption_state().PreemptReenable();
}

extern "C" {

__attribute__((no_sanitize_thread)) void __tsan_read1(void* addr) {
  kcsan_read<uint8_t>(addr);
}

__attribute__((no_sanitize_thread)) void __tsan_read2(void* addr) {
  kcsan_read<uint16_t>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read2(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read4(void* addr) {
  kcsan_read<uint32_t>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read4(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read8(void *addr) {
  kcsan_read<uint64_t>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_read8(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_read16(void *addr) {
  kcsan_read<__int128>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_read_range(void *addr, unsigned long size) {}

__attribute__((no_sanitize_thread)) void __tsan_write1(void *addr) {
  kcsan_write<uint8_t>(addr);
}

__attribute__((no_sanitize_thread)) void __tsan_write2(void *addr) {
  kcsan_write<uint16_t>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write2(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_write4(void *addr) {
  kcsan_write<uint32_t>(addr);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write4(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_write8(void *addr) {
  // TODO(): Figure out __tsan_write8 error.
  // uint64_t t0 = *(volatile uint64_t *) addr;
  // spin_for_a_bit();
  // uint64_t t1 = *(volatile uint64_t *) addr;
  // ZX_ASSERT_MSG(t0 == t1, "t0=%x t1=%x", t0, t1);
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_write8(void *addr) {}

__attribute__((no_sanitize_thread)) void __tsan_write16(void *addr) {
  kcsan_write<__int128>(addr);
}

__attribute__((no_sanitize_thread)) void __tsan_volatile_read1(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_volatile_write1(void *addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_volatile_read4(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_volatile_write4(void *addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_volatile_read8(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_volatile_write8(void *addr) {
}

__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_read2(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_write2(void *addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_read4(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_write4(void *addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_read8(void* addr) {
}
__attribute__((no_sanitize_thread)) void __tsan_unaligned_volatile_write8(void *addr) {
}

__attribute__((no_sanitize_thread)) void __tsan_write_range(void *addr, unsigned long size) {}

__attribute__((no_sanitize_thread)) void __tsan_vptr_update(void **vptr_p, void *val) {}
__attribute__((no_sanitize_thread)) void __tsan_vptr_read(void **vptr_p) {}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_load(
    const volatile __tsan_atomic8 *addr, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 1) == false);
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic8_store(volatile __tsan_atomic8 *addr,
                                                              __tsan_atomic8 v,
                                                              __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 1) == false);
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic16 __tsan_atomic16_load(
    const volatile __tsan_atomic16 *addr, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 2) == false);
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic16 __tsan_atomic16_fetch_or(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 2) == false);
  return __atomic_fetch_or(a, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_load(
    const volatile __tsan_atomic32 *addr, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic32_store(volatile __tsan_atomic32 *addr,
                                                               __tsan_atomic32 v,
                                                               __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_or(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 4) == false);
  return __atomic_fetch_or(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_and(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 4) == false);
  return __atomic_fetch_and(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_add(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 4) == false);
  return __atomic_fetch_add(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_fetch_sub(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 4) == false);
  return __atomic_fetch_sub(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_load(
    const volatile __tsan_atomic64 *addr, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic64_store(volatile __tsan_atomic64 *addr,
                                                               __tsan_atomic64 v,
                                                               __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_or(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 8) == false);
  return __atomic_fetch_or(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_add(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 8) == false);
  return __atomic_fetch_add(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_fetch_sub(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(a), 8) == false);
  return __atomic_fetch_sub(a, v, mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic128 __tsan_atomic128_load(
    const volatile __tsan_atomic128 *addr, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 16) == false);
  return __atomic_load_n(addr, order);
}

__attribute__((no_sanitize_thread)) void __tsan_atomic128_store(volatile __tsan_atomic128 *addr,
                                                                __tsan_atomic128 v,
                                                                __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 16) == false);
  __atomic_store_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_exchange(
    volatile __tsan_atomic8 *addr, __tsan_atomic8 v, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 1) == false);
  return __atomic_exchange_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic8_compare_exchange_strong(
    volatile __tsan_atomic8 *addr, __tsan_atomic8 *c, __tsan_atomic8 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 1) == false);
  return __atomic_compare_exchange_n(addr, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic8 __tsan_atomic8_compare_exchange_val(
  volatile __tsan_atomic8 *addr, __tsan_atomic8 c, __tsan_atomic8 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 1) == false);
  __atomic_compare_exchange_n(addr, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_exchange(
    volatile __tsan_atomic32 *addr, __tsan_atomic32 v, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  return __atomic_exchange_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic32_compare_exchange_weak(
    volatile __tsan_atomic32 *addr, __tsan_atomic32 *c, __tsan_atomic32 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  return __atomic_compare_exchange_n(addr, c, v, true, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic32_compare_exchange_strong(
    volatile __tsan_atomic32 *addr, __tsan_atomic32 *c, __tsan_atomic32 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  return __atomic_compare_exchange_n(addr, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic32 __tsan_atomic32_compare_exchange_val(
  volatile __tsan_atomic32 *addr, __tsan_atomic32 c, __tsan_atomic32 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 4) == false);
  __atomic_compare_exchange_n(addr, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_exchange(
    volatile __tsan_atomic64 *addr, __tsan_atomic64 v, __tsan_memory_order order) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  return __atomic_exchange_n(addr, v, order);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic64_compare_exchange_weak(
    volatile __tsan_atomic64 *addr, __tsan_atomic64 *c, __tsan_atomic64 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  return __atomic_compare_exchange_n(addr, c, v, true, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) int __tsan_atomic64_compare_exchange_strong(
    volatile __tsan_atomic64 *addr, __tsan_atomic64 *c, __tsan_atomic64 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  return __atomic_compare_exchange_n(addr, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic64 __tsan_atomic64_compare_exchange_val(
  volatile __tsan_atomic64 *addr, __tsan_atomic64 c, __tsan_atomic64 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 8) == false);
  __atomic_compare_exchange_n(addr, &c, v, false, mo, fail_mo);
  return c;
}

__attribute__((no_sanitize_thread)) int __tsan_atomic128_compare_exchange_strong(
    volatile __tsan_atomic128 *addr, __tsan_atomic128 *c, __tsan_atomic128 v, __tsan_memory_order mo,
    __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 16) == false);
  return __atomic_compare_exchange_n(addr, c, v, false, mo, fail_mo);
}

__attribute__((no_sanitize_thread)) __tsan_atomic128 __tsan_atomic128_compare_exchange_val(
  volatile __tsan_atomic128 *addr, __tsan_atomic128 c, __tsan_atomic128 v, __tsan_memory_order mo,
  __tsan_memory_order fail_mo) {
  ZX_ASSERT(match(arch_curr_cpu_num(), reinterpret_cast<uintptr_t>(addr), 16) == false);
  __atomic_compare_exchange_n(addr, &c, v, false, mo, fail_mo);
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
