// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>

class Handle;

// Observer base class for state maintained by StateTracker.
class StateObserver {
public:
    // Optional initial counts. Each object might have a different idea of them
    // and currently we assume at most two. The state observers will iterate on
    // the entries and might fire if |signal| matches one of their trigger signals
    // so each entry should be associated with a unique signal or with 0 if not
    // applicable.
    struct CountInfo {
        struct {
            uint64_t count;
            mx_signals_t signal;
        } entry[2];
    };

    StateObserver() { }

    typedef unsigned Flags;

    // Bitmask of return values for On...() methods
    static constexpr Flags kWokeThreads = 1;
    static constexpr Flags kNeedRemoval = 2;
    static constexpr Flags kHandled = 4;

    // Called when this object is added to a StateTracker, to give it the initial state.
    // Note that |cinfo| might be null.
    // May return flags: kWokeThreads, kNeedRemoval
    // WARNING: This is called under StateTracker's mutex.
    virtual Flags OnInitialize(mx_signals_t initial_state, const CountInfo* cinfo) = 0;

    // Called whenever the state changes, to give it the new state.
    // May return flags: kWokeThreads, kNeedRemoval
    // WARNING: This is called under StateTracker's mutex
    virtual Flags OnStateChange(mx_signals_t new_state) = 0;

    // Called when |handle| (which refers to a handle to the object that owns the StateTracker) is
    // being destroyed/"closed"/transferred. (The object itself, and thus the StateTracker too, may
    // also be destroyed shortly afterwards.)
    // Returns flag kHandled if |this| observer handled the call which normally
    // means it was bound to |handle|.
    // May also return flags: kNeedRemoval, kWokeThreads
    // WARNING: This is called under StateTracker's mutex.
    virtual Flags OnCancel(Handle* handle) = 0;

    // Called when the client wants to cancel an outstanding object_wait_aysnc(..key..). In this
    // case the object might not be destroyed.
    // Returns flag kHandled if |this| observer handled the call which normally
    // means it was bound to |handle| and |key|.
    // May also return flags: kNeedRemoval, kWokeThreads
    // WARNING: This is called under StateTracker's mutex.
    virtual Flags OnCancelByKey(Handle* handle, const void* port, uint64_t key) { return 0; }

    // Called after this observer has been removed from the state tracker list. In this callback
    // is safe to delete the observer.
    virtual void OnRemoved() {}

protected:
    ~StateObserver() {}

private:
    fbl::Canary<fbl::magic("SOBS")> canary_;

    friend struct StateObserverListTraits;
    fbl::DoublyLinkedListNodeState<StateObserver*> state_observer_list_node_state_;
};

// For use by StateTracker to maintain a list of StateObservers. (We don't use the default traits so
// that implementations of StateObserver can themselves use the default traits if they need to be on
// a different list.)
struct StateObserverListTraits {
    inline static fbl::DoublyLinkedListNodeState<StateObserver*>& node_state(
            StateObserver& obj) {
        return obj.state_observer_list_node_state_;
    }
};
