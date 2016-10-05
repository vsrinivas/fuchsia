// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/wait_state_observer.h>

#include <assert.h>

#include <magenta/state_tracker.h>
#include <magenta/wait_event.h>

#include <mxtl/type_support.h>

WaitStateObserver::~WaitStateObserver() {
    DEBUG_ASSERT(!dispatcher_);
}

mx_status_t WaitStateObserver::Begin(WaitEvent* event,
                                     Handle* handle,
                                     mx_signals_t watched_signals,
                                     uint64_t context) {
    DEBUG_ASSERT(!dispatcher_);

    event_ = event;
    handle_ = handle;
    watched_signals_ = watched_signals;
    context_ = context;
    dispatcher_ = handle->dispatcher();
    auto state_tracker = dispatcher_->get_state_tracker();
    mx_status_t result = (state_tracker && state_tracker->is_waitable())
            ? state_tracker->AddObserver(this) : ERR_NOT_SUPPORTED;
    if (result != NO_ERROR)
        dispatcher_.reset();
    return result;
}

mx_signals_state_t WaitStateObserver::End() {
    DEBUG_ASSERT(dispatcher_);

    mx_signals_state_t state = {};
    auto tracker = dispatcher_->get_state_tracker();
    DEBUG_ASSERT(tracker);
    if (tracker)
        state = tracker->RemoveObserver(this);
    dispatcher_.reset();
    return state;
}

bool WaitStateObserver::OnInitialize(mx_signals_state_t initial_state) {
    return MaybeSignal(initial_state);
}

bool WaitStateObserver::OnStateChange(mx_signals_state_t new_state) {
    return MaybeSignal(new_state);
}

bool WaitStateObserver::OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) {
    DEBUG_ASSERT(!*should_remove);  // We'll leave it at its default value, which should be false.
    DEBUG_ASSERT(dispatcher_);

    return (handle == handle_) ? event_->Signal(WaitEvent::Result::CANCELLED, context_) : false;
}

bool WaitStateObserver::MaybeSignal(mx_signals_state_t state) {
    DEBUG_ASSERT(dispatcher_);

    if (state.satisfied & watched_signals_)
        return event_->Signal(WaitEvent::Result::SATISFIED, context_);
    if (!(state.satisfiable & watched_signals_))
        return event_->Signal(WaitEvent::Result::UNSATISFIABLE, context_);
    return false;
}
