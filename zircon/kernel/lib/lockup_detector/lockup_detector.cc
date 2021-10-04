// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/lockup_detector.h"

#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <lib/crashlog.h>
#include <lib/lockup_detector/inline_impl.h>
#include <lib/lockup_detector/state.h>
#include <lib/relaxed_atomic.h>
#include <lib/version.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <zircon/time.h>

#include <dev/hw_watchdog.h>
#include <fbl/algorithm.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#if defined(__aarch64__)
#include <arch/arm64/dap.h>
#endif

// Counter for the number of lockups detected.
KCOUNTER(counter_lockup_cs_count, "lockup_detector.critical_section.count")

// Counters for number of lockups exceeding a given duration.
KCOUNTER(counter_lockup_cs_exceeding_10ms, "lockup_detector.critical_section.exceeding_ms.10")
KCOUNTER(counter_lockup_cs_exceeding_1000ms, "lockup_detector.critical_section.exceeding_ms.1000")
KCOUNTER(counter_lockup_cs_exceeding_100000ms,
         "lockup_detector.critical_section.exceeding_ms.100000")

// Counts the number of times the lockup detector has emitted a "no heartbeat" oops.
KCOUNTER(counter_lockup_no_heartbeat_oops, "lockup_detector.no_heartbeat_oops")

LockupDetectorState gLockupDetectorPerCpuState[SMP_MAX_CPUS];

namespace {

enum class FailureSeverity { Oops, Fatal };

inline zx_duration_t TicksToDuration(zx_ticks_t ticks) {
  return platform_get_ticks_to_time_ratio().Scale(ticks);
}

inline zx_ticks_t DurationToTicks(zx_duration_t duration) {
  return platform_get_ticks_to_time_ratio().Inverse().Scale(duration);
}

#if defined(__aarch64__)
void DumpRegistersAndBacktrace(cpu_num_t cpu, FILE* output_target) {
  arm64_dap_processor_state state;
  zx_status_t result = arm64_dap_read_processor_state(cpu, &state);

  if (result != ZX_OK) {
    fprintf(output_target, "Failed to read DAP state (res %d)\n", result);
    return;
  }

  fprintf(output_target, "DAP state:\n");
  state.Dump(output_target);
  fprintf(output_target, "\n");

  if constexpr (__has_feature(shadow_call_stack)) {
    constexpr const char* bt_fmt = "{{{bt:%u:%p}}}\n";
    uint32_t n = 0;

    // Don't attempt to do any backtracking unless this looks like the thread is
    // in the kernel right now.  The PC might be completely bogus, but even if
    // it is in a legit user mode process, I'm not sure of a good way to print
    // the symbolizer context for that process, or to figure out if the process
    // is using a shadow call stack or not.
    if (state.get_el_level() != 1u) {
      fprintf(output_target, "Skipping backtrace, CPU-%u EL is %u, not 1\n", cpu,
              state.get_el_level());
      return;
    }

    // Print the symbolizer context, and then the PC as frame 0's address, and
    // the LR as frame 1's address.
    PrintSymbolizerContext(output_target);
    fprintf(output_target, bt_fmt, n++, reinterpret_cast<void*>(state.pc));
    fprintf(output_target, bt_fmt, n++, reinterpret_cast<void*>(state.r[30]));

    constexpr size_t PtrSize = sizeof(void*);
    uintptr_t ret_addr_ptr = state.r[18];
    if (ret_addr_ptr & (PtrSize - 1)) {
      fprintf(output_target, "Halting backtrace, x18 (0x%" PRIu64 ") is not %zu byte aligned.\n",
              ret_addr_ptr, PtrSize);
      return;
    }

    constexpr uint32_t MAX_BACKTRACE = 32;
    for (; n < MAX_BACKTRACE; ++n) {
      // Attempt to back up one level.  Never cross a page boundary when we do this.
      static_assert(fbl::is_pow2(static_cast<uint64_t>(PAGE_SIZE)),
                    "PAGE_SIZE is not a power of 2!  Wut??");
      if ((ret_addr_ptr & (PAGE_SIZE - 1)) == 0) {
        break;
      }

      ret_addr_ptr -= PtrSize;

      // Print out this level
      fprintf(output_target, bt_fmt, n, reinterpret_cast<void**>(ret_addr_ptr)[0]);
    }
  }
}
#elif defined(__x86_64__)
void DumpRegistersAndBacktrace(cpu_num_t cpu, FILE* output_target) {
  fprintf(output_target, "Regs and Backtrace unavailable for CPU-%u on x64\n", cpu);
}
#else
#error "Unknown architecture! Neither __aarch64__ nor __x86_64__ are defined"
#endif

void DumpCommonDiagnostics(cpu_num_t cpu, FILE* output_target, FailureSeverity severity) {
  DEBUG_ASSERT(arch_ints_disabled());

  auto& percpu = percpu::Get(cpu);
  fprintf(output_target, "timer_ints: %lu, interrupts: %lu\n", percpu.stats.timer_ints,
          percpu.stats.interrupts);

  if (ThreadLock::Get()->lock().HolderCpu() == cpu) {
    fprintf(output_target,
            "thread lock is held by cpu %u, skipping thread and scheduler diagnostics\n", cpu);
    return;
  }

  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};
  percpu.scheduler.Dump(output_target);
  Thread* thread = percpu.scheduler.active_thread();
  if (thread != nullptr) {
    fprintf(output_target, "thread: pid=%lu tid=%lu\n", thread->pid(), thread->tid());
    ThreadDispatcher* user_thread = thread->user_thread();
    if (user_thread != nullptr) {
      ProcessDispatcher* process = user_thread->process();
      char name[ZX_MAX_NAME_LEN]{};
      process->get_name(name);
      fprintf(output_target, "process: name=%s\n", name);
    }
  }

