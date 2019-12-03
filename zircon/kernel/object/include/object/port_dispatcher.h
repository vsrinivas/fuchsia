// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PORT_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PORT_DISPATCHER_H_

#include <sys/types.h>
#include <zircon/rights.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#include <ktl/unique_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/state_observer.h>

// Important pointers diagram for PortObserver
//
// The diagrams below show the *relevant* pointers on different
// states of the system. The pure header view is really the
// union of all these pointer which can be confusing.
//
// PortDispatcher is responsible for destroying PortObservers (MaybeReap
// or on_zero_handles), however, their destruction may be initiated by
// either Dispatcher or PortDispatcher.
//
// rc = ref counted
// p  = raw pointer
// o =  owning pointer
//
// 1) Situation after object_wait_async(port, handle) is issued:
//
//
//                                   list   +--------+
//          +------p------+      +----p-----+  Port  |
//          |             v      v          |        |
//  +-------+--+        +-----------+       +-+------+
//  | object   |        | Port      |         ^
//  |          | <--rc--+ Observer  |         |
//  +----------+        |           +---rc----+
//                      |           |
//                      +-----------+
//                      |  Port     |
//                      |  Packet   |
//                      +-----------+
//
//   State changes of the object are propagated from the object
//   to the port via |p| --> observer --> |rc| calls.
//
// 2) Situation after the packet is queued on signal match or the wait
//    is canceled.
//
//                                          +--------+
//                                          |  Port  |
//                                          |        |
//  +----------+        +-----------+       +-+---+--+
//  | object   |        | Port      |         ^   |
//  |          |        | Observer  |         |   |
//  +----------+        |           +---rc----+   |
//                +---> |           |             |
//                |     +-----------+             | list
//                |     |  Port     |             |
//                +-rc--|  Packet   | <-----o-----+
//                      +-----------+
//
//   Note that the object no longer has a |p| to the observer
//   but the observer still owns the port via |rc|.
//
//   The |o| pointer is used to destroy the port observer only
//   when cancellation happens and the port still owns the packet.
//

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
  ktl::unique_ptr<const PortObserver> observer;
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

// Observers are weakly contained in Dispatchers until their OnInitialize(), OnStateChange() or
// OnCancel() callbacks return StateObserver::kNeedRemoval.
class PortObserver final : public StateObserver {
 public:
  using ListNodeState = fbl::DoublyLinkedListNodeState<PortObserver*>;

  // ListTraits allows PortObservers to be placed on a PortObserver::List.
  struct ListTraits {
    static ListNodeState& node_state(PortObserver& obj) { return obj.observer_list_node_state_; }
  };

  using List = fbl::DoublyLinkedList<PortObserver*, PortObserver::ListTraits>;

  PortObserver(uint32_t options, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
               Lock<fbl::Mutex>* port_lock, uint64_t key, zx_signals_t signals);

  ~PortObserver() = default;

  // May only be called while holding PortDispatcher lock.
  fbl::RefPtr<Dispatcher> UnlinkDispatcherLocked() {
    DEBUG_ASSERT(port_lock_->lock().IsHeld());
    return ktl::move(dispatcher_);
  }

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

  const uint32_t options_;
  const zx_signals_t trigger_;
  PortPacket packet_;

  fbl::RefPtr<PortDispatcher> const port_;
  Lock<fbl::Mutex>* const port_lock_;

  // Guarded by port_lock_;
  ListNodeState observer_list_node_state_;

  // Guarded by port_lock_;
  fbl::RefPtr<Dispatcher> dispatcher_;
};

// The PortDispatcher implements the port kernel object which is the cornerstone
// for waiting on object changes in Zircon. The PortDispatcher handles 3 usage
// cases:
//  1- Object state change notification: zx_object_wait_async()
//  2- Manual queuing: zx_port_queue()
//  3- Interrupt change notification: zx_interrupt_bind()
//
// This makes the implementation non-trivial. Cases 1 and 2 use the |packets_|
// linked list and case 3 uses |interrupt_packets_| linked list.
//
// The threads that wish to receive notifications block on Dequeue() (which
// maps to zx_port_wait()) and will receive packets from any of the four sources
// depending on what kind of object the port has been 'bound' to.
//
// When a packet from any of the sources arrives to the port, one waiting
// thread unblocks and gets the packet. In all cases |sema_| is used to signal
// and manage the waiting threads.

class PortDispatcher final : public SoloDispatcher<PortDispatcher, ZX_DEFAULT_PORT_RIGHTS> {
 public:
  static void Init();
  static PortAllocator* DefaultPortAllocator();
  static zx_status_t Create(uint32_t options, KernelHandle<PortDispatcher>* handle,
                            zx_rights_t* rights);

  ~PortDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PORT; }

  bool can_bind_to_interrupt() const { return options_ & ZX_PORT_BIND_TO_INTERRUPT; }
  void on_zero_handles() final;

  zx_status_t Queue(PortPacket* port_packet, zx_signals_t observed, uint64_t count);
  zx_status_t QueueUser(const zx_port_packet_t& packet);
  bool QueueInterruptPacket(PortInterruptPacket* port_packet, zx_time_t timestamp);
  zx_status_t Dequeue(const Deadline& deadline, zx_port_packet_t* packet);
  bool RemoveInterruptPacket(PortInterruptPacket* port_packet);

  // This method determines the observer's fate. Upon return, one of the following will have
  // occurred:
  //
  // 1. The observer is destroyed.
  //
  // 2. The observer is linked to an alreadyed queued packet and will be destroyed when the packet
  // is destroyed (Queued or CancelQueued).
  //
  // 3. The observer is left for on_zero_handles to destroyed.
  void MaybeReap(PortObserver* observer, PortPacket* port_packet);

  // Called under the handle table lock.
  zx_status_t MakeObserver(uint32_t options, Handle* handle, uint64_t key, zx_signals_t signals);

  // Returns true if at least one packet was removed from the queue.
  // Called under the handle table lock when |handle| is not null.
  // When |handle| is null, ephemeral PortPackets are removed from the queue but not freed.
  bool CancelQueued(const void* handle, uint64_t key);

  // Removes |port_packet| from this port's queue. Returns false if the packet was
  // not in this queue. It is undefined to call this with a packet queued in another port.
  bool CancelQueued(PortPacket* port_packet);

 private:
  explicit PortDispatcher(uint32_t options);

  const uint32_t options_;
  Semaphore sema_;
  bool zero_handles_ TA_GUARDED(get_lock());

  // Next three members handle the object and manual notifications.
  size_t num_ephemeral_packets_ TA_GUARDED(get_lock());
  fbl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(get_lock());
  // Next two members handle the interrupt notifications.
  DECLARE_SPINLOCK(PortDispatcher) spinlock_;
  fbl::DoublyLinkedList<PortInterruptPacket*> interrupt_packets_ TA_GUARDED(spinlock_);

  // Keeps track of outstanding observers so they can be removed from dispatchers once handle
  // count drops to zero.
  PortObserver::List observers_ TA_GUARDED(get_lock());
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PORT_DISPATCHER_H_
