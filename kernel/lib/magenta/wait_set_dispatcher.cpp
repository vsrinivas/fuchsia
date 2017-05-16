// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/wait_set_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <stdint.h>

#include <kernel/auto_lock.h>

#include <magenta/handle.h>
#include <magenta/magenta.h>
#include <magenta/state_tracker.h>

#include <sys/types.h>

#include <mxalloc/new.h>
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

void WaitSetDispatcher::Entry::InitLocked(WaitSetDispatcher* wait_set, Handle* handle) {
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

WaitSetDispatcher::Entry::State WaitSetDispatcher::Entry::GetStateLocked() const {
    // Don't assert |wait_set_->mutex_.IsHeld()| here, since we may get called from
    // WaitSetDispatcher's destructor.
    return state_;
}

Handle* WaitSetDispatcher::Entry::GetHandleLocked() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return handle_;
}

void WaitSetDispatcher::Entry::SetStateLocked(State new_state) {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    state_ = new_state;
}

const mxtl::RefPtr<Dispatcher>& WaitSetDispatcher::Entry::GetDispatcherLocked() const {
    // Don't assert |wait_set_->mutex_.IsHeld()| here, since we may get called from
    // WaitSetDispatcher's destructor.
    return dispatcher_;
}

bool WaitSetDispatcher::Entry::IsTriggeredLocked() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return is_triggered_;
}

mx_signals_t WaitSetDispatcher::Entry::GetSignalsStateLocked() const {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());
    return signals_;
}

WaitSetDispatcher::Entry::Entry(mx_signals_t watched_signals, uint64_t cookie)
    : StateObserver(), watched_signals_(watched_signals), cookie_(cookie) {}

bool WaitSetDispatcher::Entry::OnInitialize(mx_signals_t initial_state,
                                            const StateObserver::CountInfo* cinfo) {
    AutoLock lock(&wait_set_->mutex_);

    DEBUG_ASSERT(state_ == State::ADD_PENDING);
    state_ = State::ADDED;

    signals_ = initial_state;

    if (watched_signals_ & signals_)
        return TriggerLocked();

    return false;
}

bool WaitSetDispatcher::Entry::OnStateChange(mx_signals_t new_state) {
    AutoLock lock(&wait_set_->mutex_);

    if (state_ == State::REMOVED)
        return false;

    DEBUG_ASSERT(state_ == State::ADDED);

    signals_= new_state;

    if (watched_signals_ & signals_) {
        if (is_triggered_)
            return false;  // Already triggered.
        return TriggerLocked();
    }

    if (is_triggered_) {
        DEBUG_ASSERT(InTriggeredEntriesListLocked());
        is_triggered_ = false;
        wait_set_->triggered_entries_.erase(*this);

        DEBUG_ASSERT(wait_set_->num_triggered_entries_ > 0u);
        wait_set_->num_triggered_entries_--;

        if ((wait_set_->num_triggered_entries_ == 0) &&
            (!wait_set_->cancelled_)) {
            event_unsignal(&wait_set_->event_);
        }
    }
    return false;
}

bool WaitSetDispatcher::Entry::OnCancel(Handle* handle) {
    AutoLock lock(&wait_set_->mutex_);

    if (state_ == State::REMOVED)
        return false;

    DEBUG_ASSERT(state_ == State::ADDED);

    DEBUG_ASSERT(handle_);
    if (handle != handle_)
        return false;
    handle_ = nullptr;
    dispatcher_.reset();

    // We'll be removed from the state observer list.
    remove_ = true;

    if (!is_triggered_)
        return TriggerLocked();

    return false;
}

bool WaitSetDispatcher::Entry::TriggerLocked() {
    DEBUG_ASSERT(wait_set_->mutex_.IsHeld());

    DEBUG_ASSERT(!is_triggered_);
    is_triggered_ = true;

    // Signal if necessary.
    bool was_empty = wait_set_->triggered_entries_.is_empty();
    wait_set_->triggered_entries_.push_back(this);
    wait_set_->num_triggered_entries_++;
    if (was_empty) {
        return event_signal(&wait_set_->event_, true) > 0;
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
            DEBUG_ASSERT(e.GetStateLocked() == Entry::State::ADDED);
            e.SetStateLocked(Entry::State::REMOVED);
        }
    }

    // We can only call RemoveObserver() outside the lock.
    for (auto& e : entries_) {
        DEBUG_ASSERT(e.GetStateLocked() == Entry::State::REMOVED);
        if (e.GetDispatcherLocked())
            e.GetDispatcherLocked()->get_state_tracker()->RemoveObserver(&e);
    }
    entries_.clear();   // Automatically destroys all Entry objects in entries_

    state_tracker_.RemoveObserver(this);

    // Note these can't be destroyed until we've called RemoveObserver() on the remaining entries.
    event_destroy(&event_);
}

