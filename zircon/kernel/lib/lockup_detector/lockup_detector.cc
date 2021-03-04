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
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

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

inline zx_duration_t TicksToDuration(zx_ticks_t ticks) {
  return platform_get_ticks_to_time_ratio().Scale(ticks);
}

inline zx_ticks_t DurationToTicks(zx_duration_t duration) {
  return platform_get_ticks_to_time_ratio().Inverse().Scale(duration);
}

void DumpSchedulerDiagnostics(cpu_num_t cpu) {
  DEBUG_ASSERT(arch_ints_disabled());

  auto& percpu = percpu::Get(cpu);
  printf("timer_ints: %lu\n", percpu.stats.timer_ints);

  if (ThreadLock::Get()->lock().HolderCpu() == cpu) {
    printf("thread lock is held by cpu %u, skipping thread and scheduler diagnostics\n", cpu);
    return;
  }

  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};
  percpu.scheduler.Dump();
  Thread* thread = percpu.scheduler.active_thread();
  if (thread != nullptr) {
    printf("thread: pid=%lu tid=%lu\n", thread->pid(), thread->tid());
    ThreadDispatcher* user_thread = thread->user_thread();
    if (user_thread != nullptr) {
      ProcessDispatcher* process = user_thread->process();
      char name[ZX_MAX_NAME_LEN]{};
      process->get_name(name);
      printf("process: name=%s\n", name);
    }
  }
}

class HeartbeatLockupChecker {
 public:
  HeartbeatLockupChecker() = default;
  HeartbeatLockupChecker(const HeartbeatLockupChecker&) = delete;
  HeartbeatLockupChecker(HeartbeatLockupChecker&&) = delete;
  HeartbeatLockupChecker& operator=(const HeartbeatLockupChecker&) = delete;
  HeartbeatLockupChecker& operator=(HeartbeatLockupChecker&&) = delete;

  static zx_duration_t period() { return period_.load(); }
  static zx_duration_t threshold() { return threshold_.load(); }
  static bool IsEnabled() { return (period() != 0) && (threshold() != 0); }

  static void InitStaticParams() {
    constexpr uint64_t kDefaultPeriodMsec = 1000;
    constexpr uint64_t kDefaultAgeThresholdMsec = 3000;

    period_.store(ZX_MSEC(
        gCmdline.GetUInt64(kernel_option::kLockupDetectorHeartbeatPeriodMs, kDefaultPeriodMsec)));
    threshold_.store(ZX_MSEC(gCmdline.GetUInt64(
        kernel_option::kLockupDetectorHeartbeatAgeThresholdMs, kDefaultAgeThresholdMsec)));
  }

  // Perform a check of the condition.  If a failure is detected, record the
  // details, and update the state structure to prevent the next checker from
  // duplicating our report.  Do not actually report the failure yet, we want to
  // drop the role of "checker" for the checked CPU before proceeding.
  //
  // TODO(johngro): once state->current_checker_id becomes a more formal
  // spinlock, come back here and TA_REQ(state.current_checker_id)
  void PerformCheck(LockupDetectorState& state, zx_ticks_t now_ticks, zx_time_t now_mono);

  // If a failure was detected during the check, report it now.
  //
  // TODO(johngro): once state->current_checker_id becomes a more formal
  // spinlock, come back here and TA_EXCL(state.current_checker_id)
  void ReportFailures(cpu_num_t cpu);

 private:
  // The period at which CPUs emit heartbeats.  0 means heartbeats are disabled.
  static inline ktl::atomic<zx_duration_t> period_{0};

  // If a CPU's most recent heartbeat is older than this threshold, it is considered to be locked up
  // and a KERNEL OOPS will be triggered.
  static inline ktl::atomic<zx_duration_t> threshold_{0};

  bool report_failure_{false};
  struct {
    zx_time_t last_heartbeat{0};
    zx_time_t age{0};
    zx_duration_t threshold{0};
  } observed_;
};

class CriticalSectionLockupChecker {
 public:
  CriticalSectionLockupChecker() = default;
  CriticalSectionLockupChecker(const CriticalSectionLockupChecker&) = delete;
  CriticalSectionLockupChecker(CriticalSectionLockupChecker&&) = delete;
  CriticalSectionLockupChecker& operator=(const CriticalSectionLockupChecker&) = delete;
  CriticalSectionLockupChecker& operator=(CriticalSectionLockupChecker&&) = delete;

