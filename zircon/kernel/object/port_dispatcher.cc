// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/port_dispatcher.h"

#include <assert.h>
#include <lib/boot-options/boot-options.h>
#include <lib/counters.h>
#include <lib/object_cache.h>
#include <platform.h>
#include <pow2.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/arena.h>
#include <fbl/auto_lock.h>
#include <lk/init.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

// All port sub-packets must be exactly 32 bytes
static_assert(sizeof(zx_packet_user_t) == 32, "incorrect size for zx_packet_signal_t");
static_assert(sizeof(zx_packet_signal_t) == 32, "incorrect size for zx_packet_signal_t");
static_assert(sizeof(zx_packet_guest_bell_t) == 32, "incorrect size for zx_packet_guest_bell_t");
static_assert(sizeof(zx_packet_guest_mem_t) == 32, "incorrect size for zx_packet_guest_mem_t");
static_assert(sizeof(zx_packet_guest_io_t) == 32, "incorrect size for zx_packet_guest_io_t");
static_assert(sizeof(zx_packet_guest_vcpu_t) == 32, "incorrect size for zx_packet_guest_vcpu_t");
static_assert(sizeof(zx_packet_interrupt_t) == 32, "incorrect size for zx_packet_interrupt_t");
static_assert(sizeof(zx_packet_page_request_t) == 32,
              "incorrect size for zx_packet_page_request_t");

KCOUNTER(port_ephemeral_packet_live, "port.ephemeral_packet.live")
KCOUNTER(port_ephemeral_packet_allocated, "port.ephemeral_packet.allocated")
KCOUNTER(port_ephemeral_packet_freed, "port.ephemeral_packet.freed")
KCOUNTER(port_full_count, "port.full.count")
KCOUNTER(port_dequeue_count, "port.dequeue.count")
KCOUNTER(port_dequeue_spurious_count, "port.dequeue.spurious.count")
KCOUNTER(dispatcher_port_create_count, "dispatcher.port.create")
KCOUNTER(dispatcher_port_destroy_count, "dispatcher.port.destroy")

// Implements the PortAllocator interface and trivially forwards to the
// ObjectCache allocator for PortPackets defined below.
struct PortPacketCacheAllocator final : PortAllocator {
  ~PortPacketCacheAllocator() override = default;
  PortPacket* Alloc() override;
  void Free(PortPacket* port_packet) override;
};

namespace {
// BUG(53762) Increase from 16k packets to 32k packets.
// TODO(maniscalco): Enforce this limit per process via the job policy.
constexpr size_t kMaxAllocatedPacketCountPerPort = 4096u;

// Per-cpu cache allocator for PortPackets.
object_cache::ObjectCache<PortPacket, object_cache::Option::PerCpu> packet_allocator;

// Per-cpu cache allocator for PortObservers.
object_cache::ObjectCache<PortObserver, object_cache::Option::PerCpu> observer_allocator;

// A trivial instance of the default PortAllocator for comparisons and to supply
// the vtable used outside of this compilation unit.
PortPacketCacheAllocator default_port_allocator;

bool IsDefaultAllocatedEphemeral(const PortPacket& port_packet) {
  return port_packet.allocator == &default_port_allocator && port_packet.is_ephemeral();
}

void RaisePacketLimitException(zx_koid_t koid, size_t num_packets) {
  auto process = ProcessDispatcher::GetCurrent();
  char pname[ZX_MAX_NAME_LEN];
  process->get_name(pname);
  printf("KERN: port (%zu) has %zu packets (%s). Raising exception\n", koid, num_packets, pname);
  Thread::Current::SignalPolicyException(ZX_EXCP_POLICY_CODE_PORT_TOO_MANY_PACKETS, 0u);
}

}  // namespace.

PortPacket* PortPacketCacheAllocator::Alloc() {
  zx::result result = packet_allocator.Allocate(nullptr, this);
  if (result.is_error()) {
    printf("WARNING: Could not allocate new port packet: %d\n", result.error_value());
    return nullptr;
  }
  kcounter_add(port_ephemeral_packet_live, 1);
  kcounter_add(port_ephemeral_packet_allocated, 1);
  return result.value().release();
}