  if (severity == FailureSeverity::Fatal) {
    fprintf(output_target, "\n");
    DumpRegistersAndBacktrace(cpu, output_target);
  }
}

class TA_CAP("mutex") FatalConditionReporterRole {
 public:
  FatalConditionReporterRole() = default;
  FatalConditionReporterRole(const FatalConditionReporterRole&) = delete;
  FatalConditionReporterRole(FatalConditionReporterRole&&) = delete;
  FatalConditionReporterRole& operator=(const FatalConditionReporterRole&) = delete;
  FatalConditionReporterRole& operator=(FatalConditionReporterRole&&) = delete;

  bool Acquire() TA_TRY_ACQ(true) {
    // A fatal condition has been observed and we are on the road to rebooting.
    // Attempt to pet the watchdog one last time, then suppress all future pets.
    // If anything goes wrong from here on out which prevents us from reporting
    // the fatal condition, we want the HW WDT (if present) to reboot us.
    hw_watchdog_pet();
    hw_watchdog_suppress_petting(true);

    // Now that the WDT is armed, attempt to assume the role of the fatal
    // condition reporter.  If we fail, then someone else is already in the
    // process of reporting the fatal condition.  We will just leave them to
    // their task.  If they hang while attempting to write a crashlog and reboot
    // the system, the HW WDT will end up rebooting the system for them (if
    // present).
    const cpu_num_t current_cpu = arch_curr_cpu_num();
    cpu_num_t expected = INVALID_CPU;
    return reporter_id_.compare_exchange_strong(expected, current_cpu);
  }

  // No one should ever actually release the role of the fatal condition
  // reporter.  Even so, we need to have a call to "release" in place in order
  // to make the static thread analysis happy.  If we ever _do_ actually make it
  // to this function, it means that the CPU which was assigned the role of
  // fatal condition reporter failed to reboot the system for some bizarre
  // reason.  Do our best to panic the system in this case.
  void Release() TA_REL() { PANIC("Fatal condition reporter failed to reboot!"); }

 private:
  ktl::atomic<cpu_num_t> reporter_id_{INVALID_CPU};
};

FatalConditionReporterRole g_fatal_condition_reporter_role;

class HeartbeatLockupChecker {
 public:
  static zx_duration_t period() { return period_; }
  static zx_duration_t threshold() { return threshold_; }
  static zx_duration_t fatal_threshold() { return fatal_threshold_; }

  static void InitStaticParams() {
    period_ = ZX_MSEC(gBootOptions->lockup_detector_heartbeat_period_ms);
    threshold_ = ZX_MSEC(gBootOptions->lockup_detector_heartbeat_age_threshold_ms);
    fatal_threshold_ = ZX_MSEC(gBootOptions->lockup_detector_age_fatal_threshold_ms);
  }

  // TODO(johngro): once state->current_checker_id becomes a more formal
  // spinlock, come back here and TA_REQ(state.current_checker_id)
  static void PerformCheck(LockupDetectorState& state, cpu_num_t cpu, zx_time_t now_mono);

 private:
  HeartbeatLockupChecker() = default;

  // Note that the following static parameters are non-atomic because they are
  // currently setup only once by the primary CPU before any other CPUs have
  // started, and because they cannot change after setup. If we ever turn these
  // into dynamic properties which can be adjusted while the system is running,
  // we need to come back here and make them atomics with proper memory order
  // semantics.
  //
  // The period at which CPUs emit heartbeats.  0 means heartbeats are disabled.
  static inline zx_duration_t period_{0};

