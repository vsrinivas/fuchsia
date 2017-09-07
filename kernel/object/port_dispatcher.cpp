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

#include <magenta/compiler.h>
#include <magenta/rights.h>
#include <magenta/syscalls/port.h>
#include <fbl/alloc_checker.h>
#include <fbl/arena.h>
#include <fbl/auto_lock.h>
#include <object/excp_port.h>
#include <object/state_tracker.h>

using fbl::AutoLock;

static_assert(sizeof(mx_packet_signal_t) == sizeof(mx_packet_user_t),
              "size of mx_packet_signal_t must match mx_packet_user_t");
static_assert(sizeof(mx_packet_exception_t) == sizeof(mx_packet_user_t),
              "size of mx_packet_exception_t must match mx_packet_user_t");
static_assert(sizeof(mx_packet_guest_mem_t) == sizeof(mx_packet_user_t),
              "size of mx_packet_guest_mem_t must match mx_packet_user_t");
static_assert(sizeof(mx_packet_guest_io_t) == sizeof(mx_packet_user_t),
              "size of mx_packet_guest_io_t must match mx_packet_user_t");

class ArenaPortAllocator final : public PortAllocator {
public:
    mx_status_t Init();
    virtual ~ArenaPortAllocator() = default;

    virtual PortPacket* Alloc();
    virtual void Free(PortPacket* port_packet);

private:
    fbl::TypedArena<PortPacket, fbl::Mutex> arena_;
};

namespace {
constexpr size_t kMaxPendingPacketCount = 16 * 1024u;
ArenaPortAllocator port_allocator;
}  // namespace.

