// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher_v2.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <pow2.h>

#include <magenta/compiler.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/port.h>

#include <kernel/auto_lock.h>

constexpr mx_rights_t kDefaultIOPortRightsV2 =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

PortPacket::PortPacket() : packet{}, observer(nullptr) {
    // Note that packet is initialized to zeros.
}

PortObserver::PortObserver(uint32_t type, Handle* handle, mxtl::RefPtr<PortDispatcherV2> port,
                           uint64_t key, mx_signals_t signals)
    : type_(type),
      key_(key),
      trigger_(signals),
      handle_(handle),
      port_(mxtl::move(port)) {

    if (type_ == MX_PKT_TYPE_SIGNAL_REP) {
        // only one signal bit supported as a trigger.
        DEBUG_ASSERT(ispow2(trigger_));
    }

    auto& packet = packet_.packet;
    packet.status = NO_ERROR;
    packet.key = key_;
    packet.type = type_;
    packet.signal.trigger = trigger_;
}

bool PortObserver::OnInitialize(mx_signals_t initial_state,
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
    MaybeQueue(initial_state, count);
    return false;
}

bool PortObserver::OnStateChange(mx_signals_t new_state) {
    MaybeQueue(new_state, 1u);
    return false;
}

bool PortObserver::OnCancel(Handle* handle) {
    if (handle_ == handle)
        remove_ = true;
    return false;
}

bool PortObserver::OnCancelByKey(Handle* handle, const void* port, uint64_t key) {
    if ((key_ != key) || (handle_ != handle))
        return false;
    // Stopgap check so that mx_handle_cancel( ..MX_CANCEL_KEY) is not broken.
    // Remove once clients have transitioned.
    if (port && (port != port_.get()))
        return false;

    remove_ = true;
    return false;
}

void PortObserver::OnRemoved() {
    if (port_->CanReap(this, &packet_))
        delete this;
}

void PortObserver::MaybeQueue(mx_signals_t new_state, uint64_t count) {
    // Always called with the object state lock being held.
    if ((trigger_ & new_state) == 0u)
        return;

    packet_.packet.signal.observed |= new_state;

    auto status = port_->Queue(&packet_, count);

    if ((type_ == MX_PKT_TYPE_SIGNAL_ONE) || (status < 0))
        remove_ = true;
}

/////////////////////////////////////////////////////////////////////////////////////////