  // If a CPU's most recent heartbeat is older than this threshold, it is
  // considered to be locked up and a KERNEL OOPS will be triggered.
  static inline zx_duration_t threshold_{0};

  // If a CPU's most recent heartbeat is older than this threshold, it is
  // considered to be locked up and crashlog will be generated followed by a
  // reboot.
  static inline zx_duration_t fatal_threshold_{0};
};

class CriticalSectionLockupChecker {
 public:
  static zx_ticks_t threshold_ticks() { return threshold_ticks_.load(); }
  static void set_threshold_ticks(zx_ticks_t val) { threshold_ticks_.store(val); }
  static zx_ticks_t fatal_threshold_ticks() { return fatal_threshold_ticks_; }

  static bool IsEnabled() { return (threshold_ticks() > 0) || (fatal_threshold_ticks() > 0); }

  static void InitStaticParams() {
    const zx_duration_t threshold_duration =
        ZX_MSEC(gBootOptions->lockup_detector_critical_section_threshold_ms);
    threshold_ticks_.store(DurationToTicks(threshold_duration));

    const zx_duration_t fatal_threshold_duration =
        ZX_MSEC(gBootOptions->lockup_detector_critical_section_fatal_threshold_ms);
    fatal_threshold_ticks_ = DurationToTicks(fatal_threshold_duration);

    worst_case_threshold_ticks_ = DurationToTicks(counter_buckets_[0].exceeding);
  }

  static void RecordCriticalSectionBucketCounters(zx_ticks_t lockup_ticks) {
    // Fast abort if the time spent in the critical sections is less than the
    // minimum bucket threshold.
    if (lockup_ticks < worst_case_threshold_ticks_) {
      return;
    }

    zx_duration_t lockup_duration = TicksToDuration(lockup_ticks);
    for (auto iter = counter_buckets_.rbegin(); iter != counter_buckets_.rend(); iter++) {
      if (lockup_duration >= iter->exceeding) {
        kcounter_add(iter->counter, 1);
        break;
      }
    }
  }

  static void PerformCheck(LockupDetectorState& state, cpu_num_t cpu, zx_ticks_t now_ticks);

 private:
  CriticalSectionLockupChecker() = default;

  // Provides histogram-like kcounter functionality.
  struct CounterBucket {
    const zx_duration_t exceeding;
    const Counter& counter;
  };

  static constexpr const ktl::array counter_buckets_{
      CounterBucket{ZX_MSEC(10), counter_lockup_cs_exceeding_10ms},
      CounterBucket{ZX_MSEC(1000), counter_lockup_cs_exceeding_1000ms},
      CounterBucket{ZX_MSEC(100000), counter_lockup_cs_exceeding_100000ms},
  };

  // These thresholds control whether critical section checking is enabled and how long is "too
  // long".
  //
  // A non-zero |threshold_ticks| enables critical section checking with a non-fatal action (log,
  // but don't reboot).
  //
  // A non-zero |fatal_threshold_ticks| enables critical section checking with a fatal action
  // (reboot).
  //
  // These values are expressed in units of ticks rather than nanoseconds because it is faster to
  // read the platform timer's tick count than to get current_time().
  //
  // These variables are atomic because, although set early during lockup detector initialization,
  // their values may change to facilitate testing (see |lockup_set_cs_threshold_ticks|).  Use
  // relaxed operations because these fields are accessed within the critical section and must be
  // fast.
  static inline RelaxedAtomic<zx_ticks_t> threshold_ticks_{0};
  static inline RelaxedAtomic<zx_ticks_t> fatal_threshold_ticks_{0};

  static inline zx_ticks_t worst_case_threshold_ticks_{ktl::numeric_limits<zx_ticks_t>::max()};
};

