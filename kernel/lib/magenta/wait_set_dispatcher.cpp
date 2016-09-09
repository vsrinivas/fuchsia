// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/wait_set_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <platform.h>
#include <stdint.h>

#include <kernel/auto_lock.h>

#include <magenta/handle.h>
#include <magenta/magenta.h>
#include <magenta/state_tracker.h>

#include <sys/types.h>

#include <mxtl/type_support.h>

// WaitSetDispatcher::Entry ------------------------------------------------------------------------

// static
status_t WaitSetDispatcher::Entry::Create(mx_signals_t watched_signals,
                                          uint64_t cookie,
                                          mxtl::unique_ptr<Entry>* entry) {
    AllocChecker ac;
    Entry* e = new (&ac) Entry (watched_signals, cookie);
    if (!ac.check())
        return ERR_NO_MEMORY;

    entry->reset(e);
    return NO_ERROR;
}

WaitSetDispatcher::Entry::~Entry() {}

void WaitSetDispatcher::Entry::Init_NoLock(WaitSetDispatcher* wait_set, Handle* handle) {
    DEBUG_ASSERT(wait_set->mutex_.IsHeld());

    DEBUG_ASSERT(state_ == State::UNINITIALIZED);
    state_ = State::ADD_PENDING;

    DEBUG_ASSERT(!wait_set_);
    wait_set_ = wait_set;

    DEBUG_ASSERT(!handle_);
    handle_ = handle;

    DEBUG_ASSERT(!dispatcher_);
    dispatcher_ = handle_->dispatcher();
}

WaitSetDispatcher::Entry::State WaitSetDispatcher::Entry::GetState_NoLock() const {
    // Don't assert |wait_set_->mutex_.IsHeld()| here, since we may get called from
    // WaitSetDispatcher's destructor.
    return state_;
}

Handle* WaitSetDispatcher::Entry::GetHandle_NoLock() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return handle_;
}

void WaitSetDispatcher::Entry::SetState_NoLock(State new_state) {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    state_ = new_state;
}

const mxtl::RefPtr<Dispatcher>& WaitSetDispatcher::Entry::GetDispatcher_NoLock() const {
    // Don't assert |wait_set_->mutex_.IsHeld()| here, since we may get called from
    // WaitSetDispatcher's destructor.
    return dispatcher_;
}

bool WaitSetDispatcher::Entry::IsTriggered_NoLock() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return is_triggered_;
}

mx_signals_state_t WaitSetDispatcher::Entry::GetSignalsState_NoLock() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return signals_state_;
}

WaitSetDispatcher::Entry::Entry(mx_signals_t watched_signals, uint64_t cookie)
    : StateObserver(IrqDisposition::IRQ_UNSAFE),
      watched_signals_(watched_signals), cookie_(cookie) {}

bool WaitSetDispatcher::Entry::OnInitialize(mx_signals_state_t initial_state) {
    AutoLock lock(&wait_set_->mutex_);

    DEBUG_ASSERT(state_ == State::ADD_PENDING);
    state_ = State::ADDED;

    signals_state_ = initial_state;

    if ((watched_signals_ & signals_state_.satisfied) ||
        !(watched_signals_ & signals_state_.satisfiable))
        return Trigger_NoLock();

    return false;
}

bool WaitSetDispatcher::Entry::OnStateChange(mx_signals_state_t new_state) {
    AutoLock lock(&wait_set_->mutex_);

    if (state_ == State::REMOVED)
        return false;

    DEBUG_ASSERT(state_ == State::ADDED);

    signals_state_ = new_state;

    if ((watched_signals_ & signals_state_.satisfied) ||
        !(watched_signals_ & signals_state_.satisfiable)) {
        if (is_triggered_)
            return false;  // Already triggered.
        return Trigger_NoLock();
    }

    if (is_triggered_) {
        DEBUG_ASSERT(InTriggeredEntriesList_NoLock());
        is_triggered_ = false;
        wait_set_->triggered_entries_.erase(*this);

        DEBUG_ASSERT(wait_set_->num_triggered_entries_ > 0u);
        wait_set_->num_triggered_entries_--;
    }
    return false;
}

bool WaitSetDispatcher::Entry::OnCancel(Handle* handle,
                                        bool* should_remove,
                                        bool* call_did_cancel) {
    AutoLock lock(&wait_set_->mutex_);

    if (state_ == State::REMOVED) {
        // |*should_remove| should be false by default. Observing REMOVED here means that we're
        // inside RemoveEntry(), just before the call to RemoveObserver() -- so there's no need for
        // us to remove ourself from the StateTracker's observer list.
        DEBUG_ASSERT(!*should_remove);
        // |*call_did_cancel| should be false by default. The OnDidCancel() callback is
        // is done outside the state tracker lock so WaitSetDispatcher could have been destroyed
        // by the time it runs.
        DEBUG_ASSERT(!*call_did_cancel);
        return false;
    }

    DEBUG_ASSERT(state_ == State::ADDED);

    DEBUG_ASSERT(handle_);
    if (handle != handle_)
        return false;
    handle_ = nullptr;
    dispatcher_.reset();

    *should_remove = true;

    if (!is_triggered_)
        return Trigger_NoLock();

    return false;
}