mx_status_t ArenaPortAllocator::Init() {
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

PortObserver::PortObserver(uint32_t type, const Handle* handle, fbl::RefPtr<PortDispatcher> port,
                           uint64_t key, mx_signals_t signals)
    : type_(type),
      trigger_(signals),
      packet_(handle, nullptr),
      port_(fbl::move(port)) {

    DEBUG_ASSERT(handle != nullptr);

    auto& packet = packet_.packet;
    packet.status = MX_OK;
    packet.key = key;
    packet.type = type_;
    packet.signal.trigger = trigger_;
}

StateObserver::Flags PortObserver::OnInitialize(mx_signals_t initial_state,
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

StateObserver::Flags PortObserver::OnStateChange(mx_signals_t new_state) {
    return MaybeQueue(new_state, 1u);
}

StateObserver::Flags PortObserver::OnCancel(Handle* handle) {
    if (packet_.handle == handle) {
        return kHandled | kNeedRemoval;
    } else {
        return 0;
    }
}

StateObserver::Flags PortObserver::OnCancelByKey(Handle* handle, const void* port, uint64_t key) {
    if ((packet_.handle != handle) || (packet_.key() != key) || (port_.get() != port))
        return 0;
    return kHandled | kNeedRemoval;
}

void PortObserver::OnRemoved() {
    if (port_->CanReap(this, &packet_))
        delete this;
}

StateObserver::Flags PortObserver::MaybeQueue(mx_signals_t new_state, uint64_t count) {
    // Always called with the object state lock being held.
    if ((trigger_ & new_state) == 0u)
        return 0;

    auto status = port_->Queue(&packet_, new_state, count);

    if ((type_ == MX_PKT_TYPE_SIGNAL_ONE) || (status < 0))
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

mx_status_t PortDispatcher::Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                                   mx_rights_t* rights) {
    DEBUG_ASSERT(options == 0);
    fbl::AllocChecker ac;
    auto disp = new (&ac) PortDispatcher(options);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_PORT_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
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
        AutoLock al(&lock_);
        zero_handles_ = true;

        // Unlink and unbind exception ports.
        while (!eports_.is_empty()) {
            auto eport = eports_.pop_back();

            // Tell the eport to unbind itself, then drop our ref to it.
            lock_.Release();  // The eport may call our ::UnlinkExceptionPort
            eport->OnPortZeroHandles();
            lock_.Acquire();
        }
    }
    while (Dequeue(0ull, nullptr) == MX_OK) {}
}

mx_status_t PortDispatcher::QueueUser(const mx_port_packet_t& packet) {
    canary_.Assert();

    auto port_packet = port_allocator.Alloc();
    if (!port_packet)
        return MX_ERR_NO_MEMORY;

    port_packet->packet = packet;
    port_packet->packet.type = MX_PKT_TYPE_USER | PKT_FLAG_EPHEMERAL;

    auto status = Queue(port_packet, 0u, 0u);
    if (status < 0)
        port_packet->Free();
    return status;
}

mx_status_t PortDispatcher::Queue(PortPacket* port_packet, mx_signals_t observed, uint64_t count) {
    canary_.Assert();

    int wake_count = 0;
    {
        AutoLock al(&lock_);
        if (zero_handles_)
            return MX_ERR_BAD_STATE;

        if (observed) {
            if (port_packet->InContainer())
                return MX_OK;
            port_packet->packet.signal.observed = observed;
            port_packet->packet.signal.count = count;
        }

        packets_.push_back(port_packet);
        wake_count = sema_.Post();
    }

    if (wake_count)
        thread_reschedule();

    return MX_OK;
}

mx_status_t PortDispatcher::Dequeue(mx_time_t deadline, mx_port_packet_t* out_packet) {
    canary_.Assert();

    while (true) {
        {
            AutoLock al(&lock_);

            PortPacket* port_packet = packets_.pop_front();
            if (port_packet == nullptr)
                goto wait;

            if (out_packet != nullptr)
                *out_packet = port_packet->packet;

            PortObserver* observer = port_packet->observer;

            if (observer) {
                // Deleting the observer under the lock is fine because
                // the reference that holds to this PortDispatcher is by
                // construction not the last one. We need to do this under
                // the lock because another thread can call CanReap().
                delete observer;
            } else if (port_packet->is_ephemeral()) {
                port_packet->Free();
            }
        }

        return MX_OK;

wait:
        mx_status_t st = sema_.Wait(deadline);
        if (st != MX_OK)
            return st;
    }
}

bool PortDispatcher::CanReap(PortObserver* observer, PortPacket* port_packet) {
    canary_.Assert();

    AutoLock al(&lock_);
    if (!port_packet->InContainer())
        return true;
    // The destruction will happen when the packet is dequeued or in CancelQueued()
    DEBUG_ASSERT(port_packet->observer == nullptr);
    port_packet->observer = observer;
    return false;
}

mx_status_t PortDispatcher::MakeObserver(uint32_t options, Handle* handle, uint64_t key,
                                         mx_signals_t signals) {
    canary_.Assert();

    // Called under the handle table lock.

    auto dispatcher = handle->dispatcher();
    if (!dispatcher->get_state_tracker())
        return MX_ERR_NOT_SUPPORTED;

    fbl::AllocChecker ac;
    auto type = (options == MX_WAIT_ASYNC_ONCE) ?
        MX_PKT_TYPE_SIGNAL_ONE : MX_PKT_TYPE_SIGNAL_REP;

    auto observer = new (&ac) PortObserver(type, handle, fbl::RefPtr<PortDispatcher>(this), key,
                                           signals);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    dispatcher->add_observer(observer);
    return MX_OK;
}

bool PortDispatcher::CancelQueued(const void* handle, uint64_t key) {
    canary_.Assert();

    AutoLock al(&lock_);

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

    AutoLock al(&lock_);
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ false));
    DEBUG_ASSERT(!eport->InContainer());
    eports_.push_back(fbl::move(AdoptRef(eport)));
}

void PortDispatcher::UnlinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(&lock_);
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ true));
    if (eport->InContainer()) {
        eports_.erase(*eport);
    }
}
