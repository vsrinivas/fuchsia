// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_SCHEDULER_H_
#define SYSROOT_ZIRCON_SYSCALLS_SCHEDULER_H_

#include <zircon/compiler.h>
#include <zircon/time.h>

__BEGIN_CDECLS

// Parameters for deadline scheduler profiles.
//
// At minimum, the following relation must hold:
//
//   0 < capacity <= relative_deadline <= period
//
// Additional restrictions on the range and granularity of the parameters may be
// enforced, which can vary from system to system.
//
typedef struct zx_sched_deadline_params {
  // The worst case execution time of the deadline work per interarrival period.
  zx_duration_t capacity;

  // The worst case finish time of the deadline work, relative to the beginning
  // of the current interarrival period.
  zx_duration_t relative_deadline;

  // The worst case interarrival period of the deadline work.
  zx_duration_t period;
} zx_sched_deadline_params_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_SCHEDULER_H_