void HeartbeatLockupChecker::PerformCheck(LockupDetectorState& state, cpu_num_t cpu,
                                          zx_time_t now_mono) {
  // If the heartbeat mechanism is currently not active for this CPU, just skip
  // all of the checks.
  auto& hb_state = state.heartbeat;
  if (!hb_state.active.load()) {
    return;
  }

  // Observe each of the details we need to know to make a determination of
  // whether or not we should report a failure.
  const zx_time_t observed_last_heartbeat = hb_state.last_heartbeat.load();
  zx_duration_t observed_age = zx_time_sub_time(now_mono, observed_last_heartbeat);
  const auto& cs_state = state.critical_section;
  // Note, we're loading name with relaxed semantics so there is nothing
  // ensuring that we see the "lastest value".  Idealy we'd use
  // memory_order_acquire when reading name and memory_order_release when
  // writing.  However, doing so has a measusrable performance impact and it's
  // crucial to minimize lockup_detector overhead.  We tolerate stale values
  // because we're only using name to help us find the point where the lockup
  // occurred.
  const char* const observed_name = cs_state.name.load(ktl::memory_order_relaxed);

  // If this is the worst gap we have ever seen, record that fact now.
  if (observed_age > hb_state.max_gap.load()) {
    hb_state.max_gap.store(observed_age);
  }

  // A shared lambda used to report errors in a consistent fashion, either to
  // just stdout, or to the stdout_panic buffer in the case that this is a fatal
  // condition.
  auto ReportFailure = [&](FailureSeverity severity) {
    FILE* output_target = (severity == FailureSeverity::Fatal) ? &stdout_panic_buffer : stdout;

    // Print an OOPS header so that we properly trigger tefmo checks, but only
    // send it to stdout.  If this a fatal failure, we don't want to waste any
    // bytes saying "OOPS" in the crashlog.  It should be pretty clear from the
    // fact that we are filing a crashlog that things went pretty seriously
    // wrong.
    KERNEL_OOPS("");
    fprintf(output_target,
            "lockup_detector: no heartbeat from CPU-%u in %" PRId64 " ms, last_heartbeat=%" PRId64
            " observed now=%" PRId64 " name=%s.\nReported by [CPU-%u] (message rate limited)\n",
            cpu, observed_age / ZX_MSEC(1), observed_last_heartbeat, now_mono,
            (observed_name ? observed_name : "unknown"), arch_curr_cpu_num());
    DumpCommonDiagnostics(cpu, output_target, severity);
  };

  // If we have a fatal threshold configured, and we have exceeded that
  // threshold, then it is time to file a crashlog and reboot the system.
  if ((fatal_threshold() > 0) && (observed_age > fatal_threshold())) {
    if (g_fatal_condition_reporter_role.Acquire()) {
      platform_panic_start();
      ReportFailure(FailureSeverity::Fatal);
      platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::SoftwareWatchdog);
      g_fatal_condition_reporter_role.Release();
    }
  }

  if ((threshold() > 0) && (observed_age > threshold()) && hb_state.alert_limiter.Ready()) {
    kcounter_add(counter_lockup_no_heartbeat_oops, 1);
    ReportFailure(FailureSeverity::Oops);
  }
}

void CriticalSectionLockupChecker::PerformCheck(LockupDetectorState& state, cpu_num_t cpu,
                                                zx_ticks_t now_ticks) {
  auto& cs_state = state.critical_section;

  // Observe all of the info we need to make a decision as to whether or not there
  // has been a condition violation.
  const zx_ticks_t observed_threshold_ticks = threshold_ticks();
  // Use Acquire semantics to ensure that if we observe a previously stored |begin_ticks| value we
  // will also observe stores to other fields that were issued prior to a Release on |begin_ticks|.
  const zx_ticks_t observed_begin_ticks = cs_state.begin_ticks.load(ktl::memory_order_acquire);
  const char* const observed_name = cs_state.name.load(ktl::memory_order_relaxed);
  const zx_ticks_t observed_worst_case_ticks =
      cs_state.worst_case_ticks.load(ktl::memory_order_relaxed);

  // If observed_begin_ticks is non-zero, then the CPU we are checking is currently in a
  // critical section.  Compute how long it has been in the CS and check to see
  // if it exceeds any of our configured thresholds.
  if (observed_begin_ticks > 0) {
    const zx_ticks_t age_ticks = zx_time_sub_time(now_ticks, observed_begin_ticks);

    // A shared lambda used to report errors in a consistent fashion, either to
    // just stdout, or to the stdout_panic buffer in the case that this is a fatal
    // condition.
    auto ReportFailure = [&](FailureSeverity severity) {
      FILE* output_target = (severity == FailureSeverity::Fatal) ? &stdout_panic_buffer : stdout;

      // See the comment in HeartbeatLockupChecker::PerformCheck for an explanation of why this
      // curious empty-string OOPS is here.
      KERNEL_OOPS("");
      fprintf(output_target,
              "lockup_detector: CPU-%u in critical section for %" PRId64 " ms, threshold=%" PRId64
              " ms start=%" PRId64 " now=%" PRId64
              " name=%s.\n"
              "Reported by [CPU-%u] (message rate limited)\n",
              cpu, TicksToDuration(age_ticks) / ZX_MSEC(1),
              TicksToDuration(observed_threshold_ticks) / ZX_MSEC(1),
              TicksToDuration(observed_begin_ticks), TicksToDuration(now_ticks),
              (observed_name ? observed_name : "unknown"), arch_curr_cpu_num());

      DumpCommonDiagnostics(cpu, output_target, severity);
    };

    // Check the fatal condition first.
    if ((fatal_threshold_ticks() > 0) && (age_ticks >= fatal_threshold_ticks())) {
      if (g_fatal_condition_reporter_role.Acquire()) {
        platform_panic_start();
        ReportFailure(FailureSeverity::Fatal);
        platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::SoftwareWatchdog);
        g_fatal_condition_reporter_role.Release();
      }
    }

    // Next, check to see if our "oops" threshold was exceeded.
    if ((observed_threshold_ticks > 0) && (age_ticks >= observed_threshold_ticks)) {
      // Threshold exceeded.  Record this in the kcounters if this is the first time
      // we have seen this event, and then decide whether or not to print out an
      // oops based on our rate limiter.
      if (cs_state.last_counted_begin_ticks != observed_begin_ticks) {
        kcounter_add(counter_lockup_cs_count, 1);
        cs_state.last_counted_begin_ticks = observed_begin_ticks;
      }

      if (cs_state.ongoing_call_alert_limiter.Ready()) {
        ReportFailure(FailureSeverity::Oops);
      }
    }
  }

  // Next check to see if we have a new worst case time spent in a critical
  // section to report.
  if ((observed_worst_case_ticks > worst_case_threshold_ticks_) &&
      (observed_worst_case_ticks > cs_state.reported_worst_case_ticks) &&
      cs_state.worst_case_alert_limiter.Ready()) {
    // Remember the last worst case we reported, so we don't report it multiple
    // times.
    cs_state.reported_worst_case_ticks = observed_worst_case_ticks;

    // Now go ahead and report the new worst case.
    const zx_duration_t duration = TicksToDuration(observed_worst_case_ticks);
    printf(
        "lockup_detector: CPU-%u encountered a new worst case critical section section time of "
        "%" PRId64 " usec. Reported by [CPU-%u] (message rate limited)\n",
        cpu, duration / ZX_USEC(1), arch_curr_cpu_num());
  }
}

