// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_

#include <lib/relaxed_atomic.h>

#include <kernel/event_limiter.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>

// Per CPU state for lockup detector.
struct LockupDetectorState {
  /////////////////////////////////////////////////////////////////////////////
  //
  // Common per-cpu lockup detector state
  //
  /////////////////////////////////////////////////////////////////////////////

  // Every active CPU wakes up periodically to record a heartbeat, as well as
  // to check to see if any of its peers are showing signs of problems.  The
  // lockup detector timer is the timer used for this.
  Timer lockup_detector_timer;

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
    // The time (tick count) at which the CPU entered the critical section.
    ktl::atomic<zx_ticks_t> begin_ticks = 0;

    // Critical sections may be nested so must keep track of the depth.
    uint32_t depth = 0;

    EventLimiter<ZX_SEC(1)> alert_limiter;
  } critical_section;
};

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_
