// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/event.h>
#include <kernel/spinlock.h>

#include <magenta/types.h>
#include <utils/intrusive_single_list.h>

class Handle;

// Magenta Waiter
//
//  Provides the interface between the syscall layer and the
//  kernel object layer that allows waiting for object state changes.
//

class Waiter {
public:
    Waiter();

    Waiter(const Waiter& o) = delete;
    Waiter& operator=(const Waiter& o) = delete;

    // The syscall layer uses these 2 functions when doing a wait.
    // assume an array of handles to wait and an array of signals:
    //
    //  event_t event;
    //  Waiter* warr = new Waiter*[n_waits];
    //
    //  for (size_t ix = 0; ix != n_waits; ++ix) {
    //     warr[ix] = handles[ix]->dispatcher()->BeginWait(
    //                    &event, handles[ix], signals[ix]);
    //  }
    //
    //  event_wait(&event);
    //
    //  for (size_t ix = 0; ix != n_waits; ++ix) {
    //      warr[ix]->FinishWait(&event);
    //  }
    //

    Waiter* BeginWait(event_t* event, Handle* handle, mx_signals_t signals);
    mx_signals_t FinishWait(event_t* event);

    // The syscall layer calls this when a handle is closed.
    bool CancelWait(Handle* handle);

    // The object internally calls this method when its state changes.
    // If there is a matching wait, the associated event will be signaled.
    bool Signal(mx_signals_t signals);

    // Call to clear the signal, but not unsignal any events.
    void ClearSignal(mx_signals_t signals);

    void Modify(mx_signals_t set_mask, mx_signals_t clear_mask);

    bool Reset();

private:
    struct WaitNode {
        WaitNode* next;
        event_t* event;
        Handle* handle;
        mx_signals_t signals;

        void list_set_next(WaitNode* node) {
            next = node;
        }
        WaitNode* list_next() {
            return next;
        }
    };

    int SignalComplete_NoLock();

    spin_lock_t lock_;
    mx_signals_t signals_;
    utils::SinglyLinkedList<WaitNode> nodes_;
};
