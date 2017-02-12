// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_observer.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <magenta/wait_event.h>

#include <sys/types.h>

#include <mxtl/intrusive_double_list.h>
#include <mxtl/unique_ptr.h>

// Important pointers diagram for PortObserver
//
// The diagrams below show the *relevant* pointers on diferent
// states of the system. The pure header view is really the
// union of all these pointer which can be confusing.
//
// rc = ref counted
// w  = weak pointer
// o =  owning pointer
//
// 1) Situation after handle_wait_async(port, handle) is issued:
//
//
//  +----------+                            +--------+
//  | object   |                            |  Port  |
//  |          |                            |        |
//  +----------+        +-----------+       +-+------+
//  | state    |  w     | Port      |         ^
//  | tracker  +------> | Observer  |         |
//  +----------+        |           |    rc   |
//                      |           +---------+
//                      +-----------+
//                      |  Port     |
//                      |  Packet   |
//                      +-----------+
//
//   State changes of the object are propagated from the object
//   to the port via |w| --> observer --> |rc| calls.
//
// 2) Situation after the packet is queued in the one-shot case on
//    signal match or the wait is cancelled.
//
//  +----------+                            +--------+
//  | object   |                            |  Port  |
//  |          |                            |        |
//  +----------+        +-----------+       +-+---+--+
//  | state    |        | Port      |         ^   |
//  | tracker  |        | Observer  |         |   |
//  +----------+        |           |    rc   |   |
//                +---> |           +---------+   |
//                |     +-----------+             | list
//             o1 |     |  Port     |      o2     |
//                +-----|  Packet   | <-----------+
//                      +-----------+
//
//   Note that the objet no longer has a |w| to the observer
//   but the observer still owns the port via |rc|.
//
//   For repeating ports |w| is always valid until the wait is
//   cancelled.
//
//   The cycle |o2| --> |o1| --> |rc|  is broken when:
//   1- the packet is dequeued.
//   2- the port gets on_zero_handles().
//

class PortDispatcherV2;
class PortObserver;

struct PortPacket final : public mxtl::DoublyLinkedListable<PortPacket*> {
    PortObserver* observer;
    mx_port_packet_t packet;

    PortPacket() = delete;
    explicit PortPacket(PortObserver* obs);
    void Destroy();
};

// Observers are weakly contained in state trackers until |remove_| member
// is false at the end of one of the OnXXXXX callbacks. If the members
// are only mutated after Begin() in the callbacks then there is no need to
// have a lock in this class since the state tracker holds its lock during
// all callback calls.
class PortObserver final : public StateObserver {
public:
    PortObserver(uint32_t type, mxtl::RefPtr<PortDispatcherV2> port,
        uint64_t key, mx_signals_t signals);
    ~PortObserver() = default;

    void Begin(Handle* handle);
    void End();

private:
    PortObserver(const PortObserver&) = delete;
    PortObserver& operator=(const PortObserver&) = delete;

    // StateObserver overrides.
    bool OnInitialize(mx_signals_t initial_state) final;
    bool OnStateChange(mx_signals_t new_state) final;
    bool OnCancel(Handle* handle) final;

    // The following two methods can only be called from
    // the above OnXXXXX callbacks/
    void MaybeQueue(mx_signals_t new_state);
    void ReapSelf();

    // Called outside the state tracker's lock. That is why
    // we need to use atomics.
    void SnapCount();

    const uint32_t type_;
    const uint64_t key_;
    const mx_signals_t trigger_;

    PortPacket packet_;
    mxtl::RefPtr<PortDispatcherV2> port_;
    Handle* handle_;
};

class PortDispatcherV2 final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~PortDispatcherV2() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_IOPORT2; }

    void on_zero_handles() final;

    mx_status_t Queue(mxtl::unique_ptr<PortPacket> packet);
    mx_status_t Queue(PortPacket* packet);
    mx_status_t DeQueue(mx_time_t timeout, PortPacket** packet);

    PortObserver* MakeObserver(uint32_t options, uint64_t key, mx_signals_t signals);

private:
    PortDispatcherV2(uint32_t options);

    Mutex lock_;
    WaitEvent event_;
    bool zero_handles_ TA_GUARDED(lock_);
    mxtl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(lock_);
};
