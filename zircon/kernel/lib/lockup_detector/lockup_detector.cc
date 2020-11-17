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
#include <lib/counters.h>
#include <lib/lockup_detector/state.h>
#include <lib/relaxed_atomic.h>
#include <platform.h>
#include <zircon/time.h>

#include <fbl/auto_call.h>
#include <kernel/event_limiter.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>

// Counter for the number of lockups detected.
KCOUNTER(counter_lockup_cs_count, "lockup_detector.critical_section.count")

// Counters for number of lockups exceeding a given duration.
KCOUNTER(counter_lockup_cs_exceeding_10ms, "lockup_detector.critical_section.exceeding_ms.10")
KCOUNTER(counter_lockup_cs_exceeding_1000ms, "lockup_detector.critical_section.exceeding_ms.1000")
KCOUNTER(counter_lockup_cs_exceeding_100000ms,
         "lockup_detector.critical_section.exceeding_ms.100000")

// Counts the number of times the lockup detector has emitted a "no heartbeat" oops.
KCOUNTER(counter_lockup_no_heartbeat_oops, "lockup_detector.no_heartbeat_oops")

namespace {

// Controls whether critical section checking is enabled and at how long is "too long".
//
// A value of 0 disables the lockup detector for critical sections.
//
// This value is expressed in units of ticks rather than nanoseconds because it is faster to read
// the platform timer's tick count than to get current_time().
RelaxedAtomic<zx_ticks_t> cs_threshold_ticks = 0;

// The period at which CPUs emit heartbeats.  0 means heartbeats are disabled.
ktl::atomic<zx_duration_t> heartbeat_period = 0;

// If a CPU's most recent heartbeat is older than this threshold, it is considered to be locked up
// and a KERNEL OOPS will be triggered.
ktl::atomic<zx_duration_t> heartbeat_threshold = 0;

zx_duration_t TicksToDuration(zx_ticks_t ticks) {
  return platform_get_ticks_to_time_ratio().Scale(ticks);
}

zx_ticks_t DurationToTicks(zx_duration_t duration) {
  return platform_get_ticks_to_time_ratio().Inverse().Scale(duration);
}

// Return an absolute deadline |duration| nanoseconds from now with a jitter of +/- |percent|%.
Deadline DeadlineWithJitterAfter(zx_duration_t duration, uint32_t percent) {
  DEBUG_ASSERT(percent <= 100);
  const zx_duration_t delta = affine::Ratio{(rand() / 100) * percent, RAND_MAX}.Scale(duration);
  return Deadline::after(zx_duration_add_duration(duration, delta));
}

// Provides histogram-like kcounter functionality.
struct CounterBucket {
  const zx_duration_t exceeding;
  const Counter* const counter;
};
constexpr const ktl::array<CounterBucket, 3> cs_counter_buckets = {{
    {ZX_MSEC(10), &counter_lockup_cs_exceeding_10ms},
    {ZX_MSEC(1000), &counter_lockup_cs_exceeding_1000ms},
    {ZX_MSEC(100000), &counter_lockup_cs_exceeding_100000ms},
}};

void RecordCriticalSectionCounters(zx_duration_t lockup_duration) {
  kcounter_add(counter_lockup_cs_count, 1);
  for (auto iter = cs_counter_buckets.rbegin(); iter != cs_counter_buckets.rend(); iter++) {
    if (lockup_duration >= iter->exceeding) {
      kcounter_add(*iter->counter, 1);
      break;
    }
  }
}

bool heartbeats_enabled() {
  return heartbeat_period.load() != 0 && heartbeat_threshold.load() != 0;
}

// Periodic timer callback invoked on secondary CPUs to record a heartbeat.
void heartbeat_callback(Timer* timer, zx_time_t now, void* arg) {
  DEBUG_ASSERT(arch_curr_cpu_num() != BOOT_CPU_ID);
  auto* state = reinterpret_cast<LockupDetectorState*>(arg);
  state->last_heartbeat.store(now);
  if (state->heartbeat_active.load()) {
    timer->Set(Deadline::after(heartbeat_period.load()), heartbeat_callback, arg);
  }
}

EventLimiter<ZX_SEC(10)> alert_limiter;

// Periodic timer that invokes |check_heartbeats_callback|.
Timer check_heartbeats_timer;