status_t WaitSetDispatcher::AddEntry(mxtl::unique_ptr<Entry> entry, Handle* handle) {
    canary_.Assert();

    if (!handle->dispatcher()->get_state_tracker())
        return ERR_NOT_SUPPORTED;

    auto e = entry.get();
    {
        AutoLock lock(&mutex_);

        if (!entries_.insert_or_find(mxtl::move(entry)))
            return ERR_ALREADY_EXISTS;

        e->InitLocked(this, handle);
    }
    // The entry |e| will remain valid since: we'll remain alive (since our caller better have a ref
    // to us) and since e->InitLocked() will set the state to ADD_PENDING and RemoveEntry() won't
    // destroy it if it's in that state (the only thing it'll do is remove it from |entries_| and
    // set its state to REMOVE_REQUESTED).

    // We need to call this outside the lock.
    __UNUSED auto status = handle->dispatcher()->add_observer(e);
    DEBUG_ASSERT(status == NO_ERROR);

    // add_observer() calls e->OnInitialize(), which sets |e|'s state to ADDED. WARNING:
    // That state change means that RemoveEntry() may actually call RemoveObserver(), so we must not
    // do any work after calling add_observer()!
    return NO_ERROR;
}

status_t WaitSetDispatcher::RemoveEntry(uint64_t cookie) {
    canary_.Assert();

    mxtl::unique_ptr<Entry> entry;
    mxtl::RefPtr<Dispatcher> dispatcher;
    {
        AutoLock lock(&mutex_);

        entry = entries_.erase(cookie);
        if (!entry)
            return ERR_NOT_FOUND;

        if (entry->IsTriggeredLocked()) {
            DEBUG_ASSERT(entry->InTriggeredEntriesListLocked());
            triggered_entries_.erase(*entry);

            DEBUG_ASSERT(num_triggered_entries_ > 0u);
            num_triggered_entries_--;
        }

        auto state = entry->GetStateLocked();
        if (state == Entry::State::ADD_PENDING) {
            // We're *in* AddEntry() on another thread! Just put it back and pretend it hasn't been
            // added yet.
            entries_.insert(mxtl::move(entry));
            return NO_ERROR;
        }
        DEBUG_ASSERT(state == Entry::State::ADDED);
        entry->SetStateLocked(Entry::State::REMOVED);
        dispatcher = entry->GetDispatcherLocked();
    }
    if (dispatcher)
        dispatcher->get_state_tracker()->RemoveObserver(entry.get());

    return NO_ERROR;
}

status_t WaitSetDispatcher::Wait(mx_time_t deadline,
                                 uint32_t* num_results,
                                 mx_waitset_result_t* results,
                                 uint32_t* max_results) {
    canary_.Assert();

    status_t result = event_wait_deadline(&event_, deadline, true);

    if (result != NO_ERROR && result != ERR_TIMED_OUT) {
        DEBUG_ASSERT(result == ERR_INTERRUPTED);
        return result;
    }

    AutoLock lock(&mutex_);

    // Always prefer to give results over timed out, but prefer "cancelled" over everything.
    if (cancelled_)
        return ERR_CANCELED;

    if (!num_triggered_entries_) {
        // It's *possible* that we woke due to something triggering
        // that managed to untrigger between our wakeup and processing
        // these under the lock
        *max_results = 0;
        *num_results = 0;
        return result;
    }

    if (num_triggered_entries_ < *num_results)
        *num_results = num_triggered_entries_;

    auto it = triggered_entries_.cbegin();
    for (uint32_t i = 0; i < *num_results; i++, ++it) {
        DEBUG_ASSERT(it != triggered_entries_.cend());

        results[i].cookie = it->GetKey();
        if (it->GetHandleLocked()) {
            // Not cancelled
            results[i].status = NO_ERROR;
            results[i].observed = it->GetSignalsStateLocked();
        } else {
            // Cancelled.
            results[i].status = ERR_CANCELED;
            results[i].observed = 0;
        }
    }

    *max_results = num_triggered_entries_;

    return result;
}

WaitSetDispatcher::WaitSetDispatcher()
    : StateObserver(), state_tracker_(false) {
    event_init(&event_, false, 0);

    // This is just so we can observe our own handle's cancellation.
    state_tracker_.AddObserver(this, nullptr);
}

bool WaitSetDispatcher::OnInitialize(mx_signals_t initial_state,
                                     const StateObserver::CountInfo* cinfo) { return false; }

bool WaitSetDispatcher::OnStateChange(mx_signals_t new_state) { return false; }

bool WaitSetDispatcher::OnCancel(Handle* handle) {
    canary_.Assert();

    AutoLock lock(&mutex_);
    cancelled_ = true;
    return event_signal(&event_, false) > 0;
}
