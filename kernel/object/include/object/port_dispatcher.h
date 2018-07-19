// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/dispatcher.h>
#include <object/semaphore.h>
#include <object/state_observer.h>

#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <kernel/spinlock.h>

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

struct PortAllocator {
    virtual ~PortAllocator() = default;

    virtual PortPacket* Alloc() = 0;
    virtual void Free(PortPacket* port_packet) = 0;
};

struct PortPacket final : public fbl::DoublyLinkedListable<PortPacket*> {
    zx_port_packet_t packet;
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

struct PortInterruptPacket final : public fbl::DoublyLinkedListable<PortInterruptPacket*> {
    zx_time_t timestamp;
    uint64_t key;
};

// Observers are weakly contained in state trackers until |remove_| member
// is false at the end of one of OnInitialize(), OnStateChange() or OnCancel()
// callbacks.
class PortObserver final : public StateObserver {
public:
    PortObserver(uint32_t type, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
                 uint64_t key, zx_signals_t signals);
    ~PortObserver() = default;

private:
    PortObserver(const PortObserver&) = delete;
    PortObserver& operator=(const PortObserver&) = delete;

    // StateObserver overrides.
    Flags OnInitialize(zx_signals_t initial_state, const StateObserver::CountInfo* cinfo) final;
    Flags OnStateChange(zx_signals_t new_state) final;
    Flags OnCancel(const Handle* handle) final;
    Flags OnCancelByKey(const Handle* handle, const void* port, uint64_t key) final;
    void OnRemoved() final;

    // The following method can only be called from
    // OnInitialize(), OnStateChange() and OnCancel().
    Flags MaybeQueue(zx_signals_t new_state, uint64_t count);

    const uint32_t type_;
    const zx_signals_t trigger_;
    PortPacket packet_;

    fbl::RefPtr<PortDispatcher> const port_;
};

// The PortDispatcher implements the port kernel object which is the cornerstone
// for waiting on object changes in Zircon. The PortDispatcher handles 4 usage
// cases:
//  1- Exception notification: task_bind_exception_port()
//  2- Object state change notification: zx_object_wait_async()
//      a) single-shot mode
//      b) repeating mode
//  3- Manual queuing: zx_port_queue()
//  4- Interrupt change notification: zx_interrupt_bind()
//
// This makes the implementation non-trivial. Cases 1, 2 and 3 uses the
// |packets_| linked list and case 4 uses |interrupt_packets_| linked list.
//
// The threads that wish to receive notifications block on Dequeue() (which
// maps to zx_port_wait()) and will receive packets from any of the four sources
// depending on what kind of object the port has been 'bound' to.
//
// When a packet from any of the sources arrives to the port, one waiting
// thread unblocks and gets the packet. In all cases |sema_| is used to signal
// and manage the waiting threads.

class PortDispatcher final : public SoloDispatcher<PortDispatcher> {
public:
    static void Init();
    static PortAllocator* DefaultPortAllocator();
    static zx_status_t Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~PortDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PORT; }

    bool can_bind_to_interrupt() const { return options_ & PORT_BIND_TO_INTERRUPT; }
    void on_zero_handles() final;

    zx_status_t Queue(PortPacket* port_packet, zx_signals_t observed, uint64_t count);
    zx_status_t QueueUser(const zx_port_packet_t& packet);
    bool QueueInterruptPacket(PortInterruptPacket* port_packet, zx_time_t timestamp);
    zx_status_t Dequeue(zx_time_t deadline, zx_port_packet_t* packet);
    bool RemoveInterruptPacket(PortInterruptPacket* port_packet);

    // Decides who is going to destroy the observer. If it returns |true| it
    // is the duty of the caller. If it is false it is the duty of the port.
    bool CanReap(PortObserver* observer, PortPacket* port_packet);

    // Called under the handle table lock.
    zx_status_t MakeObserver(uint32_t options, Handle* handle, uint64_t key, zx_signals_t signals);

    // Called under the handle table lock. Returns true if at least one packet was
    // removed from the queue.
    bool CancelQueued(const void* handle, uint64_t key);

    // Bits for options passed to port_create
    static constexpr uint32_t PORT_BIND_TO_INTERRUPT = (1u << 0);

private:
    friend class ExceptionPort;

    explicit PortDispatcher(uint32_t options);

    void FreePacket(PortPacket* port_packet) TA_REQ(get_lock());

    // Adopts a RefPtr to |eport|, and adds it to |eports_|.
    // Called by ExceptionPort.
    void LinkExceptionPort(ExceptionPort* eport);

    // Removes |eport| from |eports_|, dropping its RefPtr.
    // Does nothing if |eport| is not on the list.
    // Called by ExceptionPort.
    void UnlinkExceptionPort(ExceptionPort* eport);

    fbl::Canary<fbl::magic("PORT")> canary_;
    const uint32_t options_;
    Semaphore sema_;
    bool zero_handles_ TA_GUARDED(get_lock());

    // Next three members handle the object, manual and exception notifications.
    size_t num_packets_ TA_GUARDED(get_lock());
    fbl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(get_lock());
    fbl::DoublyLinkedList<fbl::RefPtr<ExceptionPort>> eports_ TA_GUARDED(get_lock());
    // Next two members handle the interrupt notifications.
    DECLARE_SPINLOCK(PortDispatcher) spinlock_;
    fbl::DoublyLinkedList<PortInterruptPacket*> interrupt_packets_ TA_GUARDED(spinlock_);
};
