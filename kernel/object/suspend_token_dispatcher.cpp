// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/suspend_token_dispatcher.h>

#include <err.h>

#include <kernel/auto_lock.h>
#include <object/thread_dispatcher.h>
#include <zircon/rights.h>
#include <fbl/alloc_checker.h>

zx_status_t SuspendTokenDispatcher::Create(fbl::RefPtr<ThreadDispatcher> thread,
                                           fbl::RefPtr<Dispatcher>* dispatcher,
                                           zx_rights_t* rights) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<SuspendTokenDispatcher> disp(
        new (&ac) SuspendTokenDispatcher(ktl::move(thread)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = disp->thread_->Suspend();
    if (status != ZX_OK)
        return ZX_ERR_BAD_STATE;

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp.release());
    return ZX_OK;
}

SuspendTokenDispatcher::SuspendTokenDispatcher(fbl::RefPtr<ThreadDispatcher> thread)
    : thread_(ktl::move(thread)) {}

SuspendTokenDispatcher::~SuspendTokenDispatcher() {}

void SuspendTokenDispatcher::on_zero_handles() {
    thread_->Resume();
}
