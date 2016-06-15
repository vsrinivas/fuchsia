// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/interrupt_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultInterruptRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

// static
status_t InterruptDispatcher::Create(uint32_t vector, uint32_t flags,
                                     utils::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights) {
    interrupt_event_t ie;

    // convert from MX_FLAG to internal INTERRUPT_EVENT_FLAG
    uint32_t ie_flags = 0;
    ie_flags |= (flags & MX_FLAG_REMAP_IRQ) ? INTERRUPT_EVENT_FLAG_REMAP_IRQ : 0;

    status_t result = interrupt_event_create(vector, ie_flags, &ie);
    if (result != NO_ERROR) return result;

    auto disp = new InterruptDispatcher(ie);
    if (!disp) return ERR_NO_MEMORY;

    *rights = kDefaultInterruptRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

InterruptDispatcher::InterruptDispatcher(interrupt_event_t ie)
    : interrupt_event_(ie), signaled_(false) {}

InterruptDispatcher::~InterruptDispatcher() {}

status_t InterruptDispatcher::InterruptWait() {
    if (signaled_) {
        LTRACEF("need to call InterruptHandle::Complete() first!\n");
        return ERR_BAD_STATE;
    }

    status_t result = interrupt_event_wait(interrupt_event_);
    if (result != NO_ERROR) return result;

    signaled_ = true;
    return NO_ERROR;
}

status_t InterruptDispatcher::InterruptComplete() {
    if (signaled_) {
        interrupt_event_complete(interrupt_event_);
        signaled_ = false;
    }
    return NO_ERROR;
}

void InterruptDispatcher::Close(Handle* handle) {
    // TODO(yky): figure this out with thread lifecycle. need to wake up the waiting thread
}
