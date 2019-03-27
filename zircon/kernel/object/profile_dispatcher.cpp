// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/profile_dispatcher.h>

#include <err.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <lib/counters.h>

#include <object/thread_dispatcher.h>

#include <zircon/rights.h>

KCOUNTER(dispatcher_profile_create_count, "dispatcher.profile.create")
KCOUNTER(dispatcher_profile_destroy_count, "dispatcher.profile.destroy")

zx_status_t validate_profile(const zx_profile_info_t& info) {
    if (info.type != ZX_PROFILE_INFO_SCHEDULER)
        return ZX_ERR_NOT_SUPPORTED;
    if ((info.scheduler.priority < LOWEST_PRIORITY) ||
        (info.scheduler.priority  > HIGHEST_PRIORITY))
        return ZX_ERR_INVALID_ARGS;
    return ZX_OK;
}

zx_status_t ProfileDispatcher::Create(const zx_profile_info_t& info,
                                      fbl::RefPtr<Dispatcher>* dispatcher,
                                      zx_rights_t* rights) {
    auto status = validate_profile(info);
    if (status != ZX_OK)
        return status;

    fbl::AllocChecker ac;
    auto disp = new (&ac) ProfileDispatcher(info);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

ProfileDispatcher::ProfileDispatcher(const zx_profile_info_t& info)
    : info_(info) {
    kcounter_add(dispatcher_profile_create_count, 1);
}

ProfileDispatcher::~ProfileDispatcher() {
    kcounter_add(dispatcher_profile_destroy_count, 1);
}

zx_status_t ProfileDispatcher::ApplyProfile(fbl::RefPtr<ThreadDispatcher> thread) {
    // At the moment, the only thing we support is the priority.
    return thread->SetPriority(info_.scheduler.priority);
}
