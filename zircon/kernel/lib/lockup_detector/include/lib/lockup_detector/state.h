// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_

#include <lib/relaxed_atomic.h>

#include <ktl/atomic.h>
#include <kernel/timer.h>

// Per CPU state for lockup detector.
struct LockupDetectorState {
  // The time (tick count) at which the CPU entered the critical section.
  ktl::atomic<zx_ticks_t> begin_ticks = 0;
  // Critical sections may be nested so must keep track of the depth.
  uint32_t critical_section_depth = 0;

  Timer heartbeat_timer;
  ktl::atomic<bool> heartbeat_active = false;
  ktl::atomic<zx_time_t> last_heartbeat = 0;
  ktl::atomic<zx_duration_t> max_heartbeat_gap = 0;
};

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_STATE_H_