// Return an absolute deadline |duration| nanoseconds from now with a jitter of +/- |percent|%.
Deadline DeadlineWithJitterAfter(zx_duration_t duration, uint32_t percent) {
  DEBUG_ASSERT(percent <= 100);
  const zx_duration_t delta = affine::Ratio{(rand() / 100) * percent, RAND_MAX}.Scale(duration);
  return Deadline::after(zx_duration_add_duration(duration, delta));
}

// Record that the current CPU is still alive by having it update its last
// heartbeat.  Then, check all of current CPU's peers to see if they have
// tripped any of our low level lockup detectors. This currently consists of:
//
// 1) The heartbeat detector (verifies that CPU timers are working)
// 2) The critical section detector (verifies that no CPU spends too long in a
//    critical section of code, such as an SMC call).
//
void DoHeartbeatAndCheckPeerCpus(Timer* timer, zx_time_t now_mono, void* arg) {
  const zx_ticks_t now_ticks = current_ticks();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  // Record that we are still alive.
  auto& checker_state = *(reinterpret_cast<LockupDetectorState*>(arg));
  checker_state.heartbeat.last_heartbeat.store(now_mono);

  // Pet the HW WDT, but only if we have a fatal heartbeat threshold
  // configured. We don't want the heartbeat checkers to be petting the dog if
  // they don't plan to reboot the system if things start to get really bad.
  if (HeartbeatLockupChecker::fatal_threshold() > 0) {
    hw_watchdog_pet();
  }

  // Now, check each of the lockup conditions for each of our peers.
  for (cpu_num_t cpu = 0; cpu < percpu::processor_count(); ++cpu) {
    if (cpu == current_cpu || !mp_is_cpu_online(cpu) || !mp_is_cpu_active(cpu)) {
      continue;
    }
    LockupDetectorState& state = gLockupDetectorPerCpuState[cpu];

    // Attempt to claim the role of the "checker" for this CPU.  If we fail to
    // do so, then another CPU is checking this CPU already, so we will just
    // skip our checks this time.  Note that this leaves a small gap in
    // detection ability.
    //
    // If the other checker has discovered no trouble and is just about to drop
    // the role of checker, but time has progressed to the point where a failure
    // would now be detected.  In this case, we would have reported the problem
    // had we been able to assume the checker role, but since it had not been
    // released yet, we will miss it.
    //
    // This gap is an acknowledged limitation.  Never stalling in these threads
    // is a more important property to maintain then having perfect gap free
    // coverage.  Presumably, some other core will check again in a short while
    // (or, we will do so ourselves next time around).
    //
    // TODO(johngro): either just replace this with a spin-try-lock, or spend
    // some time reviewing the memory order here.  CST seems like overkill, but
    // then again, checks are currently only performed once per second, so I
    // would rather be correct than fast for the time being.
    cpu_num_t expected = INVALID_CPU;
    if (state.current_checker_id.compare_exchange_strong(expected, current_cpu)) {
      // Now that we are the assigned "checker", perform the checks.  Start with
      // the CriticalSection check.  If there is a fatal condition to be
      // reported, we would rather start with the CriticalSection fatal
      // condition as it will can provide more specific details about the lockup
      // than the heartbeat checker can.
      CriticalSectionLockupChecker::PerformCheck(state, cpu, now_ticks);
      HeartbeatLockupChecker::PerformCheck(state, cpu, now_mono);

      // Next, release our role as checker for this CPU.
      state.current_checker_id.store(INVALID_CPU);
    }
  }

  // If heartbeats are still enabled for this core, schedule the next check.
  if (checker_state.heartbeat.active.load()) {
    timer->Set(Deadline::after(HeartbeatLockupChecker::period()), DoHeartbeatAndCheckPeerCpus, arg);
  }
}