void PortPacketCacheAllocator::Free(PortPacket* port_packet) {
  kcounter_add(port_ephemeral_packet_live, -1);
  kcounter_add(port_ephemeral_packet_freed, 1);
  object_cache::UniquePtr<PortPacket> destroyer{port_packet};
}

PortPacket::PortPacket(const void* handle, PortAllocator* allocator)
    : packet{}, handle(handle), observer(nullptr), allocator(allocator) {
  // Note that packet is initialized to zeros.
}

PortObserver::PortObserver(uint32_t options, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
                           Lock<CriticalMutex>* port_lock, uint64_t key, zx_signals_t signals)
    : options_(options),
      packet_(handle, nullptr),
      port_(ktl::move(port)),
      port_lock_(port_lock),
      dispatcher_(handle->dispatcher()) {
  DEBUG_ASSERT(port_lock_ != nullptr);
  DEBUG_ASSERT(handle != nullptr);
  DEBUG_ASSERT(dispatcher_ != nullptr);

  auto& packet = packet_.packet;
  packet.status = ZX_OK;
  packet.key = key;
  packet.type = ZX_PKT_TYPE_SIGNAL_ONE;
  packet.signal.trigger = signals;
}

void PortObserver::OnMatch(zx_signals_t signals) {
  if (options_ & ZX_WAIT_ASYNC_TIMESTAMP) {
    // Getting the current time can be somewhat expensive.
    packet_.packet.signal.timestamp = current_time();
  }

  // The packet is not allocated in the packet arena and does not count against the per-port limit
  // so |Queue| cannot fail due to the packet count.  However, the last handle to the port may have
  // been closed so it can still fail with ZX_ERR_BAD_HANDLE.  Just ignore ZX_ERR_BAD_HANDLE because
  // there is nothing to be done.
  const zx_status_t status = port_->Queue(&packet_, signals);
  DEBUG_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_BAD_HANDLE, "status %d\n", status);

  port_->MaybeReap(this, &packet_);
  // The |MaybeReap| call may have deleted |this|, so it is not safe to access any members now.
}

void PortObserver::OnCancel(zx_signals_t signals) {
  port_->MaybeReap(this, &packet_);
  // The |MaybeReap| call may have deleted |this|, so it is not safe to access any members now.
}

bool PortObserver::MatchesKey(const void* port, uint64_t key) {
  return key == packet_.key() && port == port_.get();
}

/////////////////////////////////////////////////////////////////////////////////////////

PortAllocator* PortDispatcher::DefaultPortAllocator() { return &default_port_allocator; }

zx_status_t PortDispatcher::Create(uint32_t options, KernelHandle<PortDispatcher>* handle,
                                   zx_rights_t* rights) {
  if (options && options != ZX_PORT_BIND_TO_INTERRUPT) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) PortDispatcher(options)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

PortDispatcher::PortDispatcher(uint32_t options)
    : options_(options), zero_handles_(false), num_ephemeral_packets_(0u) {
  kcounter_add(dispatcher_port_create_count, 1);
}

PortDispatcher::~PortDispatcher() {
  DEBUG_ASSERT(zero_handles_);
  DEBUG_ASSERT(num_ephemeral_packets_ == 0u);
  kcounter_add(dispatcher_port_destroy_count, 1);
}

