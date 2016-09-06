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
    enum class IrqDisposition {
        IRQ_UNSAFE, // OnStateChange is not safe to call from an IRQ
        IRQ_SAFE,   // OnStateChange is safe to call from an IRQ
    };

    explicit StateObserver(IrqDisposition irq_disposition) : irq_disposition_(irq_disposition) { }

    // Called when this object is added to a StateTracker, to give it the initial state. Returns
    // true if a thread was awoken.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnInitialize(mx_signals_state_t initial_state) = 0;

    // Called whenever the state changes, to give it the new state. Returns true if a thread was
    // awoken.
    // WARNING: This is called under StateTracker's lock and may be called from an IRQ (instead of a
    // thread) if irq_safe() is true.
    virtual bool OnStateChange(mx_signals_state_t new_state);

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

    // True if this StateObserver may be signaled (OnStateChange) safely from an IRQ context.
    // StateObserver implementations which claim to be "irq safe" must take care to only perform
    // operations which are safe to perform from either a thread or an irq context.  In
    // particular...
    //
    // ++ Synchronization should be done using things like atomic operations and spin_locks with
    //    IRQs disabled.  Mutexes, or anything which might attempt to suspend a thread are not
    //    allowed.
    // ++ Triggering a reschedule event (usually via signaling an event_t) is not allowed.
    // ++ Sleeping is not allowed.
    // ++ O(n) operations are not allowed.
    bool irq_safe() const { return irq_disposition_ == IrqDisposition::IRQ_SAFE; }

protected:
    ~StateObserver() {}

private:
    friend struct StateObserverListTraits;
    mxtl::DoublyLinkedListNodeState<StateObserver*> state_observer_list_node_state_;
    const IrqDisposition irq_disposition_;
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
