// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/profile_dispatcher.h"

#include <bits.h>
#include <lib/counters.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <ktl/popcount.h>
#include <object/thread_dispatcher.h>

KCOUNTER(dispatcher_profile_create_count, "dispatcher.profile.create")
KCOUNTER(dispatcher_profile_destroy_count, "dispatcher.profile.destroy")

static zx_status_t parse_cpu_mask(const zx_cpu_set_t& set, cpu_mask_t* result) {
  // The code below only supports reading up to 1 word in the mask.
  static_assert(SMP_MAX_CPUS <= sizeof(set.mask[0]) * CHAR_BIT);
  static_assert(SMP_MAX_CPUS <= sizeof(cpu_mask_t) * CHAR_BIT);
  static_assert(SMP_MAX_CPUS <= ZX_CPU_SET_MAX_CPUS);

  // We throw away any bits beyond SMP_MAX_CPUs.
  *result = static_cast<cpu_mask_t>(set.mask[0] & BIT_MASK(SMP_MAX_CPUS));
  return ZX_OK;
}

zx_status_t validate_profile(const zx_profile_info_t& info) {
  uint32_t flags = info.flags;

  // Ensure at least one flag has been set.
  if (flags == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Ensure only zero or one of the mutually exclusive flags is set.
  const uint32_t kMutuallyExclusiveFlags =
      ZX_PROFILE_INFO_FLAG_PRIORITY | ZX_PROFILE_INFO_FLAG_DEADLINE;
  if (ktl::popcount(flags & kMutuallyExclusiveFlags) > 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Ensure priority is valid.
  if ((flags & ZX_PROFILE_INFO_FLAG_PRIORITY) != 0) {
    if ((info.priority < LOWEST_PRIORITY) || (info.priority > HIGHEST_PRIORITY)) {
      return ZX_ERR_INVALID_ARGS;
    }
    flags &= ~ZX_PROFILE_INFO_FLAG_PRIORITY;
  }

  if ((flags & ZX_PROFILE_INFO_FLAG_DEADLINE) != 0) {
    // TODO(eieio): Add additional admission criteria to prevent values that are
    // too large or too small. These values are mediated by a privileged service
    // so the risk of abuse is low, but it still might be good to implement some
    // sort of failsafe check to prevent mistakes.
    const bool admissible =
        info.deadline_params.capacity > 0 &&
        info.deadline_params.capacity <= info.deadline_params.relative_deadline &&
        info.deadline_params.relative_deadline <= info.deadline_params.period;
    if (!admissible) {
      return ZX_ERR_INVALID_ARGS;
    }
    flags &= ~ZX_PROFILE_INFO_FLAG_DEADLINE;
  }

  // Ensure affinity mask is valid.
  if ((flags & ZX_PROFILE_INFO_FLAG_CPU_MASK) != 0) {
    cpu_mask_t unused_mask;
    zx_status_t result = parse_cpu_mask(info.cpu_affinity_mask, &unused_mask);
    if (result != ZX_OK) {
      return result;
    }
    flags &= ~ZX_PROFILE_INFO_FLAG_CPU_MASK;
  }

  // Ensure no other flags have been set.
  if (flags != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t ProfileDispatcher::Create(const zx_profile_info_t& info,
                                      KernelHandle<ProfileDispatcher>* handle,
                                      zx_rights_t* rights) {
  auto status = validate_profile(info);
  if (status != ZX_OK)
    return status;

  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) ProfileDispatcher(info)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

ProfileDispatcher::ProfileDispatcher(const zx_profile_info_t& info) : info_(info) {
  kcounter_add(dispatcher_profile_create_count, 1);
}

ProfileDispatcher::~ProfileDispatcher() { kcounter_add(dispatcher_profile_destroy_count, 1); }

zx_status_t ProfileDispatcher::ApplyProfile(fbl::RefPtr<ThreadDispatcher> thread) {
  // Set priority.
  if ((info_.flags & ZX_PROFILE_INFO_FLAG_PRIORITY) != 0) {
    zx_status_t result = thread->SetPriority(info_.priority);
    if (result != ZX_OK) {
      return result;
    }
  }

  // Set deadline.
  if ((info_.flags & ZX_PROFILE_INFO_FLAG_DEADLINE) != 0) {
    zx_status_t result = thread->SetDeadline(info_.deadline_params);
    if (result != ZX_OK) {
      return result;
    }
  }

  // Set affinity.
  if ((info_.flags & ZX_PROFILE_INFO_FLAG_CPU_MASK) != 0) {
    cpu_mask_t mask;
    zx_status_t result = parse_cpu_mask(info_.cpu_affinity_mask, &mask);
    if (result != ZX_OK) {
      return result;
    }
    return thread->SetSoftAffinity(mask);
  }

  return ZX_OK;
}
