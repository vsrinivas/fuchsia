// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_
#define SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_

#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define ZX_PRIORITY_LOWEST 0
#define ZX_PRIORITY_LOW 8
#define ZX_PRIORITY_DEFAULT 16
#define ZX_PRIORITY_HIGH 24
#define ZX_PRIORITY_HIGHEST 31

#define ZX_PROFILE_INFO_FLAG_PRIORITY (1 << 0)
#define ZX_PROFILE_INFO_FLAG_CPU_MASK (1 << 1)
#define ZX_PROFILE_INFO_FLAG_DEADLINE (1 << 2)

typedef struct zx_profile_info {
  // A bitmask of ZX_PROFILE_INFO_FLAG_* values. Specifies which fields
  // below have been specified. Other fields are considered unset.
  uint32_t flags;

  uint8_t padding1[4];

  union {
    struct {
      // Scheduling priority. |flags| must have ZX_PROFILE_INFO_FLAG_PRIORITY set.
      int32_t priority;

      uint8_t padding2[20];
    };

    // Scheduling deadline. |flags| must have ZX_PROFILE_INFO_FLAG_DEADLINE set.
    zx_sched_deadline_params_t deadline_params;
  };

  // CPUs that threads may be scheduled on. |flags| must have
  // ZX_PROFILE_INFO_FLAG_CPU_MASK set.
  zx_cpu_set_t cpu_affinity_mask;
} zx_profile_info_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_