// Stop the process of having the current CPU recording heartbeats and checking
// in on other CPUs.
void stop_heartbeats() {
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  state.heartbeat.active.store(false);
  percpu::GetCurrent().lockup_detector_timer.Cancel();
}

// Start the process of recording heartbeats and checking in on other CPUs on
// the current CPU.
void start_heartbeats() {
  if (HeartbeatLockupChecker::period() <= 0) {
    stop_heartbeats();
    return;
  }

  // To be safe, make sure we have a recent last heartbeat before activating.
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  auto& hb_state = state.heartbeat;
  const zx_time_t now = current_time();
  hb_state.last_heartbeat.store(now);
  hb_state.active.store(true);

  // Use a deadline with some jitter to avoid having all CPUs heartbeat at the same time.
  const Deadline deadline = DeadlineWithJitterAfter(HeartbeatLockupChecker::period(), 10);
  percpu::GetCurrent().lockup_detector_timer.Set(deadline, DoHeartbeatAndCheckPeerCpus, &state);
}

}  // namespace

void lockup_primary_init() {
  // Initialize parameters for the heartbeat checks.
  HeartbeatLockupChecker::InitStaticParams();

  dprintf(INFO,
          "lockup_detector: heartbeats %s, period is %" PRId64 " ms, threshold is %" PRId64
          " ms, fatal threshold is %" PRId64 " ms\n",
          (HeartbeatLockupChecker::period() > 0) ? "enabled" : "disabled",
          HeartbeatLockupChecker::period() / ZX_MSEC(1),
          HeartbeatLockupChecker::threshold() / ZX_MSEC(1),
          HeartbeatLockupChecker::fatal_threshold() / ZX_MSEC(1));

  // Initialize parameters for the critical section checks, but only if the
  // heartbeat mechanism is enabled.  If the heartbeat mechanism is disabled, no
  // checks will ever be performed.
  //
  // TODO(johngro): relax this.  There is no strong reason to not do our
  // periodic checking if any of the check conditions are enabled.
  if constexpr (LOCKUP_CRITICAL_SECTION_ENALBED) {
    if (HeartbeatLockupChecker::period() > 0) {
      CriticalSectionLockupChecker::InitStaticParams();

      if (CriticalSectionLockupChecker::IsEnabled()) {
        dprintf(
            INFO,
            "lockup_detector: critical section threshold is %" PRId64
            " ms, fatal threshold is %" PRId64 " ms\n",
            TicksToDuration(CriticalSectionLockupChecker::threshold_ticks()) / ZX_MSEC(1),
            TicksToDuration(CriticalSectionLockupChecker::fatal_threshold_ticks()) / ZX_MSEC(1));
      } else {
        dprintf(INFO, "lockup_detector: critical section detection disabled by threshold\n");
      }
    } else {
      dprintf(
          INFO,
          "lockup_detector: critical section detection disabled because heartbeats are disabled\n");
    }
  } else {
    dprintf(INFO, "lockup_detector: critical section detection disabled by build\n");
  }

  // Kick off heartbeats on this CPU, if they are enabled.
  start_heartbeats();
}

void lockup_secondary_init() { start_heartbeats(); }
void lockup_secondary_shutdown() { stop_heartbeats(); }