void PortDispatcher::on_zero_handles() {
  canary_.Assert();

  Guard<CriticalMutex> guard{get_lock()};
  DEBUG_ASSERT(!zero_handles_);
  zero_handles_ = true;

  // Free any queued packets.
  while (!packets_.is_empty()) {
    auto packet = packets_.pop_front();

    // If the packet is ephemeral, free it outside of the lock. Otherwise,
    // reset the observer if it is present.
    if (IsDefaultAllocatedEphemeral(*packet)) {
      --num_ephemeral_packets_;
      guard.CallUnlocked([packet]() { packet->Free(); });
    } else {
      // The reference to the port that the observer holds cannot be the last one
      // because another reference was used to call on_zero_handles, so we don't
      // need to worry about destroying ourselves.
      packet->observer.reset();
    }
  }

  // For each of our outstanding observers, remove them from their dispatchers and destroy them.
  //
  // We could be racing with the dispatcher calling OnMatch/OnCancel/MaybeReap. Only destroy the
  // observer after RemoveObserver completes to ensure we don't destroy it out from under the
  // dispatcher.
  while (!observers_.is_empty()) {
    PortObserver* observer = observers_.pop_front();
    fbl::RefPtr<Dispatcher> dispatcher = observer->UnlinkDispatcherLocked();
    DEBUG_ASSERT(dispatcher != nullptr);

    // Don't hold the lock while calling RemoveObserver because we don't want to create a
    // PortDispatcher-to-Dispatcher lock dependency.
    guard.CallUnlocked([&dispatcher, &observer]() {
      // We cannot assert that RemoveObserver returns true because it's possible that the
      // Dispatcher removed it before we got here.
      dispatcher->RemoveObserver(observer);
      dispatcher.reset();

      // At this point we know the dispatcher no longer has a reference to the observer.
      object_cache::UniquePtr<PortObserver> destroyer(observer);
    });
  }
}

zx_status_t PortDispatcher::QueueUser(const zx_port_packet_t& packet) {
  canary_.Assert();

  auto port_packet = default_port_allocator.Alloc();
  if (!port_packet)
    return ZX_ERR_NO_MEMORY;

  port_packet->packet = packet;
  port_packet->packet.type = ZX_PKT_TYPE_USER;

  auto status = Queue(port_packet, 0u);
  if (status != ZX_OK)
    port_packet->Free();
  return status;
}

bool PortDispatcher::RemoveInterruptPacket(PortInterruptPacket* port_packet) {
  Guard<SpinLock, IrqSave> guard{&spinlock_};
  if (port_packet->InContainer()) {
    interrupt_packets_.erase(*port_packet);
    return true;
  }
  return false;
}

bool PortDispatcher::QueueInterruptPacket(PortInterruptPacket* port_packet, zx_time_t timestamp) {
  {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    if (port_packet->InContainer()) {
      return false;
    }

    port_packet->timestamp = timestamp;
    interrupt_packets_.push_back(port_packet);
  }

  // |Post| may unblock a waiting thread that will immediately acquire the spinlock. We drop the
  // spinlock before posting to avoid unnecessary spinning.
  sema_.Post();
  return true;
}

zx_status_t PortDispatcher::Queue(PortPacket* port_packet, zx_signals_t observed) {
  canary_.Assert();

  {
    Guard<CriticalMutex> guard{get_lock()};
    if (zero_handles_) {
      return ZX_ERR_BAD_HANDLE;
    }

    if (IsDefaultAllocatedEphemeral(*port_packet) &&
        num_ephemeral_packets_ > kMaxAllocatedPacketCountPerPort) {
      kcounter_add(port_full_count, 1);
      RaisePacketLimitException(get_koid(), num_ephemeral_packets_);
      // The usermode caller sees the exception, not the return code.
      return ZX_ERR_SHOULD_WAIT;
    }

    if (observed) {
      if (port_packet->InContainer()) {
        port_packet->packet.signal.observed |= observed;
        return ZX_OK;
      }
      port_packet->packet.signal.observed = observed;

      // |count| previously stored the number of pending messages on
      // a channel. It is now deprecated, but we set it to 1 for backwards
      // compatibility, so that readers attempt to read at least 1 message and
      // continue to make progress.
      port_packet->packet.signal.count = 1u;
    }
    packets_.push_back(port_packet);
    if (IsDefaultAllocatedEphemeral(*port_packet)) {
      ++num_ephemeral_packets_;
    }
  }

  // If |Post| unblocks a thread, that thread will attempt to acquire the lock. We drop the lock
  // before calling |Post| to allow the unblocked thread to acquire the lock without blocking.
  sema_.Post();
  return ZX_OK;
}

