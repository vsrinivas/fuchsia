// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_
#define SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_

#include <zircon/types.h>

__BEGIN_CDECLS

#define ZX_PROFILE_INFO_SCHEDULER 1

typedef struct zx_profile_scheduler {
  int32_t priority;
  uint32_t boost;
  uint32_t deboost;
  uint32_t quantum;
} zx_profile_scheduler_t;

#define ZX_PRIORITY_LOWEST 0
#define ZX_PRIORITY_LOW 8
#define ZX_PRIORITY_DEFAULT 16
#define ZX_PRIORITY_HIGH 24
#define ZX_PRIORITY_HIGHEST 31

#define ZX_PROFILE_INFO_FLAG_PRIORITY (1 << 0)
#define ZX_PROFILE_INFO_FLAG_CPU_MASK (1 << 1)

typedef struct zx_profile_info {
  // TODO(ZX-3828): Once fields below are implemented, remove fields |type| and |scheduler|.
  uint32_t type;  // one of ZX_PROFILE_INFO_
  union {
    zx_profile_scheduler_t scheduler;
  };

  // A bitmask of ZX_PROFILE_INFO_FLAG_* values. Specifies which fields
  // below have been specified. Other fields are considered unset.
  uint32_t flags;

  // Scheduling priority. |flags| must have ZX_PROFILE_INFO_FLAG_PRIORITY set.
  int32_t priority;

  // CPUs that threads may be scheduled on. |flags| must have
  // ZX_PROFILE_INFO_FLAG_CPU_MASK set.
  zx_cpu_set_t cpu_affinity_mask;
} zx_profile_info_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_PROFILE_H_