  static zx_ticks_t threshold_ticks() { return threshold_ticks_.load(); }
  static void set_threshold_ticks(zx_ticks_t val) { threshold_ticks_.store(val); }
  static bool IsEnabled() { return threshold_ticks() > 0; }

  static void InitStaticParams() {
    constexpr uint64_t kDefaultThresholdMsec = 3000;
    const zx_duration_t threshold_duration = ZX_MSEC(gCmdline.GetUInt64(
        kernel_option::kLockupDetectorCriticalSectionThresholdMs, kDefaultThresholdMsec));
    threshold_ticks_.store(DurationToTicks(threshold_duration));
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

  // See comments in HeartbeatLockupChecker.
  void PerformCheck(LockupDetectorState& state, zx_ticks_t now_ticks, zx_time_t now_mono);
  void ReportFailures(cpu_num_t cpu);

 private:
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

  // Controls whether critical section checking is enabled and at how long is "too long".
  //
  // A value of 0 disables the lockup detector for critical sections.
  //
  // This value is expressed in units of ticks rather than nanoseconds because it is faster to read
  // the platform timer's tick count than to get current_time().
  static inline RelaxedAtomic<zx_ticks_t> threshold_ticks_{0};
  static inline zx_ticks_t worst_case_threshold_ticks_{ktl::numeric_limits<zx_ticks_t>::max()};

  bool report_failure_{false};
  struct {
    zx_ticks_t begin_ticks{0};
    zx_ticks_t threshold_ticks{0};
    zx_ticks_t age_ticks{0};
    zx_ticks_t new_worst_case_ticks{0};
  } observed_;
};

void HeartbeatLockupChecker::PerformCheck(LockupDetectorState& state, zx_ticks_t now_ticks,
                                          zx_time_t now_mono) {
  // If the heartbeat mechanism is currently not active for this CPU, just skip
  // all of the checks.
  auto& hb_state = state.heartbeat;
  if (!hb_state.active.load()) {
    return;
  }

  // Observe each of the details we need to know to make a determination of
  // whether or not we should report a failure.
  observed_.last_heartbeat = hb_state.last_heartbeat.load();
  observed_.threshold = threshold();
  observed_.age = zx_time_sub_time(now_mono, observed_.last_heartbeat);

  // If we didn't exceed the currently configured threshold, then this CPU is OK
  // and we are done.
  if (observed_.age < observed_.threshold) {
    return;
  }

  // If this is the worst gap we have ever seen, record that fact now.
  if (observed_.age > hb_state.max_gap.load()) {
    hb_state.max_gap.store(observed_.age);
  }

  // Report this issue once we drop the role of checker, if the rate limiter
  // will allow it.
  report_failure_ = hb_state.alert_limiter.Ready();
}

void HeartbeatLockupChecker::ReportFailures(cpu_num_t cpu) {
  // If no report was requested during the check phase, just get out now.
  if (!report_failure_) {
    return;
  }

  // Bump the kcounter, then print the oops containing the details which
  // triggered the report, then dump the current scheduler state.
  kcounter_add(counter_lockup_no_heartbeat_oops, 1);

  KERNEL_OOPS("lockup_detector: no heartbeat from CPU-%u in %" PRId64 " ms, last_heartbeat=%" PRId64
              " observed now=%" PRId64 ".  Reported by [CPU-%u] (message rate limited)\n",
              cpu, observed_.age / ZX_MSEC(1), observed_.last_heartbeat,
              zx_time_add_duration(observed_.last_heartbeat, observed_.age), arch_curr_cpu_num());

  DumpSchedulerDiagnostics(cpu);
}

void CriticalSectionLockupChecker::PerformCheck(LockupDetectorState& state, zx_ticks_t now_ticks,
                                                zx_time_t now_mono) {
  // Observe all of the info we need to make a decision as to whether or not there
  // has been a condition violation.
  auto& cs_state = state.critical_section;
  observed_.begin_ticks = cs_state.begin_ticks.load(ktl::memory_order_relaxed);
  observed_.threshold_ticks = threshold_ticks_.load();
  zx_ticks_t worst_case_ticks = cs_state.worst_case_ticks.load(ktl::memory_order_acquire);

  // If:
  // 1) The worst case is bad enough to make it into any of our counter buckets, and
  // 2) The worst case is worse than the previously reported worst case, and
  // 3) The rate limiter will allow us to create a new report
  //
  // Then stash the newly observed worst case in the observed state to be
  // reported later, and also update the worst reported value to prevent
  // multiple reports for the same value.
  if ((worst_case_ticks > worst_case_threshold_ticks_) &&
      (worst_case_ticks > cs_state.reported_worst_case_ticks) &&
      cs_state.worst_case_alert_limiter.Ready()) {
    observed_.new_worst_case_ticks = worst_case_ticks;
    cs_state.reported_worst_case_ticks = worst_case_ticks;
  }

  // If the threshold ticks value is non-positive, then checking is currently
  // disabled and we can get out now.  Likewise, if the CPU's begin_ticks value
  // is non-positive, then the CPU was not in a critical section at the time
  // that we checked, so the condition cannot have violated and we can get out
  // right now.
  if ((observed_.begin_ticks <= 0) || (observed_.threshold_ticks <= 0)) {
    return;
  }

  // The feature is enabled, and the CPU in question has been inside the
  // critical section for some amount of time already.  Was the threshold
  // exceeded?
  //
  // TODO(johngro): we really should have special, underflow aware, routines for
  // manipulating ticks as well as time.  Right now we simply cheat, since we
  // know under the hood that the types for ticks and time are the same, and the
  // time version of the underflow check will work just as well.
  observed_.age_ticks = zx_time_sub_time(now_ticks, observed_.begin_ticks);
  if (observed_.age_ticks < observed_.threshold_ticks) {
    return;
  }

  // Threshold exceeded.  Record this in the kcounters if this is the first time
  // we have seen this event, and then decide whether or not to print out an
  // oops based on our rate limiter.
  //
  // TODO(johngro): Consider cleaning this up.  We are going to clearly be
  // double reporting some of these faults as things stand.  Not only will each
  // of the other cores accumulate a similar situation when they take a look at
  // this CPU, there are periods of time (between the 1 second bin, and the 100
  // second bin) where even if there was only one CPU doing sanity checks, we
  // would end up recording a counter every time we observe that this core has
  // been in a CS for more than one second.
  //
  // It seems like we would rather only report the 1 second+ hang a single time
  // for a single instance of the hang.  I can come back here and clean this up
  // once I have a better idea of what this counter should be tracking,
  // however.
  if (cs_state.last_counted_begin_ticks != observed_.begin_ticks) {
    kcounter_add(counter_lockup_cs_count, 1);
    cs_state.last_counted_begin_ticks = observed_.begin_ticks;
  }

  // Report this issue once we drop the role of checker, if the rate limiter
  // will allow it.
  report_failure_ = cs_state.ongoing_call_alert_limiter.Ready();
}

void CriticalSectionLockupChecker::ReportFailures(cpu_num_t cpu) {
  // Did we observe a new worst case?  If so, report it and update the counters.
  if (observed_.new_worst_case_ticks > 0) {
    const zx_duration_t duration = TicksToDuration(observed_.new_worst_case_ticks);
    KERNEL_OOPS(
        "lockup_detector: CPU-%u encountered a new worst case critical section section time of "
        "%" PRId64 " usec. Reported by [CPU-%u] (message rate limited)\n",
        cpu, duration / ZX_USEC(1), arch_curr_cpu_num());
  }

  if (report_failure_) {
    KERNEL_OOPS("lockup_detector: CPU-%u in critical section for %" PRId64 " ms, threshold=%" PRId64
                " ms start=%" PRId64 " now=%" PRId64
                ". Reported by [CPU-%u] (message rate limited)\n",
                cpu, TicksToDuration(observed_.age_ticks) / ZX_MSEC(1),
                TicksToDuration(observed_.threshold_ticks) / ZX_MSEC(1),
                TicksToDuration(observed_.begin_ticks),
                TicksToDuration(zx_time_add_duration(observed_.begin_ticks, observed_.age_ticks)),
                arch_curr_cpu_num());

    DumpSchedulerDiagnostics(cpu);
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
// TODO(johngro): We probably want to pet the dog here as well.  In theory, the
// WDT should only fire if literally all of the cores have locked up and no core
// is in a position to check any other core.
//
void DoHeartbeatAndCheckPeerCpus(Timer* timer, zx_time_t now_mono, void* arg) {
  const zx_ticks_t now_ticks = current_ticks();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  // Record that we are still alive.  Here would be a good place to pet the WDT
  // as well.
  auto& checker_state = *(reinterpret_cast<LockupDetectorState*>(arg));
  checker_state.heartbeat.last_heartbeat.store(now_mono);

  // Now, check each of the lockup conditions for each of our peers.
  percpu::ForEach([current_cpu, now_ticks, now_mono](cpu_num_t cpu, percpu* percpu_state) {
    if (cpu == current_cpu || !mp_is_cpu_online(cpu) || !mp_is_cpu_active(cpu)) {
      return;
    }
    LockupDetectorState& state = percpu_state->lockup_detector_state;

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
    HeartbeatLockupChecker hb_checker;
    CriticalSectionLockupChecker cs_checker;
    cpu_num_t expected = INVALID_CPU;
    if (state.current_checker_id.compare_exchange_strong(expected, current_cpu)) {
      // Now that we are the assigned "checker", perform the checks.
      hb_checker.PerformCheck(state, now_ticks, now_mono);
      cs_checker.PerformCheck(state, now_ticks, now_mono);

      // Next, release our role as checker for this CPU.
      state.current_checker_id.store(INVALID_CPU);

      // Finally, now that we are out of the way, attempt to report any failure
      // we detected.
      hb_checker.ReportFailures(cpu);
      cs_checker.ReportFailures(cpu);
    }
  });

  // If heartbeats are still enabled for this core, schedule the next check.
  if (checker_state.heartbeat.active.load()) {
    timer->Set(Deadline::after(HeartbeatLockupChecker::period()), DoHeartbeatAndCheckPeerCpus, arg);
  }
}

// Stop the process of having the current CPU recording heartbeats and checking
// in on other CPUs.
void stop_heartbeats() {
  LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  state.heartbeat.active.store(false);
  state.lockup_detector_timer.Cancel();
}

// Start the process of recording heartbeats and checking in on other CPUs on
// the current CPU.
void start_heartbeats() {
  if (!HeartbeatLockupChecker::IsEnabled()) {
    stop_heartbeats();
    return;
  }

  // To be safe, make sure we have a recent last heartbeat before activating.
  LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  auto& hb_state = state.heartbeat;
  const zx_time_t now = current_time();
  hb_state.last_heartbeat.store(now);
  hb_state.active.store(true);

  // Use a deadline with some jitter to avoid having all CPUs heartbeat at the same time.
  const Deadline deadline = DeadlineWithJitterAfter(HeartbeatLockupChecker::period(), 10);
  state.lockup_detector_timer.Set(deadline, DoHeartbeatAndCheckPeerCpus, &state);
}

}  // namespace

void lockup_primary_init() {
  // Initialize parameters for the heartbeat checks.
  HeartbeatLockupChecker::InitStaticParams();

  dprintf(INFO,
          "lockup_detector: heartbeats %s, period is %" PRId64 " ms, threshold is %" PRId64 " ms\n",
          HeartbeatLockupChecker::IsEnabled() ? "enabled" : "disabled",
          HeartbeatLockupChecker::period() / ZX_MSEC(1),
          HeartbeatLockupChecker::threshold() / ZX_MSEC(1));

  // Initialize parameters for the critical section checks, but only if the
  // heartbeat mechanism is enabled.  If the heartbeat mechanism is disabled, no
  // checks will ever be performed.
  //
  // TODO(johngro): relax this.  There is no strong reason to not do our
  // periodic checking if any of the check conditions are enabled.
  if constexpr (LOCKUP_CRITICAL_SECTION_ENALBED) {
    if (HeartbeatLockupChecker::IsEnabled()) {
      CriticalSectionLockupChecker::InitStaticParams();

      if (CriticalSectionLockupChecker::IsEnabled()) {
        dprintf(INFO,
                "lockup_detector: critical section threshold is %" PRId64 " ticks (%" PRId64
                " ms)\n",
                CriticalSectionLockupChecker::threshold_ticks(),
                TicksToDuration(CriticalSectionLockupChecker::threshold_ticks()) / ZX_MSEC(1));
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

void lockup_begin() {
  LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  auto& cs_state = state.critical_section;

  // We must maintain the invariant that if a call to `lockup_begin()` increments the depth, the
  // matching call to `lockup_end()` decrements it.  The most reliable way to accomplish that is to
  // always increment and always decrement.
  cs_state.depth++;
  if (cs_state.depth != 1) {
    // We must be in a nested critical section.  Do nothing so that only the outermost critical
    // section is measured.
    return;
  }

  if (!CriticalSectionLockupChecker::IsEnabled()) {
    // Lockup detector is disabled.
    return;
  }

  const zx_ticks_t now = current_ticks();
  cs_state.begin_ticks.store(now, ktl::memory_order_relaxed);
}

void lockup_end() {
  LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  auto& cs_state = state.critical_section;

  // Defer decrementing until the very end of this function to protect against reentrancy hazards
  // from the calls to `KERNEL_OOPS` and `Thread::Current::PrintBacktrace`.
  //
  // Every call to `lockup_end()` must decrement the depth because every call to `lockup_begin()` is
  // guaranteed to increment it.
  auto cleanup = fbl::MakeAutoCall([&cs_state]() {
    DEBUG_ASSERT(cs_state.depth > 0);
    cs_state.depth--;
  });

  if (cs_state.depth != 1) {
    // We must be in a nested critical section.  Do not zero out our begin_ticks
    // value, so that only the outermost critical section is measured.  Simply
    // let the cleanup lambda decrement our current depth.
    return;
  }

  // Is this a new worst for us?
  const zx_ticks_t now_ticks = current_ticks();
  const zx_ticks_t begin = cs_state.begin_ticks.load(ktl::memory_order_relaxed);
  zx_ticks_t delta = zx_time_sub_time(now_ticks, begin);
  if (delta > cs_state.worst_case_ticks.load(ktl::memory_order_relaxed)) {
    cs_state.worst_case_ticks.store(delta, ktl::memory_order_release);
  }

  // Update our counters.
  CriticalSectionLockupChecker::RecordCriticalSectionBucketCounters(delta);

  // We are done with the CS now.  Clear the begin time to indicate that we are
  // not in any critical section.
  cs_state.begin_ticks.store(0, ktl::memory_order_relaxed);
}

int64_t lockup_get_critical_section_oops_count() { return counter_lockup_cs_count.Value(); }

int64_t lockup_get_no_heartbeat_oops_count() { return counter_lockup_no_heartbeat_oops.Value(); }

namespace {

void lockup_status() {
  const zx_ticks_t ticks = CriticalSectionLockupChecker::threshold_ticks();
  printf("critical section threshold is %" PRId64 " ticks (%" PRId64 " ns)\n", ticks,
         TicksToDuration(ticks));
  if (ticks != 0) {
    for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
      if (!mp_is_cpu_active(i)) {
        printf("CPU-%u is not active, skipping\n", i);
        continue;
      }

      const auto& cs_state = percpu::Get(i).lockup_detector_state.critical_section;
      const zx_ticks_t begin_ticks = cs_state.begin_ticks.load(ktl::memory_order_relaxed);
      const zx_ticks_t now = current_ticks();
      const int64_t worst_case_usec =
          TicksToDuration(cs_state.worst_case_ticks.load(ktl::memory_order_acquire)) / ZX_USEC(1);
      if (begin_ticks == 0) {
        printf("CPU-%u not in critical section (worst case %" PRId64 " uSec)\n", i,
               worst_case_usec);
      } else {
        zx_duration_t duration = TicksToDuration(now - begin_ticks);
        printf("CPU-%u in critical section for %" PRId64 " ms (worst case %" PRId64 " uSec)\n", i,
               duration / ZX_MSEC(1), worst_case_usec);
      }
    }
  }

  printf("heartbeat period is %" PRId64 " ms, heartbeat threshold is %" PRId64 " ms\n",
         HeartbeatLockupChecker::period() / ZX_MSEC(1),
         HeartbeatLockupChecker::threshold() / ZX_MSEC(1));

  percpu::ForEach([](cpu_num_t cpu, percpu* percpu_state) {
    if (!mp_is_cpu_online(cpu) | !mp_is_cpu_active(cpu)) {
      return;
    }

    const auto& hb_state = percpu_state->lockup_detector_state.heartbeat;
    if (!hb_state.active.load()) {
      printf("CPU-%u heartbeats disabled\n", cpu);
      return;
    }
    const zx_time_t last_heartbeat = hb_state.last_heartbeat.load();
    const zx_duration_t age = zx_time_sub_time(current_time(), last_heartbeat);
    const zx_duration_t max_gap = hb_state.max_gap.load();
    printf("CPU-%u last heartbeat at %" PRId64 " ms, age is %" PRId64 " ms, max gap is %" PRId64
           " ms\n",
           cpu, last_heartbeat / ZX_MSEC(1), age / ZX_MSEC(1), max_gap / ZX_MSEC(1));
  });
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
    DECLARE_SINGLETON_SPINLOCK(lockup_test_lock);
    Guard<SpinLock, IrqSave> guard{lockup_test_lock::Get()};
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
    LOCKUP_BEGIN();
    const zx_time_t deadline = zx_time_add_duration(current_time(), duration);
    while (current_time() < deadline) {
      arch::Yield();
    }
    LOCKUP_END();
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
