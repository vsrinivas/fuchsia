// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/event_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <new.h>

#include <kernel/event.h>

#include <magenta/state_tracker.h>

constexpr mx_rights_t kDefaultEventRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

status_t EventDispatcher::Create(uint32_t options, utils::RefPtr<Dispatcher>* dispatcher,
                                 mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) EventDispatcher(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultEventRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

EventDispatcher::EventDispatcher(uint32_t options)
        : state_tracker_(true, mx_signals_state_t{0u, MX_SIGNAL_SIGNALED | MX_SIGNAL_USER_ALL}) {}

EventDispatcher::~EventDispatcher() {}

status_t EventDispatcher::SignalEvent() {
    state_tracker_.UpdateSatisfied(MX_SIGNAL_SIGNALED, 0u);
    return NO_ERROR;
}

status_t EventDispatcher::ResetEvent() {
    state_tracker_.UpdateSatisfied(0, MX_SIGNAL_SIGNALED);
    return NO_ERROR;
}

status_t EventDispatcher::UserSignal(uint32_t set_mask, uint32_t clear_mask) {
    state_tracker_.UpdateSatisfied(set_mask, clear_mask);
    return NO_ERROR;
}
