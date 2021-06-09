// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/arch/intrin.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#include <arch/ops.h>
#include <dev/hw_watchdog.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/brwlock.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <ktl/type_traits.h>

#include "tests.h"

const size_t BUFSIZE = (512 * 1024);  // must be smaller than max allowed heap allocation
const size_t ITER =
    (1UL * 1024 * 1024 * 1024 / BUFSIZE);  // enough iterations to have to copy/set 1GB of memory

__NO_INLINE static void bench_cycles_per_second() {
  {
    InterruptDisableGuard irqd;
    const zx_ticks_t before_ticks = current_ticks();
    const uint64_t before_cycles = arch::Cycles();
    for (size_t i = 0; i < 100000000; i++) {
      __asm__ volatile("");
    }
    const zx_ticks_t after_ticks = current_ticks();
    const uint64_t after_cycles = arch::Cycles();
    const zx_duration_t delta_time =
        platform_get_ticks_to_time_ratio().Scale(after_ticks - before_ticks);
    const uint64_t delta_cycles = after_cycles - before_cycles;
    printf("%" PRIu64 " cycles per second (%" PRIu64 " cycles in %" PRId64 " ns)\n",
           (delta_cycles * ZX_SEC(1) / delta_time), delta_cycles, delta_time);
  }
}

__NO_INLINE static void bench_set_overhead() {
  uint32_t* buf = (uint32_t*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      __asm__ volatile("");
    }
    count = arch::Cycles() - count;
  }

  printf("took %" PRIu64 " cycles overhead to loop %zu times\n", count, ITER);

  free(buf);
}

