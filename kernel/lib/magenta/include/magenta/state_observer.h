// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>

#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>

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

    explicit StateObserver() : remove_(false) { }

    // Called when this object is added to a StateTracker, to give it the initial state.
    // Note that |cinfo| might be null. Returns true if a thread was awoken.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnInitialize(mx_signals_t initial_state, const CountInfo* cinfo) = 0;

    // Called whenever the state changes, to give it the new state. Returns true if a thread was
    // awoken.
    // WARNING: This is called under StateTracker's mutex
    virtual bool OnStateChange(mx_signals_t new_state) = 0;

    // Called when |handle| (which refers to a handle to the object that owns the StateTracker) is
    // being destroyed/"closed"/transferred. (The object itself, and thus the StateTracker too, may
    // also be destroyed shortly afterwards.) Returns true if |this| observer handled the call
    // which normally means it was bound to |handle|.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnCancel(Handle* handle) = 0;

    // Called when the client wants to cancel an outstanding object_wait_aysnc(..key..). In this
    // case the object might not be destroyed. Returns true if |this| observer handled the call
    // which normally means it was bound to |handle| and |key|.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnCancelByKey(Handle* handle, const void* port, uint64_t key) { return false; }

    // Called after this observer has been removed from the state tracker list. In this callback
    // is safe to delete the observer.
    virtual void OnRemoved() {}

    // Return true to have the observer removed from the state_observer after calling
    // OnInitialize(), OnStateChange(), OnCancel(), or OnCancelByKey().
    bool remove() const { return remove_; }

protected:
    ~StateObserver() {}
    // Warning: |remove_| should only be mutated during the OnXXX callbacks.
    bool remove_ = false;

private:
    mxtl::Canary<mxtl::magic("SOBS")> canary_;

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
