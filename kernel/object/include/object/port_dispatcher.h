// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/dispatcher.h>
#include <object/semaphore.h>
#include <object/state_observer.h>

#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

#include <sys/types.h>

// Important pointers diagram for PortObserver
//
// The diagrams below show the *relevant* pointers on different
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
//    signal match or the wait is canceled.
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
//   Note that the object no longer has a |w| to the observer
//   but the observer still owns the port via |rc|.
//
//   For repeating ports |w| is always valid until the wait is
//   canceled.
//
//   The |o1| pointer is used to destroy the port observer only
//   when cancellation happens and the port still owns the packet.
//

class ExceptionPort;
class PortDispatcher;
class PortObserver;
struct PortPacket;

#define PKT_FLAG_EPHEMERAL  0x10000000u
#define PKT_FLAG_MASK       0x0FFFFFFFu

struct PortAllocator {
    virtual ~PortAllocator() = default;

    virtual PortPacket* Alloc() = 0;
    virtual void Free(PortPacket* port_packet) = 0;
};

struct PortPacket final : public fbl::DoublyLinkedListable<PortPacket*> {
    mx_port_packet_t packet;
    const void* const handle;
    PortObserver* observer;
    PortAllocator* const allocator;

    PortPacket(const void* handle, PortAllocator* allocator);
    PortPacket(const PortPacket&) = delete;
    void operator=(PortPacket) = delete;

    uint64_t key() const { return packet.key; }
    bool is_ephemeral() const { return allocator != nullptr; }
    void Free() { allocator->Free(this); }
};

// Observers are weakly contained in state trackers until |remove_| member
// is false at the end of one of OnInitialize(), OnStateChange() or OnCancel()
// callbacks.
class PortObserver final : public StateObserver {
public:
    PortObserver(uint32_t type, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
                 uint64_t key, mx_signals_t signals);
    ~PortObserver() = default;

private:
    PortObserver(const PortObserver&) = delete;
    PortObserver& operator=(const PortObserver&) = delete;

    // StateObserver overrides.
    Flags OnInitialize(mx_signals_t initial_state, const StateObserver::CountInfo* cinfo) final;
    Flags OnStateChange(mx_signals_t new_state) final;
    Flags OnCancel(Handle* handle) final;
    Flags OnCancelByKey(Handle* handle, const void* port, uint64_t key) final;
    void OnRemoved() final;

    // The following method can only be called from
    // OnInitialize(), OnStateChange() and OnCancel().
    Flags MaybeQueue(mx_signals_t new_state, uint64_t count);

    const uint32_t type_;
    const mx_signals_t trigger_;
    PortPacket packet_;

    fbl::RefPtr<PortDispatcher> const port_;
};

class PortDispatcher final : public Dispatcher {
public:
    static void Init();
    static PortAllocator* DefaultPortAllocator();
    static mx_status_t Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~PortDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_PORT; }

    void on_zero_handles() final;

    mx_status_t Queue(PortPacket* port_packet, mx_signals_t observed, uint64_t count);
    mx_status_t QueueUser(const mx_port_packet_t& packet);
    mx_status_t Dequeue(mx_time_t deadline, mx_port_packet_t* packet);

    // Decides who is going to destroy the observer. If it returns |true| it
    // is the duty of the caller. If it is false it is the duty of the port.
    bool CanReap(PortObserver* observer, PortPacket* port_packet);

    // Called under the handle table lock.
    mx_status_t MakeObserver(uint32_t options, Handle* handle, uint64_t key, mx_signals_t signals);

    // Called under the handle table lock. Returns true if at least one packet was
    // removed from the queue.
    bool CancelQueued(const void* handle, uint64_t key);

private:
    friend class ExceptionPort;

    explicit PortDispatcher(uint32_t options);

    // Adopts a RefPtr to |eport|, and adds it to |eports_|.
    // Called by ExceptionPort.
    void LinkExceptionPort(ExceptionPort* eport);

    // Removes |eport| from |eports_|, dropping its RefPtr.
    // Does nothing if |eport| is not on the list.
    // Called by ExceptionPort.
    void UnlinkExceptionPort(ExceptionPort* eport);

    fbl::Canary<fbl::magic("PORT")> canary_;
    fbl::Mutex lock_;
    Semaphore sema_;
    bool zero_handles_ TA_GUARDED(lock_);
    fbl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(lock_);
    fbl::DoublyLinkedList<fbl::RefPtr<ExceptionPort>> eports_ TA_GUARDED(lock_);
};
