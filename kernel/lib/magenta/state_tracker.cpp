// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/state_tracker.h>

#include <kernel/auto_lock.h>
#include <magenta/wait_event.h>

namespace internal {

// "storage" for constexpr members of trait classes.
constexpr bool NonIrqStateTrackerTraits::SignalableFromIrq;
constexpr bool IrqStateTrackerTraits::SignalableFromIrq;

// Forced instantiation of the two types of state trackers.
template class StateTrackerImpl<NonIrqStateTrackerTraits>;
template class StateTrackerImpl<IrqStateTrackerTraits>;

template <typename Traits>
mx_status_t StateTrackerImpl<Traits>::AddObserver(StateObserver* observer) {
    DEBUG_ASSERT(observer != nullptr);

    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        // State trackers which can be signaled from IRQ context currenty have
        // some restrictions which must be enforced.
        //
        // 1) StateObservers of these StateTrackers must be "irq safe", meaning
        //    that they are guaranteed to perform no operations during their
        //    OnStateChange implementation which would be illegal to perform
        //    in an IRQ context.
        // 2) StateTrackers which can be signaled from IRQ context are only
        //    permitted to have one observer (at most) at a time.  This is to
        //    prevent the posibility of needing to perform an unbound number of
        //    wakeup operations when the IRQ signals the state tracker.
        if (Traits::SignalableFromIrq) {
            if (!observer->irq_safe())
                return ERR_INVALID_ARGS;

            if (!observers_.is_empty())
                return ERR_BAD_STATE;
        }

        observers_.push_front(observer);
        awoke_threads = observer->OnInitialize(signals_state_);
    }
    if (awoke_threads)
        thread_preempt(false);
    return NO_ERROR;
}

template <typename Traits>
mx_signals_state_t StateTrackerImpl<Traits>::RemoveObserver(StateObserver* observer) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
    return signals_state_;
}

template <typename Traits>
void StateTrackerImpl<Traits>::Cancel(Handle* handle) {
    bool awoke_threads = false;
    StateObserver* observer = nullptr;

    mxtl::DoublyLinkedList<StateObserver*, StateObserverListTraits> did_cancel_list;

    {
        AutoLock lock(&lock_);
        for (auto it = observers_.begin(); it != observers_.end();) {
            bool should_remove = false;
            bool call_did_cancel = false;
            awoke_threads = it->OnCancel(handle, &should_remove, &call_did_cancel) || awoke_threads;
            if (should_remove) {
                auto to_remove = it;
                ++it;
                observer = observers_.erase(to_remove);
                if (call_did_cancel)
                    did_cancel_list.push_front(observer);
            } else {
                ++it;
            }
        }
    }

    while (!did_cancel_list.is_empty()) {
        auto observer = did_cancel_list.pop_front();
        if (observer)
            observer->OnDidCancel();
    }

    if (awoke_threads)
        thread_preempt(false);
}

template <typename Traits>
void StateTrackerImpl<Traits>::UpdateState(mx_signals_t satisfied_clear_mask,
                                           mx_signals_t satisfied_set_mask,
                                           mx_signals_t satisfiable_clear_mask,
                                           mx_signals_t satisfiable_set_mask) {
    if (UpdateStateInternal(satisfied_clear_mask,
                            satisfied_set_mask,
                            satisfiable_clear_mask,
                            satisfiable_set_mask)) {
        thread_preempt(false);
    }
}

template <typename Traits>
mx_signals_state_t StateTrackerImpl<Traits>::GetSignalsState() {
    AutoLock lock(&lock_);
    return signals_state_;
}

template <typename Traits>
bool StateTrackerImpl<Traits>::UpdateStateInternal(mx_signals_t satisfied_clear_mask,
                                                   mx_signals_t satisfied_set_mask,
                                                   mx_signals_t satisfiable_clear_mask,
                                                   mx_signals_t satisfiable_set_mask) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        auto previous_signals_state = signals_state_;
        signals_state_.satisfied &= ~satisfied_clear_mask;
        signals_state_.satisfied |= satisfied_set_mask;
        signals_state_.satisfiable &= ~satisfiable_clear_mask;
        signals_state_.satisfiable |= satisfiable_set_mask;

        if (previous_signals_state.satisfied == signals_state_.satisfied &&
            previous_signals_state.satisfiable == signals_state_.satisfiable)
            return false;

        for (auto& observer : observers_) {
            awoke_threads = observer.OnStateChange(signals_state_) || awoke_threads;
        }

    }

    return awoke_threads;
}

}  // namespace internal