bool WaitSetDispatcher::Entry::Trigger_NoLock() {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());

    DEBUG_ASSERT(!is_triggered_);
    is_triggered_ = true;

    // Signal if necessary.
    bool was_empty = wait_set_->triggered_entries_.is_empty();
    wait_set_->triggered_entries_.push_back(this);
    wait_set_->num_triggered_entries_++;
    if (was_empty) {
        cond_broadcast(&wait_set_->cv_);
        return !!wait_set_->waiter_count_;
    }

    return false;
}

// WaitSetDispatcher -------------------------------------------------------------------------------

constexpr mx_rights_t kDefaultWaitSetRights = MX_RIGHT_READ | MX_RIGHT_WRITE;

// static
status_t WaitSetDispatcher::Create(mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* d = new (&ac) WaitSetDispatcher();
    if (!ac.check())
        return ERR_NO_MEMORY;

    *dispatcher = mxtl::AdoptRef(d);
    *rights = kDefaultWaitSetRights;
    return NO_ERROR;
}

WaitSetDispatcher::~WaitSetDispatcher() {
    // This is a bit dodgy, but we need to lock |mutex_| even though we're in the destructor, since
    // the |entries_|s' StateObserver methods may still be called (an |Entry| does not keep its
    // owner alive).
    {
        AutoLock lock(&mutex_);

        triggered_entries_.clear();

        for (auto& e : entries_) {
            // If we're being destroyed, every entry in |entries_| should be in the ADDED state (since
            // we can't be in the middle of AddEntry() or RemoveEntry().
            DEBUG_ASSERT(e.GetState_NoLock() == Entry::State::ADDED);
            e.SetState_NoLock(Entry::State::REMOVED);
        }
    }

    // We can only call RemoveObserver() outside the lock.
    for (auto& e : entries_) {
        DEBUG_ASSERT(e.GetState_NoLock() == Entry::State::REMOVED);
        if (e.GetDispatcher_NoLock())
            e.GetDispatcher_NoLock()->get_state_tracker()->RemoveObserver(&e);
    }
    entries_.clear();   // Automatically destroys all Entry objects in entries_

    state_tracker_.RemoveObserver(this);

    // Note these can't be destroyed until we've called RemoveObserver() on the remaining entries.
    cond_destroy(&cv_);
}

status_t WaitSetDispatcher::AddEntry(mxtl::unique_ptr<Entry> entry, Handle* handle) {
    auto state_tracker = handle->dispatcher()->get_state_tracker();
    if (!state_tracker || !state_tracker->is_waitable())
        return ERR_NOT_SUPPORTED;

    auto e = entry.get();
    {
        AutoLock lock(&mutex_);

        if (!entries_.insert_or_find(mxtl::move(entry)))
            return ERR_ALREADY_EXISTS;

        e->Init_NoLock(this, handle);
    }
    // The entry |e| will remain valid since: we'll remain alive (since our caller better have a ref
    // to us) and since e->Init_NoLock() will set the state to ADD_PENDING and RemoveEntry() won't
    // destroy it if it's in that state (the only thing it'll do is remove it from |entries_| and
    // set its state to REMOVE_REQUESTED).

    // We need to call this outside the lock.
    auto result = state_tracker->AddObserver(e);
    if (result != NO_ERROR) {
        AutoLock lock(&mutex_);
        DEBUG_ASSERT(e->GetState_NoLock() == Entry::State::ADD_PENDING);
        DEBUG_ASSERT(entry == nullptr);

        entry = entries_.erase(*e);
        DEBUG_ASSERT(e == entry.get());

        // entry destructs as it goes out of scope.
        return result;
    }

    // Otherwise, AddObserver() calls e->OnInitialize(), which sets |e|'s state to ADDED. WARNING:
    // That state change means that RemoveEntry() may actually call RemoveObserver(), so we must not
    // do any work after calling AddObserver() in the success case!
    return NO_ERROR;
}

status_t WaitSetDispatcher::RemoveEntry(uint64_t cookie) {
    mxtl::unique_ptr<Entry> entry;
    mxtl::RefPtr<Dispatcher> dispatcher;
    {
        AutoLock lock(&mutex_);

        entry = entries_.erase(cookie);
        if (!entry)
            return ERR_NOT_FOUND;

        if (entry->IsTriggered_NoLock()) {
            DEBUG_ASSERT(entry->InTriggeredEntriesList_NoLock());
            triggered_entries_.erase(*entry);

            DEBUG_ASSERT(num_triggered_entries_ > 0u);
            num_triggered_entries_--;
        }

        auto state = entry->GetState_NoLock();
        if (state == Entry::State::ADD_PENDING) {
            // We're *in* AddEntry() on another thread! Just put it back and pretend it hasn't been
            // added yet.
            entries_.insert(mxtl::move(entry));
            return NO_ERROR;
        }
        DEBUG_ASSERT(state == Entry::State::ADDED);
        entry->SetState_NoLock(Entry::State::REMOVED);
        dispatcher = entry->GetDispatcher_NoLock();
    }
    if (dispatcher)
        dispatcher->get_state_tracker()->RemoveObserver(entry.get());

    return NO_ERROR;
}

