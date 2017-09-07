// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/resource_dispatcher.h>

#include <magenta/rights.h>
#include <fbl/alloc_checker.h>

#include <kernel/auto_lock.h>
#include <string.h>


mx_status_t ResourceDispatcher::Create(fbl::RefPtr<ResourceDispatcher>* dispatcher,
                                       mx_rights_t* rights, uint32_t kind,
                                       uint64_t low, uint64_t high) {
    fbl::AllocChecker ac;
    ResourceDispatcher* disp = new (&ac) ResourceDispatcher(kind, low, high);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_RESOURCE_RIGHTS;
    *dispatcher = fbl::AdoptRef<ResourceDispatcher>(disp);
    return MX_OK;
}

ResourceDispatcher::ResourceDispatcher(uint32_t kind, uint64_t low, uint64_t high) :
    kind_(kind), low_(low), high_(high), state_tracker_(0) {
}

ResourceDispatcher::~ResourceDispatcher() {
}
