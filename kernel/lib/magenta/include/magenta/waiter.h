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
#include <magenta/io_port_dispatcher.h>

#include <utils/intrusive_single_list.h>

class Handle;

// Magenta Waiter
//
//  Provides the interface between the syscall layer and the kernel object layer
//  that allows waiting for object state changes. It connects the waitee (which
//  owns the Waiter object) and (possibly) many waiters.
//
//  The waitee uses Signal/ClearSignal to inform the waiters of state changes.
//
//  The Waiter has two styles for notifying waiters. They are mutually exclusive.
//
//  In the examples that follow, assume a waitee pointed by |handle| and
//  some |signals| to wait for.
//
//  Style 1: Using BeginWait / FinishWait. Assume an existing |event|
//
//      auto waiter = handle->dispatcher()->get_waiter();
//      waiter->BeginWait(&event, handle, signals);
//
//      event_wait(&event);
//      waiter->FinishWait(&event);
//
//  Style 2: Using IOPorts. Assume an existing |io_port|.
//
//      auto waiter = handle->dispatcher()->get_waiter();
//      waiter->BindIOPOrt(io_port, key, signals);
//
//      IOP_Packet pk;
//      io_port->Wait(&pk);
//

class Waiter {
public:
    Waiter();

    Waiter(const Waiter& o) = delete;
    Waiter& operator=(const Waiter& o) = delete;

    // Start an event-based wait.
    mx_status_t BeginWait(event_t* event, Handle* handle, mx_signals_t signals);

    // End an event-based wait.
    mx_signals_t FinishWait(event_t* event);

    // Register IO Port for state changes.
    bool BindIOPort(utils::RefPtr<IOPortDispatcher> io_port, intptr_t key, mx_signals_t signals);

    // Cancel a pending wait started with BeginWait.
    bool CancelWait(Handle* handle);

    // The object internally calls this method when its state changes.
    // If there is a matching wait, the associated event will be signaled.
    bool Signal(mx_signals_t signals) { return Modify(signals, 0u, true); }

    // Call to clear the signal, but not unsignal any events.
    void ClearSignal(mx_signals_t signals);

    // Notify others (possibly waking them) that signals have changed.
    bool Modify(mx_signals_t set_mask, mx_signals_t clear_mask, bool yield);

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

    bool SendIOPortPacket_NoLock(IOPortDispatcher* io_port, mx_signals_t signals);

    spin_lock_t lock_;
    mx_signals_t signals_;
    utils::SinglyLinkedList<WaitNode> nodes_;
    utils::RefPtr<IOPortDispatcher> io_port_;
    mx_signals_t io_port_signals_;
    intptr_t io_port_key_;
};