  // Periodic timer callback invoked on the primary CPU to check heartbeats of secondary CPUs.
void check_heartbeats_callback(Timer* timer, zx_time_t now, void*) {
  const cpu_num_t current_cpu = arch_curr_cpu_num();
  DEBUG_ASSERT(current_cpu == BOOT_CPU_ID);

  percpu::ForEach([current_cpu, now](cpu_num_t cpu, percpu* percpu_state) {
    if (cpu == current_cpu || !mp_is_cpu_online(cpu) | !mp_is_cpu_active(cpu)) {
      return;
    }
    LockupDetectorState* state = &percpu_state->lockup_detector_state;
    if (!state->heartbeat_active.load()) {
      return;
    }

    const zx_time_t last_heartbeat = state->last_heartbeat.load();
    const zx_duration_t age = zx_time_sub_time(now, last_heartbeat);
    if (age > heartbeat_threshold.load()) {
      if (age > state->max_heartbeat_gap.load()) {
        state->max_heartbeat_gap.store(age);
      }
      if (alert_limiter.Ready()) {
        kcounter_add(counter_lockup_no_heartbeat_oops, 1);
        KERNEL_OOPS("lockup_detector: no heartbeat from CPU-%u in %" PRId64
                    " ms, last_heartbeat=%" PRId64 " now=%" PRId64 " (message rate limited)\n",
                    cpu, age / ZX_MSEC(1), last_heartbeat, now);
        {
          Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};
          percpu::Get(cpu).scheduler.Dump();
        }
        // TODO(maniscalco): Print the contents of cpu's interrupt counters.
      }
    }
  });

  if (heartbeats_enabled()) {
    check_heartbeats_timer.Set(Deadline::after(heartbeat_period.load()), check_heartbeats_callback,
                               nullptr);
  }
}

}  // namespace

void lockup_primary_init() {
  // TODO(maniscalco): Set a default threshold that is high enough that it won't erronously trigger
  // under qemu.  Alternatively, set an aggressive default threshold in code and override in
  // virtualized environments and scripts that start qemu.
  constexpr uint64_t kDefaultThresholdMsec = 0;
  const zx_duration_t lockup_duration = ZX_MSEC(gCmdline.GetUInt64(
      "kernel.lockup-detector.critical-section-threshold-ms", kDefaultThresholdMsec));
  const zx_ticks_t ticks = DurationToTicks(lockup_duration);
  cs_threshold_ticks.store(ticks);

  if constexpr (!DEBUG_ASSERT_IMPLEMENTED) {
    dprintf(INFO, "lockup_detector: critical section detection disabled\n");
  } else if (ticks > 0) {
    dprintf(INFO,
            "lockup_detector: critical section threshold is %" PRId64 " ticks (%" PRId64 " ns)\n",
            ticks, lockup_duration);
  } else {
    dprintf(INFO, "lockup_detector: critical section detection disabled by threshold\n");
  }

  constexpr uint64_t kDefaultHeartbeatPeriodMsec = 1000;
  heartbeat_period.store(ZX_MSEC(gCmdline.GetUInt64("kernel.lockup-detector.heartbeat-period-ms",
                                                    kDefaultHeartbeatPeriodMsec)));
  constexpr uint64_t kDefaultHeartbeatAgeThresholdMsec = 3000;
  heartbeat_threshold.store(ZX_MSEC(gCmdline.GetUInt64(
      "kernel.lockup-detector.heartbeat-age-threshold-ms", kDefaultHeartbeatAgeThresholdMsec)));
  const bool enabled = heartbeats_enabled();
  dprintf(INFO,
          "lockup_detector: heartbeats %s, period is %" PRId64 " ms, threshold is %" PRId64 " ms\n",
          enabled ? "enabled" : "disabled", heartbeat_period.load() / ZX_MSEC(1),
          heartbeat_threshold.load() / ZX_MSEC(1));
  if (enabled) {
    check_heartbeats_callback(&check_heartbeats_timer, current_time(), nullptr);
  }
}

