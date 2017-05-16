// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/event_dispatcher.h>

#include <err.h>

#include <magenta/state_tracker.h>
#include <mxalloc/new.h>

constexpr mx_rights_t kDefaultEventRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr uint32_t kUserSignalMask = MX_EVENT_SIGNALED | MX_USER_SIGNAL_ALL;

status_t EventDispatcher::Create(uint32_t options, mxtl::RefPtr<Dispatcher>* dispatcher,
                                 mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) EventDispatcher(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultEventRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

EventDispatcher::EventDispatcher(uint32_t options)
        : state_tracker_(0u) {}

EventDispatcher::~EventDispatcher() {}

status_t EventDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if (peer)
        return ERR_NOT_SUPPORTED;

    if ((set_mask & ~kUserSignalMask) || (clear_mask & ~kUserSignalMask))
        return ERR_INVALID_ARGS;

    state_tracker_.UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}