__NO_INLINE static void bench_memset() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      memset(buf, 0, BUFSIZE);
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to memset a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_memset_per_page() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE; j += PAGE_SIZE) {
        memset(buf + j, 0, PAGE_SIZE);
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to per-page memset a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_zero_page() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE; j += PAGE_SIZE) {
        arch_zero_page(buf + j);
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to arch_zero_page a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

template <typename T>
__NO_INLINE static void bench_cset() {
  T* buf = (T*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE / sizeof(T); j++) {
        buf[j] = 0;
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to clear a buffer using wordsize %zu of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, sizeof(*buf), BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000,
         bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_cset_wide() {
  uint32_t* buf = (uint32_t*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE / sizeof(*buf) / 8; j++) {
        buf[j * 8] = 0;
        buf[j * 8 + 1] = 0;
        buf[j * 8 + 2] = 0;
        buf[j * 8 + 3] = 0;
        buf[j * 8 + 4] = 0;
        buf[j * 8 + 5] = 0;
        buf[j * 8 + 6] = 0;
        buf[j * 8 + 7] = 0;
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to clear a buffer of size %zu %zu times 8 words at a time "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_memcpy() {
  uint8_t* buf = (uint8_t*)calloc(1, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: calloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      memcpy(buf, buf + BUFSIZE / 2, BUFSIZE / 2);
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE / 2 * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to memcpy a buffer of size %zu %zu times "
         "(%zu source bytes), %" PRIu64 ".%03" PRIu64 " source bytes/cycle\n",
         count, BUFSIZE / 2, ITER, BUFSIZE / 2 * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

template <typename SpinLockType>
__NO_INLINE static void bench_spinlock(const char* spin_lock_name) {
  interrupt_saved_state_t state;
  SpinLockType lock;
  uint64_t c;

#define COUNT (128 * 1024 * 1024)
  // test 1: acquire/release a spinlock with interrupts already disabled
  {
    InterruptDisableGuard irqd;

    c = arch::Cycles();
    for (size_t i = 0; i < COUNT; i++) {
      if constexpr (ktl::is_same_v<SpinLockType, MonitoredSpinLock>) {
        lock.Acquire(SOURCE_TAG);
      } else {
        lock.Acquire();
      }
      lock.Release();
    }
    c = arch::Cycles() - c;
  }

  printf("%" PRIu64 " cycles to acquire/release %s %d times (%" PRIu64 " cycles per)\n", c,
         spin_lock_name, COUNT, c / COUNT);

  // test 2: acquire/release a spinlock with irq save and irqs already disabled
  {
    InterruptDisableGuard irqd;

    c = arch::Cycles();
    for (size_t i = 0; i < COUNT; i++) {
      if constexpr (ktl::is_same_v<SpinLockType, MonitoredSpinLock>) {
        lock.AcquireIrqSave(state, SOURCE_TAG);
      } else {
        lock.AcquireIrqSave(state);
      }
      lock.ReleaseIrqRestore(state);
    }
    c = arch::Cycles() - c;
  }

  printf("%" PRIu64 " cycles to acquire/release %s w/irqsave (already disabled) %d times (%" PRIu64
         " cycles per)\n",
         c, spin_lock_name, COUNT, c / COUNT);

  // test 2: acquire/release a spinlock with irq save and irqs enabled
  c = arch::Cycles();
  for (size_t i = 0; i < COUNT; i++) {
    if constexpr (ktl::is_same_v<SpinLockType, MonitoredSpinLock>) {
      lock.AcquireIrqSave(state, SOURCE_TAG);
    } else {
      lock.AcquireIrqSave(state);
    }
    lock.ReleaseIrqRestore(state);
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64 " cycles to acquire/release %s w/irqsave %d times (%" PRIu64 " cycles per)\n",
         c, spin_lock_name, COUNT, c / COUNT);
#undef COUNT
}

__NO_INLINE static void bench_mutex() {
  Mutex m;

  static const uint count = 128 * 1024 * 1024;
  uint64_t c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    m.Acquire();
    m.Release();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64 " cycles to acquire/release uncontended mutex %u times (%" PRIu64
         " cycles per)\n",
         c, count, c / count);
}

template <typename LockType>
__NO_INLINE static void bench_rwlock() {
  LockType rw;
  static const uint count = 128 * 1024 * 1024;
  uint64_t c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    rw.ReadAcquire();
    rw.ReadRelease();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64
         " cycles to acquire/release uncontended brwlock(PI: %d) for read %u times (%" PRIu64
         " cycles per)\n",
         c, ktl::is_same_v<LockType, BrwLockPi>, count, c / count);

  c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    rw.WriteAcquire();
    rw.WriteRelease();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64
         " cycles to acquire/release uncontended brwlock(PI: %d) for write %u times (%" PRIu64
         " cycles per)\n",
         c, ktl::is_same_v<LockType, BrwLockPi>, count, c / count);
}

__NO_INLINE static void bench_heap() {
  constexpr size_t kHeapToUse = 256 * MB;
  constexpr size_t kAllocSizes[] = {256, KB, 2 * KB};

  for (const auto& alloc_size : kAllocSizes) {
    const size_t num_allocs = kHeapToUse / alloc_size;

    uint64_t before_alloc = arch::Cycles();
    uint64_t after_alloc;

    {
      size_t** alloc_chain = nullptr;
      auto cleanup = fit::defer([&alloc_chain]() {
        while (alloc_chain) {
          size_t** next_alloc = reinterpret_cast<size_t**>(*alloc_chain);
          free(alloc_chain);
          alloc_chain = next_alloc;
        }
      });

      for (size_t i = 0; i < num_allocs; i++) {
        size_t** next_alloc = reinterpret_cast<size_t**>(malloc(alloc_size));
        if (!next_alloc) {
          printf("Allocation failed during %s\n", __FUNCTION__);
          return;
        }
        *next_alloc = reinterpret_cast<size_t*>(alloc_chain);
        alloc_chain = next_alloc;
      }
      after_alloc = arch::Cycles();
      // End the block to trigger cleanup and free.
    }
    uint64_t after_free = arch::Cycles();

    printf("Heap test using %zu allocations of %zu bytes took %" PRIu64
           " cycles to allocate and %" PRIu64 " cycles to free\n",
           num_allocs, alloc_size, after_alloc - before_alloc, after_free - after_alloc);
  }
}

int benchmarks(int, const cmd_args*, uint32_t) {
  // Disable the hardware watchdog (if present and enabled) because some of these benchmarks will
  // disable interrupts for extended periods of time.
  bool need_to_reenable = false;
  if (hw_watchdog_present() && hw_watchdog_is_enabled()) {
    hw_watchdog_set_enabled(false);
    need_to_reenable = true;
  }
  auto reenable_hw_watchdog = fit::defer([need_to_reenable]() {
    if (need_to_reenable) {
      hw_watchdog_set_enabled(true);
    }
  });

  // Ensure that benchmarks aren't impacted by preemption.
  AutoPreemptDisabler preempt_disabler;

  bench_cycles_per_second();
  bench_set_overhead();
  bench_heap();
  bench_memcpy();
  bench_memset();

  bench_memset_per_page();
  bench_zero_page();

  bench_cset<uint8_t>();
  bench_cset<uint16_t>();
  bench_cset<uint32_t>();
  bench_cset<uint64_t>();
  bench_cset_wide();
  bench_spinlock<SpinLock>("SpinLock");
  bench_spinlock<MonitoredSpinLock>("MonitoredSpinLock");
  bench_mutex();
  bench_rwlock<BrwLockPi>();
  bench_rwlock<BrwLockNoPi>();

  return 0;
}
