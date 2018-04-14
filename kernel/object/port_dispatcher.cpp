// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/port_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <pow2.h>

#include <fbl/alloc_checker.h>
#include <fbl/arena.h>
#include <fbl/auto_lock.h>
#include <object/excp_port.h>
#include <object/handle.h>
#include <zircon/compiler.h>
#include <zircon/rights.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

using fbl::AutoLock;

static_assert(sizeof(zx_packet_signal_t) == sizeof(zx_packet_user_t),
              "size of zx_packet_signal_t must match zx_packet_user_t");
static_assert(sizeof(zx_packet_exception_t) == sizeof(zx_packet_user_t),
              "size of zx_packet_exception_t must match zx_packet_user_t");
static_assert(sizeof(zx_packet_guest_mem_t) == sizeof(zx_packet_user_t),
              "size of zx_packet_guest_mem_t must match zx_packet_user_t");
static_assert(sizeof(zx_packet_guest_io_t) == sizeof(zx_packet_user_t),
              "size of zx_packet_guest_io_t must match zx_packet_user_t");
static_assert(sizeof(zx_packet_guest_vcpu_t) == sizeof(zx_packet_user_t),
              "size of zx_packet_guest_vcpu_t must match zx_packet_user_t");

class ArenaPortAllocator final : public PortAllocator {
public:
    zx_status_t Init();
    virtual ~ArenaPortAllocator() = default;

    virtual PortPacket* Alloc();
    virtual void Free(PortPacket* port_packet);

    size_t DiagnosticCount() const {
        return arena_.DiagnosticCount();
    }

private:
    fbl::TypedArena<PortPacket, fbl::Mutex> arena_;
};

namespace {
constexpr size_t kMaxPendingPacketCount = 16 * 1024u;
ArenaPortAllocator port_allocator;
}  // namespace.

zx_status_t ArenaPortAllocator::Init() {
    return arena_.Init("packets", kMaxPendingPacketCount);
}

PortPacket* ArenaPortAllocator::Alloc() {
    PortPacket* packet = arena_.New(nullptr, this);
    if (packet == nullptr) {
        printf("WARNING: Could not allocate new port packet\n");
        return nullptr;
    }
    return packet;
}

void ArenaPortAllocator::Free(PortPacket* port_packet) {
    arena_.Delete(port_packet);
}

PortPacket::PortPacket(const void* handle, PortAllocator* allocator)
    : packet{}, handle(handle), observer(nullptr), allocator(allocator) {
    // Note that packet is initialized to zeros.
    if (handle) {
        // Currently |handle| is only valid if the packets are not ephemeral
        // which means that PortObserver always uses the kernel heap.
        DEBUG_ASSERT(allocator == nullptr);
    }
}

// static
size_t PortPacket::DiagnosticAllocationCount() {
    return port_allocator.DiagnosticCount();
}

PortObserver::PortObserver(uint32_t type, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
                           uint64_t key, zx_signals_t signals)
    : type_(type),
      trigger_(signals),
      packet_(handle, nullptr),
      port_(fbl::move(port)) {

    DEBUG_ASSERT(handle != nullptr);

    auto& packet = packet_.packet;
    packet.status = ZX_OK;
    packet.key = key;
    packet.type = type_;
    packet.signal.trigger = trigger_;
}

StateObserver::Flags PortObserver::OnInitialize(zx_signals_t initial_state,
                                                const StateObserver::CountInfo* cinfo) {
    uint64_t count = 1u;

    if (cinfo) {
        for (const auto& entry : cinfo->entry) {
            if ((entry.signal & trigger_) && (entry.count > 0u)) {
                count = entry.count;
                break;
            }
        }
    }
    return MaybeQueue(initial_state, count);
}

StateObserver::Flags PortObserver::OnStateChange(zx_signals_t new_state) {
    return MaybeQueue(new_state, 1u);
}

StateObserver::Flags PortObserver::OnCancel(const Handle* handle) {
    if (packet_.handle == handle) {
        return kHandled | kNeedRemoval;
    } else {
        return 0;
    }
}

StateObserver::Flags PortObserver::OnCancelByKey(const Handle* handle, const void* port, uint64_t key) {
    if ((packet_.handle != handle) || (packet_.key() != key) || (port_.get() != port))
        return 0;
    return kHandled | kNeedRemoval;
}

void PortObserver::OnRemoved() {
    if (port_->CanReap(this, &packet_))
        delete this;
}

