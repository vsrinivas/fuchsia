// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <magenta/state_observer.h>
#include <magenta/types.h>
#include <mxtl/intrusive_double_list.h>

class Handle;

class StateTracker {
public:
    StateTracker(mx_signals_t signals = 0u) : signals_(signals) { }

    StateTracker(const StateTracker& o) = delete;
    StateTracker& operator=(const StateTracker& o) = delete;

    // Set the initial signals state. This is an alternative to provide the initial signals state to
    // the constructor. This does no locking and does not notify anything.
    void set_initial_signals_state(mx_signals_t signals) {
        signals_ = signals;
    }

    // Add an observer.
    mx_status_t AddObserver(StateObserver* observer);

    // Remove an observer (which must have been added).
    void RemoveObserver(StateObserver* observer);

    // Called when observers of the handle's state (e.g., waits on the handle) should be
    // "cancelled", i.e., when a handle (for the object that owns this StateTracker) is being
    // destroyed or transferred.
    void Cancel(Handle* handle);

    // Notify others of a change in state (possibly waking them). (Clearing satisfied signals or
    // setting satisfiable signals should not wake anyone.)
    void UpdateState(mx_signals_t clear_mask, mx_signals_t set_mask);

    mx_signals_t GetSignalsState() { return signals_; }

private:
    mx_signals_t signals_;
    Mutex lock_;

    // Active observers are elements in |observers_|.
    mxtl::DoublyLinkedList<StateObserver*, StateObserverListTraits> observers_;
};
