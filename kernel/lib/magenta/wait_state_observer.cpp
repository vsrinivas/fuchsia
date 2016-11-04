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
                                     mx_signals_t watched_signals) {
    DEBUG_ASSERT(!dispatcher_);

    event_ = event;
    handle_ = handle;
    watched_signals_ = watched_signals;
    dispatcher_ = handle->dispatcher();
    wakeup_reasons_ = 0u;

    auto state_tracker = dispatcher_->get_state_tracker();
    mx_status_t result = (state_tracker != nullptr)
            ? state_tracker->AddObserver(this) : ERR_NOT_SUPPORTED;

    if (result != NO_ERROR)
        dispatcher_.reset();
    return result;
}

mx_signals_t WaitStateObserver::End() {
    DEBUG_ASSERT(dispatcher_);

    auto tracker = dispatcher_->get_state_tracker();
    DEBUG_ASSERT(tracker);
    if (tracker)
        tracker->RemoveObserver(this);
    dispatcher_.reset();

    // Return the set of reasons that we may have been woken.  Basically, this
    // is set of satisfied bits which were ever set while we were waiting on the list.
    //
    // TODO(johngro): should these reasons be masked by watched_signals_?
    return wakeup_reasons_;
}

bool WaitStateObserver::OnInitialize(mx_signals_t initial_state) {
    // Record the initial state of the state tracker as our wakeup reason.  If
    // we are going to become immediately signaled, the reason is contained
    // somewhere in this initial state.
    wakeup_reasons_ = initial_state;
    return MaybeSignal(initial_state);
}

bool WaitStateObserver::OnStateChange(mx_signals_t new_state) {
    // If we are still on our StateTracker's list of observers, and the
    // StateTracker's state has changed, accumulate the reasons that we may have
    // woken up.  In particular any satisfied bits which have become set
    // while we were on the list may have been reasons to wake up.
    wakeup_reasons_ |= new_state;
    return MaybeSignal(new_state);
}

bool WaitStateObserver::OnCancel(Handle* handle, bool* should_remove) {
    DEBUG_ASSERT(dispatcher_);

    if (handle == handle_) {
        wakeup_reasons_ |= MX_SIGNAL_HANDLE_CLOSED;
        return event_->Signal() > 0;
    } else {
        return false;
    }
}

bool WaitStateObserver::MaybeSignal(mx_signals_t state) {
    DEBUG_ASSERT(dispatcher_);

    if (state & watched_signals_)
        return event_->Signal() > 0;

    return false;
}
