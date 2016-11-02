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

    // ASSERT that the signal masks are still 32 bits.  If this changes, the
    // all-FFs constant below will need to be updated.
    static_assert(sizeof(wakeup_reasons_.satisfiable) == 4, "Signal mask is not 32 bits!");
    wakeup_reasons_.satisfied   = 0u;
    wakeup_reasons_.satisfiable = 0xFFFFFFFFu;

    auto state_tracker = dispatcher_->get_state_tracker();
    mx_status_t result = (state_tracker && state_tracker->is_waitable())
            ? state_tracker->AddObserver(this) : ERR_NOT_SUPPORTED;

    if (result != NO_ERROR)
        dispatcher_.reset();
    return result;
}

mx_signals_state_t WaitStateObserver::End() {
    DEBUG_ASSERT(dispatcher_);

    auto tracker = dispatcher_->get_state_tracker();
    DEBUG_ASSERT(tracker);
    if (tracker)
        tracker->RemoveObserver(this);
    dispatcher_.reset();

    // Return the set of reasons that we may have been woken.  Basically, this
    // is set of satisfied bits which were ever set, and the set of satisfiable
    // bits which were ever cleared while we were waiting on the list.
    //
    // TODO(johngro): should these reasons be masked by watched_signals_?  Also,
    // should the sense of satisfiable be flipped?  IOW - would it be better to
    // tell the user that they were woken because of an unsatisfiable constraint
    // by having the bit set in the returned reasons instead of having it
    // cleared?
    return wakeup_reasons_;
}

bool WaitStateObserver::OnInitialize(mx_signals_state_t initial_state) {
    // Record the initial state of the state tracker as our wakeup reason.  If
    // we are going to become immediately signaled, the reason is contained
    // somewhere in this initial state.
    wakeup_reasons_ = initial_state;
    return MaybeSignal(initial_state);
}

bool WaitStateObserver::OnStateChange(mx_signals_state_t new_state) {
    // If we are still on our StateTracker's list of observers, and the
    // StateTracker's state has changed, accumulate the reasons that we may have
    // woken up.  In particular any satisfied bits which have become set and any
    // satisfiable bits which have become cleared while we were on the list may have
    // been reasons to wake up.
    wakeup_reasons_.satisfied   |=  new_state.satisfied;
    wakeup_reasons_.satisfiable &= ~new_state.satisfiable;
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

    return false;
}
