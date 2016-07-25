// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>

#include <utils/intrusive_double_list.h>

class Handle;

// Observer interface for state maintained by StateTracker.
class StateObserver : public utils::DoublyLinkedListable<StateObserver*> {
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
    // also be destroyed shortly afterwards.) Returns true if a thread was awoken.
    // WARNING: This is called under StateTracker's mutex.
    virtual bool OnCancel(Handle* handle) = 0;

protected:
    ~StateObserver() {}
};
