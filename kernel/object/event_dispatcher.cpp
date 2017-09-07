// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/event_dispatcher.h>

#include <err.h>

#include <magenta/rights.h>
#include <fbl/alloc_checker.h>
#include <object/state_tracker.h>

constexpr uint32_t kUserSignalMask = MX_EVENT_SIGNALED | MX_USER_SIGNAL_ALL;

mx_status_t EventDispatcher::Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) EventDispatcher(options);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_EVENT_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
}

EventDispatcher::EventDispatcher(uint32_t options)
        : state_tracker_(0u) {}

EventDispatcher::~EventDispatcher() {}

mx_status_t EventDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if (peer)
        return MX_ERR_NOT_SUPPORTED;

    if ((set_mask & ~kUserSignalMask) || (clear_mask & ~kUserSignalMask))
        return MX_ERR_INVALID_ARGS;

    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}
