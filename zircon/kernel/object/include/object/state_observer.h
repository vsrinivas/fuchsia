// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>

class Handle;

// Observer base class for state maintained by Dispatcher.
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
            zx_signals_t signal;
        } entry[2];
    };

    StateObserver() = default;

    typedef unsigned Flags;

    // Bitmask of return values for On...() methods
    static constexpr Flags kNeedRemoval = 1;
    static constexpr Flags kHandled = 2;

    // Called when this object is added to a Dispatcher, to give it the initial state.
    // Note that |cinfo| might be null.
    // May return flags: kNeedRemoval
    // WARNING: This is called under Dispatcher's mutex.
    virtual Flags OnInitialize(zx_signals_t initial_state, const CountInfo* cinfo) = 0;

    // Called whenever the state changes, to give it the new state.
    // May return flags: kNeedRemoval
    // WARNING: This is called under Dispatcher's mutex
    virtual Flags OnStateChange(zx_signals_t new_state) = 0;

    // Called when |handle| (which refers to a handle to the Dispatcher object) is being
    // destroyed/"closed"/transferred. (The object itself, may also be destroyed shortly
    // afterwards.)
    // Returns flag kHandled if |this| observer handled the call which normally means it
    // was bound to |handle|.
    // May also return flags: kNeedRemoval
    // WARNING: This is called under Dispatcher's mutex.
    virtual Flags OnCancel(const Handle* handle) = 0;

    // Called when the client wants to cancel an outstanding object_wait_aysnc(..key..). In this
    // case the object might not be destroyed.
    // Returns flag kHandled if |this| observer handled the call which normally
    // means it was bound to |handle| and |key|.
    // May also return flags: kNeedRemoval
    // WARNING: This is called under Dispatcher's mutex.
    virtual Flags OnCancelByKey(const Handle* handle, const void* port, uint64_t key) { return 0; }

    // Called after this observer has been removed from the Dispatcher. In this callback
    // is safe to delete the observer.
    // WARNING: This is called under Dispatcher's mutex.
    virtual void OnRemoved() {}

    struct ObserverListTraits {
        static fbl::DoublyLinkedListNodeState<StateObserver*>& node_state(StateObserver& obj) {
            return obj.observer_list_node_state_;
        }
    };

protected:
    ~StateObserver() = default;

private:
    fbl::Canary<fbl::magic("SOBS")> canary_;

    // Guarded by Dispatcher's lock.
    fbl::DoublyLinkedListNodeState<StateObserver*> observer_list_node_state_;
};
