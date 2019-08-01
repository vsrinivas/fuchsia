// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/profile_dispatcher.h"

#include <err.h>
#include <lib/counters.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <object/thread_dispatcher.h>

KCOUNTER(dispatcher_profile_create_count, "dispatcher.profile.create")
KCOUNTER(dispatcher_profile_destroy_count, "dispatcher.profile.destroy")

zx_status_t validate_profile(const zx_profile_info_t& info) {
  uint32_t flags = info.flags;

  // We currently support only the priority flag. (In particular, no
  // flags set is unsupported, and more flags are unsupported.)
  if (flags == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (flags != ZX_PROFILE_INFO_FLAG_PRIORITY) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Ensure priority is valid.
  if ((info.priority < LOWEST_PRIORITY) || (info.priority > HIGHEST_PRIORITY)) {
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
  // At the moment, the only thing we support is the priority.
  if ((info_.flags & ZX_PROFILE_INFO_FLAG_PRIORITY) != 0) {
    return thread->SetPriority(info_.priority);
  }
  return ZX_OK;
}