status_t WaitSetDispatcher::Wait(mx_time_t timeout,
                                 uint32_t* num_results,
                                 mx_waitset_result_t* results,
                                 uint32_t* max_results) {
    AutoLock lock(&mutex_);

    lk_time_t lk_timeout = mx_time_to_lk(timeout);
    status_t result = NO_ERROR;
    if (!num_triggered_entries_ && !cancelled_) {
        result = (lk_timeout == INFINITE_TIME) ? DoWaitInfinite_NoLock()
                                               : DoWaitTimeout_NoLock(lk_timeout);
    } // Else the condition is already satisfied.

    if (result != NO_ERROR && result != ERR_TIMED_OUT) {
        DEBUG_ASSERT(result == ERR_INTERRUPTED);
        return result;
    }

    // Always prefer to give results over timed out, but prefer "cancelled" over everything.
    if (cancelled_)
        return ERR_HANDLE_CLOSED;
    if (!num_triggered_entries_) {
        DEBUG_ASSERT(result == ERR_TIMED_OUT);
        return ERR_TIMED_OUT;
    }

    if (num_triggered_entries_ < *num_results)
        *num_results = num_triggered_entries_;

    auto it = triggered_entries_.cbegin();
    for (uint32_t i = 0; i < *num_results; i++, ++it) {
        DEBUG_ASSERT(it != triggered_entries_.cend());

        results[i].cookie = it->GetKey();
        results[i].reserved = 0u;
        if (it->GetHandle_NoLock()) {
            // Not cancelled: satisfied or unsatisfiable.
            auto st = it->GetSignalsState_NoLock();
            if ((st.satisfied & it->watched_signals())) {
                results[i].wait_result = NO_ERROR;
            } else {
                DEBUG_ASSERT(!(st.satisfiable & it->watched_signals()));
                results[i].wait_result = ERR_BAD_STATE;
            }
            results[i].signals_state = st;
        } else {
            // Cancelled.
            results[i].wait_result = ERR_HANDLE_CLOSED;
            results[i].signals_state = mx_signals_state_t{0u, 0u};
        }
    }

    *max_results = num_triggered_entries_;

    return NO_ERROR;
}

WaitSetDispatcher::WaitSetDispatcher()
    : StateObserver(IrqDisposition::IRQ_UNSAFE),
      state_tracker_(false) {
    cond_init(&cv_);

    // This is just so we can observe our own handle's cancellation.
    state_tracker_.AddObserver(this);
}

bool WaitSetDispatcher::OnInitialize(mx_signals_state_t initial_state) { return false; }

bool WaitSetDispatcher::OnStateChange(mx_signals_state_t new_state) { return false; }

bool WaitSetDispatcher::OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) {
    DEBUG_ASSERT(!*should_remove);  // We'll leave |*should_remove| at its default, which is false.
    DEBUG_ASSERT(!*call_did_cancel);

    AutoLock lock(&mutex_);
    cancelled_ = true;
    cond_broadcast(&cv_);
    return waiter_count_ > 0u;
}

status_t WaitSetDispatcher::DoWaitInfinite_NoLock() {
    DEBUG_ASSERT(mutex_.IsHeld());
    DEBUG_ASSERT(!num_triggered_entries_ && !cancelled_);

    for (;;) {
        status_t result = cond_wait_timeout(&cv_, mutex_.GetInternal(), INFINITE_TIME);
        if (num_triggered_entries_ || cancelled_ || result != NO_ERROR)
            return result;
    }
}

status_t WaitSetDispatcher::DoWaitTimeout_NoLock(lk_time_t timeout) {
    DEBUG_ASSERT(mutex_.IsHeld());
    DEBUG_ASSERT(!num_triggered_entries_ && !cancelled_);

    // Calculate an absolute deadline.
    lk_time_t now = current_time();
    lk_time_t deadline = timeout_to_deadline(now, timeout);
    if (deadline == INFINITE_TIME)
        return DoWaitInfinite_NoLock();

    for (;;) {
        status_t result = cond_wait_timeout(&cv_, mutex_.GetInternal(), deadline - now);
        if (num_triggered_entries_ || cancelled_ || result != NO_ERROR)
            return result;

        now = current_time();
        if (now >= deadline)
            return ERR_TIMED_OUT;
    }
}
