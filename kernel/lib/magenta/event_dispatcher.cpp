// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/event_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#include <kernel/event.h>

#include <magenta/waiter.h>

constexpr mx_rights_t kDefaultEventRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

status_t EventDispatcher::Create(uint32_t options, utils::RefPtr<Dispatcher>* dispatcher,
                                 mx_rights_t* rights) {
    auto disp = new EventDispatcher(options);
    if (!disp) return ERR_NO_MEMORY;

    *rights = kDefaultEventRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

EventDispatcher::EventDispatcher(uint32_t options)
        : waiter_(mx_signals_state_t{0u, MX_SIGNAL_SIGNALED}) {}

EventDispatcher::~EventDispatcher() {}

status_t EventDispatcher::SignalEvent() {
    // TODO(cpu): to signal from IRQ we need a diferent entrypoint that calls Satisfied(..., false).
    waiter_.Satisfied(MX_SIGNAL_SIGNALED, 0, true);
    return NO_ERROR;
}

status_t EventDispatcher::ResetEvent() {
    waiter_.Satisfied(0, MX_SIGNAL_SIGNALED, true);
    return NO_ERROR;
}

status_t EventDispatcher::UserSignal(uint32_t set_mask, uint32_t clear_mask) {
    waiter_.Satisfied(set_mask, clear_mask, true);
    return NO_ERROR;
}