StateObserver::Flags PortObserver::MaybeQueue(zx_signals_t new_state, uint64_t count) {
    // Always called with the object state lock being held.
    if ((trigger_ & new_state) == 0u)
        return 0;

    auto status = port_->Queue(&packet_, new_state, count);

    if ((type_ == ZX_PKT_TYPE_SIGNAL_ONE) || (status < 0))
        return kNeedRemoval;

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

void PortDispatcher::Init() {
    port_allocator.Init();
}

PortAllocator* PortDispatcher::DefaultPortAllocator() {
    return &port_allocator;
}

zx_status_t PortDispatcher::Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                                   zx_rights_t* rights) {
    DEBUG_ASSERT(options == 0);
    fbl::AllocChecker ac;
    auto disp = new (&ac) PortDispatcher(options);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_PORT_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

PortDispatcher::PortDispatcher(uint32_t /*options*/)
    : zero_handles_(false) {
}

PortDispatcher::~PortDispatcher() {
    DEBUG_ASSERT(zero_handles_);
}

void PortDispatcher::on_zero_handles() {
    canary_.Assert();

    {
        AutoLock al(get_lock());
        zero_handles_ = true;

        // Unlink and unbind exception ports.
        while (!eports_.is_empty()) {
            auto eport = eports_.pop_back();

            // Tell the eport to unbind itself, then drop our ref to it.
            get_lock()->Release();  // The eport may call our ::UnlinkExceptionPort
            eport->OnPortZeroHandles();
            get_lock()->Acquire();
        }

        // Free any queued packets.
        while (!packets_.is_empty()) {
            FreePacket(packets_.pop_front());
        }
    }
}

zx_status_t PortDispatcher::QueueUser(const zx_port_packet_t& packet) {
    canary_.Assert();

    auto port_packet = port_allocator.Alloc();
    if (!port_packet)
        return ZX_ERR_NO_MEMORY;

    port_packet->packet = packet;
    port_packet->packet.type = ZX_PKT_TYPE_USER;

    auto status = Queue(port_packet, 0u, 0u);
    if (status < 0)
        port_packet->Free();
    return status;
}

zx_status_t PortDispatcher::Queue(PortPacket* port_packet, zx_signals_t observed, uint64_t count) {
    canary_.Assert();

    int wake_count = 0;
    {
        AutoLock al(get_lock());
        if (zero_handles_)
            return ZX_ERR_BAD_STATE;

        if (observed) {
            if (port_packet->InContainer()) {
                port_packet->packet.signal.observed |= observed;
                // |count| is deliberately left as is.
                return ZX_OK;
            }
            port_packet->packet.signal.observed = observed;
            port_packet->packet.signal.count = count;
        }

        packets_.push_back(port_packet);
        wake_count = sema_.Post();
    }

    if (wake_count)
        thread_reschedule();

    return ZX_OK;
}

zx_status_t PortDispatcher::Dequeue(zx_time_t deadline, zx_port_packet_t* out_packet) {
    canary_.Assert();

    while (true) {
        {
            AutoLock al(get_lock());

            PortPacket* port_packet = packets_.pop_front();
            if (port_packet == nullptr)
                goto wait;

            *out_packet = port_packet->packet;
            FreePacket(port_packet);
        }

        return ZX_OK;

wait:
        zx_status_t st = sema_.Wait(deadline, nullptr);
        if (st != ZX_OK)
            return st;
    }
}

void PortDispatcher::FreePacket(PortPacket* port_packet) {
    PortObserver* observer = port_packet->observer;

    if (observer) {
        // Deleting the observer under the lock is fine because the
        // reference that holds to this PortDispatcher is by construction
        // not the last one. We need to do this under the lock because
        // another thread can call CanReap().
        delete observer;
    } else if (port_packet->is_ephemeral()) {
        port_packet->Free();
    }
}

bool PortDispatcher::CanReap(PortObserver* observer, PortPacket* port_packet) {
    canary_.Assert();

    AutoLock al(get_lock());
    if (!port_packet->InContainer())
        return true;
    // The destruction will happen when the packet is dequeued or in CancelQueued()
    DEBUG_ASSERT(port_packet->observer == nullptr);
    port_packet->observer = observer;
    return false;
}

zx_status_t PortDispatcher::MakeObserver(uint32_t options, Handle* handle, uint64_t key,
                                         zx_signals_t signals) {
    canary_.Assert();

    // Called under the handle table lock.

    auto dispatcher = handle->dispatcher();
    if (!dispatcher->has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;

    uint32_t type;
    switch (options) {
        case ZX_WAIT_ASYNC_ONCE:
            type = ZX_PKT_TYPE_SIGNAL_ONE;
            break;
        case ZX_WAIT_ASYNC_REPEATING:
            type = ZX_PKT_TYPE_SIGNAL_REP;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto observer = new (&ac) PortObserver(type, handle, fbl::RefPtr<PortDispatcher>(this), key,
                                           signals);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    dispatcher->add_observer(observer);
    return ZX_OK;
}

bool PortDispatcher::CancelQueued(const void* handle, uint64_t key) {
    canary_.Assert();

    AutoLock al(get_lock());

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
        if (it->handle == nullptr) {
            ++it;
            continue;
        }

        if ((it->handle == handle) && (it->key() == key)) {
            auto to_remove = it++;
            delete packets_.erase(to_remove)->observer;
            packet_removed = true;
        } else {
            ++it;
        }
    }

    return packet_removed;
}


void PortDispatcher::LinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(get_lock());
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ false));
    DEBUG_ASSERT(!eport->InContainer());
    eports_.push_back(fbl::move(AdoptRef(eport)));
}

void PortDispatcher::UnlinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(get_lock());
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ true));
    if (eport->InContainer()) {
        eports_.erase(*eport);
    }
}