// TODO(johngro): Make the definition of the various checkers available (perhaps
// in a "lockup_detector" namespace) so that things like tests outside of this
// translational unit can directly query stuff like this, instead of needing to
// bound through a functions like this.
zx_ticks_t lockup_get_cs_threshold_ticks() {
  return CriticalSectionLockupChecker::threshold_ticks();
}
void lockup_set_cs_threshold_ticks(zx_ticks_t val) {
  CriticalSectionLockupChecker::set_threshold_ticks(val);
}

void lockup_timed_begin(const char* name) {
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  const bool outermost = lockup_internal::Enter(state, name);
  if (likely(outermost)) {
    auto& cs_state = state.critical_section;
    // We're using memory_order_relaxed instead of memory_order_release to
    // minimize performance impact.  As a result, HeartbeatLockupChecker may see
    // stale name values because there is nothing for it to synchronize-with.
    // However, if CriticalSectionLockupChecker is enabled, then the begin_ticks
    // store with release semantics will ensure the CriticalSectionLockupChecker
    // sees the latest value.
    cs_state.name.store(name, ktl::memory_order_relaxed);
    if (CriticalSectionLockupChecker::IsEnabled()) {
      const zx_ticks_t now = current_ticks();
      // Use release semantics to ensure that if an observer sees this store to |begin_ticks|,
      // they will also see the stores that preceded it.
      cs_state.begin_ticks.store(now, ktl::memory_order_release);
    }
  }
}

void lockup_timed_end() {
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  lockup_internal::CallIfOuterAndLeave(state, [](LockupDetectorState& state) {
    // Is this a new worst for us?
    const zx_ticks_t now_ticks = current_ticks();
    auto& cs_state = state.critical_section;
    const zx_ticks_t begin = cs_state.begin_ticks.load(ktl::memory_order_relaxed);
    zx_ticks_t delta = zx_time_sub_time(now_ticks, begin);

    // Update our counters.
    CriticalSectionLockupChecker::RecordCriticalSectionBucketCounters(delta);

    if (delta > cs_state.worst_case_ticks.load(ktl::memory_order_relaxed)) {
      cs_state.worst_case_ticks.store(delta, ktl::memory_order_relaxed);
    }

    // See comment in lockup_timed_begin at the point where name is stored.
    cs_state.name.store(nullptr, ktl::memory_order_relaxed);

    // We are done with the CS now.  Clear the begin time to indicate that we are not in any
    // critical section.
    //
    // Use release semantics to ensure that if an observer sees this store to |begin_ticks|, they
    // will also see any of our previous stores.
    cs_state.begin_ticks.store(0, ktl::memory_order_release);
  });
}

int64_t lockup_get_critical_section_oops_count() { return counter_lockup_cs_count.Value(); }

int64_t lockup_get_no_heartbeat_oops_count() { return counter_lockup_no_heartbeat_oops.Value(); }

namespace {

void lockup_status() {
  const zx_ticks_t ticks = CriticalSectionLockupChecker::threshold_ticks();
  printf("critical section threshold is %" PRId64 " ticks (%" PRId64 " ms)\n", ticks,
         TicksToDuration(ticks) / ZX_MSEC(1));
  if (ticks != 0) {
    for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
      if (!mp_is_cpu_active(i)) {
        printf("CPU-%u is not active, skipping\n", i);
        continue;
      }

      const auto& cs_state = gLockupDetectorPerCpuState[i].critical_section;
      const zx_ticks_t begin_ticks = cs_state.begin_ticks.load(ktl::memory_order_acquire);
      const char* name = cs_state.name.load(ktl::memory_order_relaxed);
      const zx_ticks_t now = current_ticks();
      const int64_t worst_case_usec =
          TicksToDuration(cs_state.worst_case_ticks.load(ktl::memory_order_relaxed)) / ZX_USEC(1);
      if (begin_ticks == 0) {
        printf("CPU-%u not in critical section (worst case %" PRId64 " uSec)\n", i,
               worst_case_usec);
      } else {
        zx_duration_t duration = TicksToDuration(now - begin_ticks);
        printf("CPU-%u in critical section (%s) for %" PRId64 " ms (worst case %" PRId64 " uSec)\n",
               i, (name != nullptr ? name : "unknown"), duration / ZX_MSEC(1), worst_case_usec);
      }
    }
  }

  printf("heartbeat period is %" PRId64 " ms, heartbeat threshold is %" PRId64 " ms\n",
         HeartbeatLockupChecker::period() / ZX_MSEC(1),
         HeartbeatLockupChecker::threshold() / ZX_MSEC(1));

