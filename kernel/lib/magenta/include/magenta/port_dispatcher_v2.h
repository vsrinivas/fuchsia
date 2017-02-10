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
//  +----------+                    o       +--------+
//  | object   |               +------------+  Port  |
//  |          |               |            |        |
//  +----------+        +------v----+       +-+---+--+
//  | state    |  w1    | Port      |         ^
//  | tracker  +------> | Observer  |         |
//  +----------+        |           |    w2   |
//                      |           +---------+
//                      +-----------+
//                      |  Port     |
//                      |  Packet   |
//                      +-----------+
//
//   State changes of the object are propagated from the object
//   to the port via w1 --> observer --> w2 calls.
//
// 2) Situation after the packet is queued for non-repeating case:
//
//  +----------+                            +--------+
//  | object   |                            |  Port  |
//  |          |                            |        |
//  +----------+        +-----------+       +-+---+--+
//  | state    |  w1    | Port      |             |
//  | tracker  +------> | Observer  |             |
//  +----------+        |           |             |
//                +---> |           |             |
//                |     +-----------+             |
//             o  |     |  Port     |      w3     |
//                +-----|  Packet   | <-----------+
//                      +-----------+
//
//   Note that the Port no longer owns the observer directly. the port
//   has a pointer w3 to the queued packet and the packet now owns the
//   observer.
//
// 3) Situation after the port packet is retrieved, but before
//    it reaches userspace:
//
//                rc
//       +-----------------+
//       |                 |
//  +----v-----+           |
//  | object   |           |
//  |          |           |
//  +----------+        +--+--------+
//  | state    |        | Port      |
//  | tracker  |        | Observer  |
//  +----------+        |           |
//                +---> |           |
//                |     +-----------+
//             o  |     |  Port     |
//                +-----|  Packet   |
//                      +-----------+
//
//   The rc pointer is used to remove the Port observer from the
//   state tracker (w1), then the Port packet can delete the Port
//   observer and thusly delete itself.
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

class PortObserver final : public StateObserver {
public:
    // List traits to belong to the Port list.
    struct PortListTraits {
        static mxtl::DoublyLinkedListNodeState<PortObserver*>& node_state(
            PortObserver& obj) {
            return obj.dll_port_;
        }
    };

    PortObserver(PortDispatcherV2* port, uint64_t key, mx_signals_t signals);
    ~PortObserver();

    mx_status_t Begin(Handle* handle);
    void End();

private:
    PortObserver(const PortObserver&) = delete;
    PortObserver& operator=(const PortObserver&) = delete;

    // StateObserver overrides.
    bool OnInitialize(mx_signals_t initial_state) final;
    bool OnStateChange(mx_signals_t new_state) final;
    bool OnCancel(Handle* handle) final;

    bool MaybeQueue(mx_signals_t new_state);

    const uint64_t key_;
    const mx_signals_t trigger_;

    PortPacket packet_;
    PortDispatcherV2* port_;
    Handle* handle_;

    mxtl::RefPtr<Dispatcher> dispatcher_;

    mxtl::DoublyLinkedListNodeState<PortObserver*> dll_port_;
};

class PortDispatcherV2 final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~PortDispatcherV2() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_IOPORT2; }

    void on_zero_handles() final;

    mx_status_t Queue(PortPacket* packet);
    mx_status_t DeQueue(mx_time_t timeout, PortPacket** packet);

    PortObserver* MakeObserver(uint64_t key, mx_signals_t signals);
    void CancelObserver(PortObserver* observer);

private:
    PortDispatcherV2(uint32_t options);

    WaitEvent event_;

    Mutex lock_;
    mxtl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(lock_);
    mxtl::DoublyLinkedList<PortObserver*,
        PortObserver::PortListTraits> observers_ TA_GUARDED(lock_);
};