zx_status_t PortDispatcher::Dequeue(const Deadline& deadline, zx_port_packet_t* out_packet) {
  canary_.Assert();

  while (true) {
    // Wait until one of the queues has a packet.
    {
      ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PORT);
      zx_status_t st = sema_.Wait(deadline);
      if (st != ZX_OK)
        return st;
    }

    // Interrupt packets are higher priority so service the interrupt packet queue first.
    if (options_ == ZX_PORT_BIND_TO_INTERRUPT) {
      Guard<SpinLock, IrqSave> guard{&spinlock_};
      PortInterruptPacket* port_interrupt_packet = interrupt_packets_.pop_front();
      if (port_interrupt_packet != nullptr) {
        *out_packet = {};
        out_packet->key = port_interrupt_packet->key;
        out_packet->type = ZX_PKT_TYPE_INTERRUPT;
        out_packet->status = ZX_OK;
        out_packet->interrupt.timestamp = port_interrupt_packet->timestamp;
        break;
      }
    }

    // No interrupt packets queued. Check the regular packets.
    {
      Guard<CriticalMutex> guard{get_lock()};
      PortPacket* port_packet = packets_.pop_front();
      if (port_packet != nullptr) {
        if (IsDefaultAllocatedEphemeral(*port_packet)) {
          --num_ephemeral_packets_;
        }
        *out_packet = port_packet->packet;

        bool is_ephemeral = port_packet->is_ephemeral();
        // The reference to the port that the observer holds cannot be the last one
        // because another reference was used to call Dequeue, so we don't need to
        // worry about destroying ourselves.
        port_packet->observer.reset();
        guard.Release();

        // If the packet is ephemeral, free it outside of the lock. We need to read
        // is_ephemeral inside the lock because it's possible for a non-ephemeral packet
        // to get deleted after a call to |MaybeReap| as soon as we release the lock.
        if (is_ephemeral) {
          port_packet->Free();
        }
        break;
      }
    }

    // Both queues were empty. The packet must have been removed before we were able to
    // dequeue. Loop back and wait again.
    kcounter_add(port_dequeue_spurious_count, 1);
  }

  kcounter_add(port_dequeue_count, 1);
  return ZX_OK;
}

void PortDispatcher::MaybeReap(PortObserver* observer, PortPacket* port_packet) {
  canary_.Assert();

  // These pointers are declared before the guard because we want the destructors to execute
  // outside the critical section below (if they end up being the last/only references).
  object_cache::UniquePtr<PortObserver> destroyer;
  fbl::RefPtr<Dispatcher> dispatcher;

  {
    Guard<CriticalMutex> guard{get_lock()};

    // We may be racing with on_zero_handles. Whichever one of us unlinks the dispatcher will be
    // responsible for ensuring the observer is cleaned up.
    dispatcher = observer->UnlinkDispatcherLocked();
    if (dispatcher != nullptr) {
      observers_.erase(*observer);

      // If the packet is queued, then the observer will be destroyed by Dequeue() or
      // CancelQueued().
      DEBUG_ASSERT(!port_packet->is_ephemeral());
      if (port_packet->InContainer()) {
        DEBUG_ASSERT(port_packet->observer == nullptr);
        port_packet->observer.reset(observer);
      } else {
        // Otherwise, it'll be destroyed when this method returns.
        destroyer.reset(observer);
      }
    }  // else on_zero_handles must have beat us and is responsible for destroying this observer.
  }
}