  for (cpu_num_t cpu = 0; cpu < percpu::processor_count(); ++cpu) {
    if (!mp_is_cpu_online(cpu) || !mp_is_cpu_active(cpu)) {
      continue;
    }

    const auto& hb_state = gLockupDetectorPerCpuState[cpu].heartbeat;
    if (!hb_state.active.load()) {
      printf("CPU-%u heartbeats disabled\n", cpu);
      continue;
    }
    const zx_time_t last_heartbeat = hb_state.last_heartbeat.load();
    const zx_duration_t age = zx_time_sub_time(current_time(), last_heartbeat);
    const zx_duration_t max_gap = hb_state.max_gap.load();
    printf("CPU-%u last heartbeat at %" PRId64 " ms, age is %" PRId64 " ms, max gap is %" PRId64
           " ms\n",
           cpu, last_heartbeat / ZX_MSEC(1), age / ZX_MSEC(1), max_gap / ZX_MSEC(1));
  }
}

// Runs |func| on |cpu|, passing |duration| as an argument.
void run_lockup_func(cpu_num_t cpu, zx_duration_t duration, thread_start_routine func) {
  Thread* t = Thread::Create("lockup-test", func, &duration, DEFAULT_PRIORITY);
  t->SetCpuAffinity(cpu_num_to_mask(cpu));
  t->Resume();
  t->Join(nullptr, ZX_TIME_INFINITE);
}

// Trigger a temporary lockup of |cpu| by holding a spinlock for |duration|.
void lockup_trigger_spinlock(cpu_num_t cpu, zx_duration_t duration) {
  run_lockup_func(cpu, duration, [](void* arg) -> int {
    const zx_duration_t duration = *reinterpret_cast<zx_duration_t*>(arg);
    // Acquire a spinlock and hold it for |duration|.
    DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(lockup_test_lock, MonitoredSpinLock);
    Guard<MonitoredSpinLock, IrqSave> guard{lockup_test_lock::Get(), SOURCE_TAG};
    const zx_time_t deadline = zx_time_add_duration(current_time(), duration);
    while (current_time() < deadline) {
      arch::Yield();
    }
    return 0;
  });
}

// Trigger a temporary lockup of |cpu| by remaining in a critical section for |duration|.
void lockup_trigger_critical_section(cpu_num_t cpu, zx_duration_t duration) {
  run_lockup_func(cpu, duration, [](void* arg) -> int {
    const zx_duration_t duration = *reinterpret_cast<zx_duration_t*>(arg);
    AutoPreemptDisabler preempt_disable;
    LOCKUP_TIMED_BEGIN("trigger-tool");
    const zx_time_t deadline = zx_time_add_duration(current_time(), duration);
    while (current_time() < deadline) {
      arch::Yield();
    }
    LOCKUP_TIMED_END();
    return 0;
  });
}

int cmd_lockup(int argc, const cmd_args* argv, uint32_t flags) {
  auto usage = [cmd_name = argv[0].str]() -> int {
    printf("usage:\n");
    printf("%s status                                 : print lockup detector status\n", cmd_name);
    printf("%s test_spinlock <cpu> <num msec>         : hold spinlock on <cpu> for <num msec>\n",
           cmd_name);
    printf(
        "%s test_critical_section <cpu> <num msec> : hold critical section on <cpu> for <num "
        "msec>\n",
        cmd_name);
    return ZX_ERR_INTERNAL;
  };

  if (argc < 2) {
    printf("not enough arguments\n");
    return usage();
  }

  if (!strcmp(argv[1].str, "status")) {
    lockup_status();
  } else if (!strcmp(argv[1].str, "test_spinlock")) {
    if (argc < 4) {
      return usage();
    }
    const auto cpu = static_cast<cpu_num_t>(argv[2].u);
    const auto ms = static_cast<uint32_t>(argv[3].u);
    printf("test_spinlock: locking up CPU %u for %u ms\n", cpu, ms);
    lockup_trigger_spinlock(cpu, ZX_MSEC(ms));
    printf("done\n");
  } else if (!strcmp(argv[1].str, "test_critical_section")) {
    if (argc < 4) {
      return usage();
    }
    const auto cpu = static_cast<cpu_num_t>(argv[2].u);
    const auto ms = static_cast<uint32_t>(argv[3].u);
    printf("test_critical_section: locking up CPU %u for %u ms\n", cpu, ms);
    lockup_trigger_critical_section(cpu, ZX_MSEC(ms));
    printf("done\n");
  } else {
    printf("unknown command\n");
    return usage();
  }

  return ZX_OK;
}

}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND("lockup", "lockup detector commands", &cmd_lockup)
STATIC_COMMAND_END(lockup)
