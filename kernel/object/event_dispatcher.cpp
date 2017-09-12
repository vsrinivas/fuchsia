// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/event_dispatcher.h>

#include <err.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <object/state_tracker.h>

constexpr uint32_t kUserSignalMask = ZX_EVENT_SIGNALED | ZX_USER_SIGNAL_ALL;

zx_status_t EventDispatcher::Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) EventDispatcher(options);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_EVENT_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

EventDispatcher::EventDispatcher(uint32_t options)
        : state_tracker_(0u) {}

EventDispatcher::~EventDispatcher() {}

zx_status_t EventDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if (peer)
        return ZX_ERR_NOT_SUPPORTED;

    if ((set_mask & ~kUserSignalMask) || (clear_mask & ~kUserSignalMask))
        return ZX_ERR_INVALID_ARGS;

    state_tracker_.UpdateState(clear_mask, set_mask);
    return ZX_OK;
}