zx_status_t PortDispatcher::MakeObserver(uint32_t options, Handle* handle, uint64_t key,
                                         zx_signals_t signals) {
  canary_.Assert();

  // Called under the handle table lock.

  auto dispatcher = handle->dispatcher();
  if (!dispatcher->is_waitable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto observer_result = observer_allocator.Allocate(
      options, handle, fbl::RefPtr<PortDispatcher>(this), get_lock(), key, signals);
  if (observer_result.is_error()) {
    return observer_result.error_value();
  }

  {
    Guard<CriticalMutex> guard{get_lock()};
    DEBUG_ASSERT(!zero_handles_);

    // If we're over the limit, raise an exception.
    if (observers_.size() >= gBootOptions->max_port_observers) {
      // We limit the number of observers to prevent a misbehaving program from impacting system
      // performance or stability.
      Thread::Current::SignalPolicyException(ZX_EXCP_POLICY_CODE_PORT_TOO_MANY_OBSERVERS, 0u);
    }

    observers_.push_front(observer_result.value().get());
  }

  Dispatcher::TriggerMode trigger_mode =
      options & ZX_WAIT_ASYNC_EDGE ? Dispatcher::TriggerMode::Edge : Dispatcher::TriggerMode::Level;

  return dispatcher->AddObserver(observer_result.value().release(), handle, signals, trigger_mode);
}

bool PortDispatcher::CancelQueued(const void* handle, uint64_t key) {
  canary_.Assert();

  Guard<CriticalMutex> guard{get_lock()};

  // This loop can take a while if there are many items.
  // In practice, the number of pending signal packets is
  // approximately the number of signaled _and_ watched
  // objects plus the number of pending user-queued
  // packets.
  //
  // There are two strategies to deal with too much
  // looping here if that is seen in practice.
  //
  // 1. Swap the |packets_| list for an empty list and
  //    release the lock. New arriving packets are
  //    added to the empty list while the loop happens.
  //    Readers will be blocked but the watched objects
  //    will be fully operational. Once processing
  //    is done the lists are appended.
  //
  // 2. Segregate user packets from signal packets
  //    and deliver them in order via timestamps or
  //    a side structure.

  bool packet_removed = false;

  for (auto it = packets_.begin(); it != packets_.end();) {
    if ((it->handle == handle) && (it->key() == key)) {
      auto to_remove = it++;
      if (IsDefaultAllocatedEphemeral(*to_remove)) {
        --num_ephemeral_packets_;
      }
      // Destroyed as we go around the loop.
      object_cache::UniquePtr<const PortObserver> observer =
          ktl::move(packets_.erase(to_remove)->observer);
      packet_removed = true;
    } else {
      ++it;
    }
  }

  return packet_removed;
}

bool PortDispatcher::CancelQueued(PortPacket* port_packet) {
  canary_.Assert();

  Guard<CriticalMutex> guard{get_lock()};

  if (port_packet->InContainer()) {
    if (IsDefaultAllocatedEphemeral(*port_packet)) {
      --num_ephemeral_packets_;
    }
    packets_.erase(*port_packet)->observer.reset();
    return true;
  }

  return false;
}

void PortDispatcher::InitializeCacheAllocators(uint32_t /*level*/) {
  const size_t observer_reserve_pages = gBootOptions->port_observer_reserve_pages;

  zx::result observer_result =
      object_cache::ObjectCache<PortObserver, object_cache::Option::PerCpu>::Create(
          observer_reserve_pages);

  ASSERT(observer_result.is_ok());
  observer_allocator = ktl::move(*observer_result);

  // Reserve 1 page per CPU for servicing ephemeral PortPackets, unless
  // overridden on the command line.
  const size_t packet_reserve_pages = gBootOptions->port_packet_reserve_pages;

  zx::result packet_result =
      object_cache::ObjectCache<PortPacket, object_cache::Option::PerCpu>::Create(
          packet_reserve_pages);

  ASSERT(packet_result.is_ok());
  packet_allocator = ktl::move(*packet_result);
}

// Initialize the cache after the percpu data structures are initialized.
LK_INIT_HOOK(port_observer_cache_init, PortDispatcher::InitializeCacheAllocators,
             LK_INIT_LEVEL_KERNEL + 1)
