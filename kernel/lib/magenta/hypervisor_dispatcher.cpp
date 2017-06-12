// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/hypervisor_dispatcher.h>
#include <magenta/rights.h>
#include <mxalloc/new.h>
#include <mxtl/auto_lock.h>

static Mutex mutex;
mxtl::RefPtr<HypervisorDispatcher> hypervisor TA_GUARDED(mutex);

// static
mx_status_t HypervisorDispatcher::Create(mxtl::RefPtr<Dispatcher>* dispatcher,
                                         mx_rights_t* rights) {
    AutoLock lock(&mutex);

    if (!hypervisor) {
        mxtl::unique_ptr<HypervisorContext> context;
        mx_status_t status = arch_hypervisor_create(&context);
        if (status != MX_OK)
            return status;

        // TODO(abdulla): We call AdoptRef to create a long-lived singleton.
        // This works for now, but a better solution would be to fully shutdown
        // on destruction. Unfortunately, we can't override Release to keep
        // prevent a race from happening during destruction.
        AllocChecker ac;
        hypervisor = mxtl::AdoptRef(new (&ac) HypervisorDispatcher(mxtl::move(context)));
        if (!ac.check())
            return MX_ERR_NO_MEMORY;
    }

    *rights = MX_DEFAULT_HYPERVISOR_RIGHTS;
    *dispatcher = mxtl::RefPtr<Dispatcher>(hypervisor);
    return MX_OK;
}

HypervisorDispatcher::HypervisorDispatcher(mxtl::unique_ptr<HypervisorContext> context)
    : context_(mxtl::move(context)) {}

HypervisorDispatcher::~HypervisorDispatcher() {}
