// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/profile_dispatcher.h>

#include <err.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>

zx_status_t ProfileDispatcher::Create(const zx_profile_info_t& info,
                                      fbl::RefPtr<Dispatcher>* dispatcher,
                                      zx_rights_t* rights) {
    if (info.type != ZX_PROFILE_INFO_SCHEDULER)
        return ZX_ERR_NOT_SUPPORTED;

    fbl::AllocChecker ac;
    auto disp = new (&ac) ProfileDispatcher(info);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_PROFILE_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

ProfileDispatcher::ProfileDispatcher(const zx_profile_info_t& info)
    : info_(info) {}

ProfileDispatcher::~ProfileDispatcher() {
    // TODO(cpu): this silences clang "unused info_" nag. Remove.
    (void) info_.scheduler.priority;
}
