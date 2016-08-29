// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>

#include <mxtl/intrusive_double_list.h>

class Handle;

// Observer base class for state maintained by StateTracker.
class StateObserver {
public:
    StateObserver() {}

    // Called when this object is added to a StateTracker, to give it the initial state. Returns
    // true if a thread was awoken.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnInitialize(mx_signals_state_t initial_state) = 0;

    // Called whenever the state changes, to give it the new state. Returns true if a thread was
    // awoken.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnStateChange(mx_signals_state_t new_state) = 0;

    // Called when |handle| (which refers to a handle to the object that owns the StateTracker) is
    // being destroyed/"closed"/transferred. (The object itself, and thus the StateTracker too, may
    // also be destroyed shortly afterwards.) Returns true if a thread was awoken. If the callee
    // wants to be removed from the calling StateTracker, it should set |*should_remove| to true
    // (by default, |*should_remove| is false), in which case RemoveObserver() should not be called
    // for the callee observer.
    // In addition to |should_remove|, the callee can set |*call_did_cancel| to true if it
    // wants to be notified after it has been removed from the state tracker list. See
    // OnDidCancel() below.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) = 0;

    // Called when the StateTracker has removed all internal references to this observer. This
    // happens after OnCancel() is called if |call_did_cancel| is set to true. This not called
    // under the StateTracker's mutex.
    virtual void OnDidCancel() = 0;

protected:
    ~StateObserver() {}

private:
    friend struct StateObserverListTraits;
    mxtl::DoublyLinkedListNodeState<StateObserver*> state_observer_list_node_state_;
};

// For use by StateTracker to maintain a list of StateObservers. (We don't use the default traits so
// that implementations of StateObserver can themselves use the default traits if they need to be on
// a different list.)
struct StateObserverListTraits {
    inline static mxtl::DoublyLinkedListNodeState<StateObserver*>& node_state(
            StateObserver& obj) {
        return obj.state_observer_list_node_state_;
    }
};
