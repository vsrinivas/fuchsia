// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/lockup_detector.h"

#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/relaxed_atomic.h>
#include <platform.h>
#include <zircon/time.h>

#include <fbl/auto_call.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>

namespace {

// Controls whether the lockup detector is enabled and at how long is "too long".
//
// A value of 0 disables the lockup detector.
//
// This value is expressed in units of ticks rather than nanoseconds because it is faster to read
// the platform timer's tick count than to get current_time().
RelaxedAtomic<zx_ticks_t> threshold_ticks = 0;

zx_duration_t TicksToDuration(zx_ticks_t ticks) {
  return platform_get_ticks_to_time_ratio().Scale(ticks);
}

zx_ticks_t DurationToTicks(zx_duration_t duration) {
  return platform_get_ticks_to_time_ratio().Inverse().Scale(duration);
}

}  // namespace

void lockup_init() {
  // TODO(maniscalco): Set a default threshold that is high enough that it won't erronously trigger
  // under qemu.  Alternatively, set an aggressive default threshold in code and override in
  // virtualized environments and scripts that start qemu.
  constexpr uint64_t kDefaultThresholdMsec = 0;
  const zx_duration_t lockup_duration =
      ZX_MSEC(gCmdline.GetUInt64("kernel.lockup-detector.threshold-ms", kDefaultThresholdMsec));
  const zx_ticks_t ticks = DurationToTicks(lockup_duration);
  threshold_ticks.store(ticks);

  if constexpr (!DEBUG_ASSERT_IMPLEMENTED) {
    dprintf(INFO, "lockup_detector: disabled\n");
    return;
  }

  if (ticks > 0) {
    dprintf(INFO, "lockup_detector: threshold is %" PRId64 " ticks (%" PRId64 " ns)\n", ticks,
            lockup_duration);
  } else {
    dprintf(INFO, "lockup_detector: disabled by threshold\n");
  }
}

zx_ticks_t lockup_get_threshold_ticks() { return threshold_ticks.load(); }

void lockup_set_threshold_ticks(zx_ticks_t ticks) { threshold_ticks.store(ticks); }

void lockup_begin() {
  if (threshold_ticks.load() == 0) {
    // Lockup detector is disabled.
    return;
  }

  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;
  state->critical_section_depth++;
  if (state->critical_section_depth != 1) {
    // We must be in a nested critical section.  Do nothing so that only the outermost critical
    // section is measured.
    return;
  }

  const zx_ticks_t now = current_ticks();
  state->begin_ticks.store(now, ktl::memory_order_relaxed);
}

void lockup_end() {
  if (threshold_ticks.load() == 0) {
    // Lockup detector is disabled.
    return;
  }

  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;

  // Defer decrementing until the very end of this function to protect against reentrancy hazards
  // from the calls to `KERNEL_OOPS` and `Thread::Current::PrintBacktrace`.
  auto cleanup = fbl::MakeAutoCall([state]() {
    DEBUG_ASSERT(state->critical_section_depth > 0);
    state->critical_section_depth--;
  });

  if (state->critical_section_depth != 1) {
    // We must be in a nested critical section.  Do nothing so that only the outermost critical
    // section is measured.
    return;
  }

  const zx_ticks_t begin_ticks = state->begin_ticks.load(ktl::memory_order_relaxed);
  // Was a begin time recorded?
  if (begin_ticks == 0) {
    // Nope, nothing to clear.
    return;
  }

  // Check and clear.  Was the threshold exceeded?
  const zx_ticks_t now = current_ticks();
  const zx_ticks_t ticks = (now - begin_ticks);
  state->begin_ticks.store(0, ktl::memory_order_relaxed);
  if (ticks < threshold_ticks.load()) {
    return;
  }

  // Threshold exceeded.
  const zx_duration_t duration = TicksToDuration(ticks);
  const cpu_num_t cpu = arch_curr_cpu_num();
  KERNEL_OOPS("CPU-%u in critical section for %" PRId64 " ms, start=%" PRId64 " now=%" PRId64 "\n",
              cpu, duration / ZX_MSEC(1), begin_ticks, now);
  Thread::Current::PrintBacktrace();
}

namespace {

void lockup_status() {
  const zx_ticks_t ticks = threshold_ticks.load();
  printf("threshold is %" PRId64 " ticks (%" PRId64 " ns)\n", ticks, TicksToDuration(ticks));
  if (ticks == 0) {
    // No threshold set, nothing else to print.
    return;
  }

  for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
    if (!mp_is_cpu_active(i)) {
      printf("CPU-%u is not active, skipping\n", i);
      continue;
    }
    const zx_ticks_t begin_ticks =
        percpu::Get(i).lockup_detector_state.begin_ticks.load(ktl::memory_order_relaxed);
    const zx_ticks_t now = current_ticks();
    if (begin_ticks == 0) {
      printf("CPU-%u not in critical section\n", i);
    } else {
      zx_duration_t duration = TicksToDuration(now - begin_ticks);
      printf("CPU-%u in critical section for %" PRId64 " ms\n", i, duration / ZX_MSEC(1));
    }
  }
}

// Trigger a temporary lockup of |cpu| for |duration|.
void lockup_spin(cpu_num_t cpu, zx_duration_t duration) {
  thread_start_routine spin = [](void* arg) -> int {
    const zx_duration_t duration = *reinterpret_cast<zx_duration_t*>(arg);
    // Acquire a spinlock and hold it for |duration|.
    DECLARE_SINGLETON_SPINLOCK(lockup_test_lock);
    Guard<SpinLock, IrqSave> guard{lockup_test_lock::Get()};
    const zx_time_t deadline = zx_time_add_duration(current_time(), duration);
    while (current_time() < deadline) {
      arch::Yield();
    }
    return 0;
  };
  Thread* t = Thread::Create("lockup spin", spin, &duration, DEFAULT_PRIORITY);
  t->SetCpuAffinity(cpu_num_to_mask(cpu));
  t->Resume();
  t->Join(nullptr, ZX_TIME_INFINITE);
}

int cmd_lockup(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s status                            : print lockup detector status\n", argv[0].str);
    printf("%s test <cpu> <num msec>             : trigger a lockup on <cpu> for <num msec>\n",
           argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "status")) {
    lockup_status();
  } else if (!strcmp(argv[1].str, "test")) {
    if (argc < 4) {
      goto usage;
    }
    const auto cpu = static_cast<cpu_num_t>(argv[2].u);
    const auto ms = static_cast<uint32_t>(argv[3].u);
    printf("locking up CPU %u for %u ms\n", cpu, ms);
    lockup_spin(cpu, ZX_MSEC(ms));
    printf("done\n");
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND("lockup", "lockup detector commands", &cmd_lockup)
STATIC_COMMAND_END(lockup)
