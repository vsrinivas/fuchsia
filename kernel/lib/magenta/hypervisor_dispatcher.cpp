// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <kernel/vm/vm_object.h>
#include <magenta/hypervisor_dispatcher.h>
#include <new.h>

constexpr mx_rights_t kDefaultHypervisorRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE;

// static
mx_status_t HypervisorDispatcher::Create(mxtl::RefPtr<Dispatcher>* dispatcher,
                                      mx_rights_t* rights) {
    // TODO(abdulla): Only allow a single HypervisorContext at a time.
    mxtl::unique_ptr<HypervisorContext> context;
    mx_status_t status = arch_hypervisor_create(&context);
    if (status != NO_ERROR)
        return status;

    AllocChecker ac;
    auto hypervisor = mxtl::AdoptRef(new (&ac) HypervisorDispatcher(mxtl::move(context)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultHypervisorRights;
    *dispatcher = mxtl::RefPtr<Dispatcher>(hypervisor.get());
    return NO_ERROR;
}

HypervisorDispatcher::HypervisorDispatcher(mxtl::unique_ptr<HypervisorContext> context)
    : context_(mxtl::move(context)) {}

HypervisorDispatcher::~HypervisorDispatcher() {}
