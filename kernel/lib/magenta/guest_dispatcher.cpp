// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/guest_dispatcher.h>
#include <new.h>

constexpr mx_rights_t kDefaultGuestRights = MX_RIGHT_EXECUTE;

// static
mx_status_t GuestDispatcher::Create(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    mxtl::unique_ptr<GuestContext> context;
    mx_status_t status = arch_guest_create(&context);
    if (status != NO_ERROR)
        return status;

    AllocChecker ac;
    auto guest = mxtl::AdoptRef(new (&ac) GuestDispatcher(hypervisor, mxtl::move(context)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultGuestRights;
    *dispatcher = mxtl::RefPtr<Dispatcher>(guest.get());
    return NO_ERROR;
}

GuestDispatcher::GuestDispatcher(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                                 mxtl::unique_ptr<GuestContext> context)
    : hypervisor_(hypervisor), context_(mxtl::move(context)) {}

GuestDispatcher::~GuestDispatcher() {}

mx_status_t GuestDispatcher::Start(uintptr_t entry, uintptr_t stack) {
    canary_.Assert();

    return arch_guest_start(context_, entry, stack);
}
