// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <pow2.h>

#include <magenta/compiler.h>
#include <magenta/excp_port.h>
#include <magenta/rights.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/port.h>

#include <mxtl/alloc_checker.h>

#include <mxtl/auto_lock.h>

using mxtl::AutoLock;

PortPacket::PortPacket(const void* handle) : packet{}, handle(handle), observer(nullptr) {
    // Note that packet is initialized to zeros.
}

PortObserver::PortObserver(uint32_t type, Handle* handle, mxtl::RefPtr<PortDispatcher> port,
                           uint64_t key, mx_signals_t signals)
    : type_(type),
      key_(key),
      trigger_(signals),
      handle_(handle),
      port_(mxtl::move(port)),
      packet_(handle) {

    auto& packet = packet_.packet;
    packet.status = MX_OK;
    packet.key = key_;
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
    if (handle_ == handle) {
        return kHandled | kNeedRemoval;
    } else {
        return 0;
    }
}

StateObserver::Flags PortObserver::OnCancelByKey(Handle* handle, const void* port, uint64_t key) {
    if ((key_ != key) || (handle_ != handle) || (port_.get() != port))
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

mx_status_t PortDispatcher::Create(uint32_t options,
                                     mxtl::RefPtr<Dispatcher>* dispatcher,
                                     mx_rights_t* rights) {
    DEBUG_ASSERT(options == 0);
    mxtl::AllocChecker ac;
    auto disp = new (&ac) PortDispatcher(options);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_PORT_RIGHTS;;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
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
    while (DeQueue(0ull, nullptr) == MX_OK) {}
}

mx_status_t PortDispatcher::QueueUser(const mx_port_packet_t& packet) {
    canary_.Assert();

    mxtl::AllocChecker ac;
    auto port_packet = new (&ac) PortPacket(nullptr);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    port_packet->packet = packet;
    port_packet->packet.type = MX_PKT_TYPE_USER | PKT_FLAG_EPHEMERAL;

    auto status = Queue(port_packet, 0u, 0u);
    if (status < 0)
        delete port_packet;
    return status;
}

mx_status_t PortDispatcher::Queue(PortPacket* port_packet,
                                    mx_signals_t observed,
                                    uint64_t count) {
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

mx_status_t PortDispatcher::DeQueue(mx_time_t deadline, mx_port_packet_t* packet) {
    canary_.Assert();

    PortPacket* port_packet = nullptr;
    PortObserver* observer = nullptr;

    while (true) {
        {
            AutoLock al(&lock_);
            if (packets_.is_empty())
                goto wait;

            port_packet = packets_.pop_front();
            observer = CopyLocked(port_packet, packet);
        }

        if (observer)
            delete observer;
        else if (packet && (packet->type & PKT_FLAG_EPHEMERAL))
            delete port_packet;
        return MX_OK;

wait:
        status_t st = sema_.Wait(deadline);
        if (st != MX_OK)
            return st;
    }
}

PortObserver* PortDispatcher::CopyLocked(PortPacket* port_packet, mx_port_packet_t* packet) {
    if (packet)
        *packet = port_packet->packet;

    return (port_packet->type() & PKT_FLAG_EPHEMERAL) ? nullptr : port_packet->observer;
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

mx_status_t PortDispatcher::MakeObservers(uint32_t options, Handle* handle,
                                            uint64_t key, mx_signals_t signals) {
    canary_.Assert();

    // Called under the handle table lock.

    auto dispatcher = handle->dispatcher();
    if (!dispatcher->get_state_tracker())
        return MX_ERR_NOT_SUPPORTED;

    mxtl::AllocChecker ac;
    auto type = (options == MX_WAIT_ASYNC_ONCE) ?
        MX_PKT_TYPE_SIGNAL_ONE : MX_PKT_TYPE_SIGNAL_REP;

    auto observer = new (&ac) PortObserver(type,
            handle, mxtl::RefPtr<PortDispatcher>(this), key, signals);
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
    eports_.push_back(mxtl::move(AdoptRef(eport)));
}

void PortDispatcher::UnlinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(&lock_);
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ true));
    if (eport->InContainer()) {
        eports_.erase(*eport);
    }
}