void lockup_secondary_init() {
  DEBUG_ASSERT(arch_curr_cpu_num() != BOOT_CPU_ID);

  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;
  if (!heartbeats_enabled()) {
    state->heartbeat_active.store(false);
    return;
  }

  // To be save, make sure we have a recent last heartbeat before activating.
  const zx_time_t now = current_time();
  state->last_heartbeat.store(now);
  state->heartbeat_active.store(true);

  // Use a deadline with some jitter to avoid having all CPUs heartbeat at the same time.
  const Deadline deadline = DeadlineWithJitterAfter(heartbeat_period.load(), 10);
  state->heartbeat_timer.Set(deadline, heartbeat_callback, state);
}

void lockup_secondary_shutdown() {
  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;
  state->heartbeat_active.store(false);
  state->heartbeat_timer.Cancel();
}

zx_ticks_t lockup_get_cs_threshold_ticks() { return cs_threshold_ticks.load(); }

void lockup_set_cs_threshold_ticks(zx_ticks_t ticks) { cs_threshold_ticks.store(ticks); }

void lockup_begin() {
  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;

  // We must maintain the invariant that if a call to `lockup_begin()` increments the depth, the
  // matching call to `lockup_end()` decrements it.  The most reliable way to accomplish that is to
  // always increment and always decrement.
  state->critical_section_depth++;
  if (state->critical_section_depth != 1) {
    // We must be in a nested critical section.  Do nothing so that only the outermost critical
    // section is measured.
    return;
  }

  if (cs_threshold_ticks.load() == 0) {
    // Lockup detector is disabled.
    return;
  }

  const zx_ticks_t now = current_ticks();
  state->begin_ticks.store(now, ktl::memory_order_relaxed);
}

void lockup_end() {
  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;

  // Defer decrementing until the very end of this function to protect against reentrancy hazards
  // from the calls to `KERNEL_OOPS` and `Thread::Current::PrintBacktrace`.
  //
  // Every call to `lockup_end()` must decrement the depth because every call to `lockup_begin()` is
  // guaranteed to increment it.
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

  // Clear it.
  state->begin_ticks.store(0, ktl::memory_order_relaxed);

  // Do we have a threshold?
  if (cs_threshold_ticks.load() == 0) {
    // Nope.  Lockup detector is disabled.
    return;
  }

  // Was the threshold exceeded?
  const zx_ticks_t now = current_ticks();
  const zx_ticks_t ticks = (now - begin_ticks);
  if (ticks < cs_threshold_ticks.load()) {
    // Nope.
    return;
  }

  // Threshold exceeded.
  const zx_duration_t duration = TicksToDuration(ticks);
  const cpu_num_t cpu = arch_curr_cpu_num();
  RecordCriticalSectionCounters(duration);
  KERNEL_OOPS("lockup_detector: CPU-%u in critical section for %" PRId64 " ms, start=%" PRId64
              " now=%" PRId64 "\n",
              cpu, duration / ZX_MSEC(1), begin_ticks, now);
  Thread::Current::PrintBacktrace();
}

namespace {

void lockup_status() {
  const zx_ticks_t ticks = cs_threshold_ticks.load();
  printf("critical section threshold is %" PRId64 " ticks (%" PRId64 " ns)\n", ticks,
         TicksToDuration(ticks));
  if (ticks != 0) {
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

  printf("heartbeat period is %" PRId64 " ms, heartbeat threshold is %" PRId64 " ms\n",
         heartbeat_period.load() / ZX_MSEC(1), heartbeat_threshold.load() / ZX_MSEC(1));
  percpu::ForEach([](cpu_num_t cpu, percpu* percpu_state) {
    if (!mp_is_cpu_online(cpu) | !mp_is_cpu_active(cpu)) {
      return;
    }
    LockupDetectorState* state = &percpu_state->lockup_detector_state;
    if (!state->heartbeat_active.load()) {
      printf("CPU-%u heartbeats disabled\n", cpu);
      return;
    }
    const zx_time_t last_heartbeat = state->last_heartbeat.load();
    const zx_duration_t age = zx_time_sub_time(current_time(), last_heartbeat);
    const zx_duration_t max_gap = state->max_heartbeat_gap.load();
    printf("CPU-%u last heartbeat at %" PRId64 " ms, age is %" PRId64 " ms, max gap is %" PRId64
           " ms\n",
           cpu, last_heartbeat / ZX_MSEC(1), age / ZX_MSEC(1), max_gap / ZX_MSEC(1));
  });
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
  Thread* t = Thread::Create("lockup-spin", spin, &duration, DEFAULT_PRIORITY);
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