mx_status_t PortDispatcherV2::Create(uint32_t options,
                                     mxtl::RefPtr<Dispatcher>* dispatcher,
                                     mx_rights_t* rights) {
    DEBUG_ASSERT(options == MX_PORT_OPT_V2);
    AllocChecker ac;
    auto disp = new (&ac) PortDispatcherV2(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultIOPortRightsV2;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

PortDispatcherV2::PortDispatcherV2(uint32_t /*options*/)
    : zero_handles_(false) {
}

PortDispatcherV2::~PortDispatcherV2() {
    DEBUG_ASSERT(zero_handles_);
}

void PortDispatcherV2::on_zero_handles() {
    canary_.Assert();

    {
        AutoLock al(&lock_);
        zero_handles_ = true;
    }
    while (DeQueue(0ull, nullptr) == NO_ERROR) {}
}

mx_status_t PortDispatcherV2::QueueUser(const mx_port_packet_t& packet) {
    canary_.Assert();

    AllocChecker ac;
    auto port_packet = new (&ac) PortPacket();
    if (!ac.check())
        return ERR_NO_MEMORY;

    port_packet->packet = packet;
    port_packet->packet.type = MX_PKT_TYPE_USER;

    auto status = Queue(port_packet, 0u);
    if (status < 0)
        delete port_packet;
    return status;
}

mx_status_t PortDispatcherV2::Queue(PortPacket* packet, uint64_t count) {
    canary_.Assert();

    int wake_count = 0;
    {
        AutoLock al(&lock_);
        if (zero_handles_)
            return ERR_BAD_STATE;

        if (!UpdateSignalCountLocked(packet, count))
            packets_.push_back(packet);

        wake_count = sema_.Post();
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

bool PortDispatcherV2::UpdateSignalCountLocked(PortPacket* port_packet, uint64_t count) {
    if (port_packet->InContainer()) {
        DEBUG_ASSERT(port_packet->type() == MX_PKT_TYPE_SIGNAL_REP);
        port_packet->packet.signal.count += count;
        return true;
    }
    // Not in container.
    if (port_packet->type() != MX_PKT_TYPE_USER)
        port_packet->packet.signal.count = count;
    return false;
}

mx_status_t PortDispatcherV2::DeQueue(mx_time_t timeout, mx_port_packet_t* packet) {
    canary_.Assert();

    PortPacket* port_packet = nullptr;
    PortObserver* observer = nullptr;

    while (true) {
        {
            AutoLock al(&lock_);
            if (packets_.is_empty())
                goto wait;

            port_packet = packets_.pop_front();
            observer = SnapCopyLocked(port_packet, packet);
        }

        if (observer)
            delete observer;
        else if (packet && packet->type == MX_PKT_TYPE_USER)
            delete port_packet;
        return NO_ERROR;

wait:
        status_t st = sema_.Wait(mx_time_to_lk(timeout));
        if (st != NO_ERROR)
            return st;
    }
}

PortObserver* PortDispatcherV2::SnapCopyLocked(PortPacket* port_packet, mx_port_packet_t* packet) {
    if (packet)
        *packet = port_packet->packet;
    // For non-repeating: queue only once, but the signal.count can be > 1.
    if (port_packet->type() == MX_PKT_TYPE_SIGNAL_ONE)
        return port_packet->observer;
    // For repeating: requeue until the count is zero. signal.count is always 1.
    if (port_packet->type() == MX_PKT_TYPE_SIGNAL_REP){
        if (packet)
            packet->signal.count = 1u;
        if (--port_packet->packet.signal.count == 0u)
            return port_packet->observer;
        packets_.push_back(port_packet);
    }
    // For other packet types there is no observer controling the lifetime.
    return nullptr;
}

bool PortDispatcherV2::CanReap(PortObserver* observer, PortPacket* port_packet) {
    canary_.Assert();

    AutoLock al(&lock_);
    if (!port_packet->InContainer())
        return true;
    // The destruction will happen when the packet is dequeued.
    DEBUG_ASSERT(port_packet->observer == nullptr);
    port_packet->observer = observer;
    return false;
}

mx_status_t PortDispatcherV2::MakeObservers(uint32_t options, Handle* handle,
                                            uint64_t key, mx_signals_t signals) {
    canary_.Assert();

    // Called under the handle table lock.

    auto dispatcher = handle->dispatcher();
    if (!dispatcher->get_state_tracker())
        return ERR_NOT_SUPPORTED;

    AllocChecker ac;

    if (options == MX_WAIT_ASYNC_ONCE) {
        auto observer = new (&ac) PortObserver(MX_PKT_TYPE_SIGNAL_ONE,
            handle, mxtl::RefPtr<PortDispatcherV2>(this), key, signals);
        if (!ac.check())
            return ERR_NO_MEMORY;
        dispatcher->add_observer(observer);
    } else {
        // In repeating mode we add an observer per signal bit.
        PortObserver* observers[sizeof(mx_signals_t) * 8u] = {};
        size_t scount = 0;

        for (size_t ix = 0; ix != countof(observers); ++ix) {
            // extract a single signal bit.
            mx_signals_t one_signal = signals & (0x1u << ix);
            if (!one_signal)
                continue;

            observers[scount] = new (&ac) PortObserver(MX_PKT_TYPE_SIGNAL_REP,
                handle, mxtl::RefPtr<PortDispatcherV2>(this), key, one_signal);
            if (!ac.check()) {
                // Delete the pending observers and exit.
                for (size_t jx = 0; jx != scount; ++jx) {
                    delete observers[jx];
                }
                return ERR_NO_MEMORY;
            }
            ++scount;
        }

        for (size_t ix = 0; ix != scount; ++ix) {
            __UNUSED auto status = dispatcher->add_observer(observers[ix]);
            DEBUG_ASSERT(status == NO_ERROR);
        }
    }

    return NO_ERROR;
}
