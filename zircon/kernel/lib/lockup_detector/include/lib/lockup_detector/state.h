// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_

#include <kernel/align.h>
#include <kernel/cpu.h>
#include <kernel/event_limiter.h>
#include <ktl/atomic.h>

// Per CPU state for lockup detector.
struct __CPU_ALIGN LockupDetectorState {
  /////////////////////////////////////////////////////////////////////////////
  //
  // Common per-cpu lockup detector state
  //
  /////////////////////////////////////////////////////////////////////////////

  // The ID of CPU who is currently performing a check of this CPUs conditions,
  // or INVALID_CPU if no CPU currently is. Used to prevent multiple CPUs from
  // recognizing and reporting the same condition on the same CPU concurrently.
  //
  // TODO(johngro): This really should just be a spinlock which is only ever
  // try-locked during the check.  If spinlocks had a trylock operation which
  // was compatible with clang static analysis, we could make use of it here
  // along with static annotations to catch mistakes.
  ktl::atomic<cpu_num_t> current_checker_id{INVALID_CPU};

  /////////////////////////////////////////////////////////////////////////////
  //
  // Per-cpu lockup detector state used for detecting the "heartbeat" condition.
  //
  /////////////////////////////////////////////////////////////////////////////

  struct {
    // A flag used to indicate that this CPU is participating in the heartbeat
    // mechanism.  IOW - it is periodically recording that it is still running in
    // the |last_heartbeat| field, and after doing so it is checking on its peer
    // CPUs.
    //
    // TODO(johngro): Should we merge this field with |last_heartbeat|?  Seems
    // like we should be able to use a sentinel value, such as 0, or
    // ZX_TIME_INFINITE to indicate that the mechanism is disabled.
    ktl::atomic<bool> active = false;

    // The last time at which this CPU checked in.
    ktl::atomic<zx_time_t> last_heartbeat = 0;

    // The largest gap between last_heartbeat and now ever observed by a checker.
    // Note that writes to the field are "protected" by the exclusive role of
    // "checker".
    ktl::atomic<zx_duration_t> max_gap = 0;

    // limiter for the rate at which heartbeat failures are reported.  "Protected"
    // by the exclusive role of "checker".
    EventLimiter<ZX_SEC(1)> alert_limiter;
  } heartbeat;

  /////////////////////////////////////////////////////////////////////////////
  //
  // Per-cpu lockup detector state used for detecting the "critical section"
  // condition.
  //
  /////////////////////////////////////////////////////////////////////////////
  struct {
    // Critical sections may be nested, so lockup_timed_begin and
    // lockup_timed_end (called as code enters and exits critical sections) must
    // keep track of the depth.  This variable is only ever accessed by the code
    // entering and exiting the CS, and always on the same CPU, so there is no
    // need for it to be atomic.  However, because an interrupt may fire as a
    // thread enters a critical section and the interrupt handler itself my
    // enter a critical section, compiler fences must be used when accessing to
    // ensure that compiler reordering does not lead to problem.
    //
    // Accessed only by this CPU.
    uint32_t depth = 0;

    // The name of the active critical section, if any.  May be nullptr.
    //
    // Accessed by both this CPU and observers.
    ktl::atomic<const char*> name{nullptr};

    // The worst case CS time ever observed by the critical section thread as it
    // exits the critical section. This variable is written only by the CPU
    // exiting the critical section, but is read by other CPUs during the
    // heartbeat sanity checks. While the thread exiting the critical section
    // reports the worst cast time via this variable, only the threads
    // performing heartbeat sanity checks will ever report issues (via an OOPS)
    // as a result of a new worst case value.
    //
    // Accessed by both this CPU and observers.
    ktl::atomic<zx_ticks_t> worst_case_ticks{0};

    // The time (tick count) at which the CPU entered the critical section.
    //
    // This field is used to establish Release-Acquire ordering of changes made
    // by critical section threads and observed by observers.
    //
    // Accessed by both this CPU and observers.
    ktl::atomic<zx_ticks_t> begin_ticks{0};

    // State variable used to de-dupe the critical section lockup events for the
    // purposes of updating kcounters.
    //
    // Accessed only by observers.
    zx_ticks_t last_counted_begin_ticks{0};

    // The largest worst case value ever _reported_ by a heartbeat checker.
    // This variable is only ever used by the current checker, and the
    // acquire/release semantics of the current_checker_id variable should
    // ensure that it is coherent on architectures with weak memory ordering.
    //
    // Accessed only by observers.
    zx_ticks_t reported_worst_case_ticks{0};

    // The alert limiter used to rate limit warnings printed for ongoing
    // critical section times (eg; CPUs which enter critical sections, but don't
    // exit them for so long that a heartbeat checker notices them)
    //
    // Accessed only by observers.
    EventLimiter<ZX_SEC(1)> ongoing_call_alert_limiter;

    // The alert limiter used to rate limit warnings printed when the heartbeat
    // monitor notices new, unreported, worst case values.
    //
    // Accessed only by observers.
    EventLimiter<ZX_SEC(1)> worst_case_alert_limiter;
  } critical_section;
};

extern LockupDetectorState gLockupDetectorPerCpuState[SMP_MAX_CPUS];

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_
