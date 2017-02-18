// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher_v2.h>

#include <assert.h>
#include <err.h>
#include <new.h>

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

    auto& packet = packet_.packet;
    packet.status = NO_ERROR;
    packet.key = key_;
    packet.type = type_;
    packet.signal.trigger = trigger_;
}

bool PortObserver::OnInitialize(mx_signals_t initial_state) {
    MaybeQueue(initial_state);
    return false;
}

bool PortObserver::OnStateChange(mx_signals_t new_state) {
    MaybeQueue(new_state);
    return false;
}

bool PortObserver::OnCancel(Handle* handle) {
    if (handle_ == handle)
        remove_ = true;
    return false;
}

void PortObserver::OnRemoved() {
    if (port_->CanReap(this, &packet_))
        delete this;
}

void PortObserver::MaybeQueue(mx_signals_t new_state) {
    // Always called with the object state lock being held.
    if ((trigger_ & new_state) == 0u)
        return;

    packet_.packet.signal.effective |= new_state;

    auto status = port_->Queue(&packet_);

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
    : event_(EVENT_FLAG_AUTOUNSIGNAL),
      zero_handles_(false) {
}

PortDispatcherV2::~PortDispatcherV2() {
    DEBUG_ASSERT(zero_handles_);
}

void PortDispatcherV2::on_zero_handles() {
    {
        AutoLock al(&lock_);
        zero_handles_ = true;
    }
    while (DeQueue(0ull, nullptr) == NO_ERROR) {}
}

mx_status_t PortDispatcherV2::QueueUser(const mx_port_packet_t& packet) {
    AllocChecker ac;
    auto port_packet = new (&ac) PortPacket();
    if (!ac.check())
        return ERR_NO_MEMORY;

    port_packet->packet = packet;
    port_packet->packet.type = MX_PKT_TYPE_USER;

    auto status = Queue(port_packet);
    if (status < 0)
        delete port_packet;
    return status;
}

mx_status_t PortDispatcherV2::Queue(PortPacket* packet) {
    int wake_count = 0;
    {
        AutoLock al(&lock_);
        if (zero_handles_)
            return ERR_BAD_STATE;

        if (HandleSignalsLocked(packet))
            return NO_ERROR;

        packets_.push_back(packet);
        wake_count = event_.Signal();
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

bool PortDispatcherV2::HandleSignalsLocked(PortPacket* port_packet) {
    if (port_packet->InContainer()) {
        DEBUG_ASSERT(port_packet->type() == MX_PKT_TYPE_SIGNAL_REP);
        port_packet->packet.signal.count += 1u;
        return true;
    }

    if (port_packet->type() != MX_PKT_TYPE_USER)
        port_packet->packet.signal.count = 1u;

    return false;
}

mx_status_t PortDispatcherV2::DeQueue(mx_time_t timeout, mx_port_packet_t* packet) {
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
        if (timeout == 0ull)
            return ERR_TIMED_OUT;

        lk_time_t to = mx_time_to_lk(timeout);
        status_t st = event_.Wait((to == 0u) ? 1u : to);
        if (st != NO_ERROR)
            return st;
    }
    return ERR_BAD_STATE;
}

PortObserver* PortDispatcherV2::SnapCopyLocked(PortPacket* port_packet, mx_port_packet_t* packet) {
    if (packet)
        *packet = port_packet->packet;
    if (port_packet->type() == MX_PKT_TYPE_SIGNAL_REP ||
        port_packet->type() == MX_PKT_TYPE_SIGNAL_ONE) {
        port_packet->packet.signal.count = 0u;
        return port_packet->observer;
    }
    return nullptr;
}

bool PortDispatcherV2::CanReap(PortObserver* observer, PortPacket* port_packet) {
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
    // Called under the handle table lock.
    auto type = (options & MX_WAIT_ASYNC_REPEATING) ?
        MX_PKT_TYPE_SIGNAL_REP : MX_PKT_TYPE_SIGNAL_ONE;

    auto state_tracker = handle->dispatcher()->get_state_tracker();
    if (!state_tracker)
        return ERR_NOT_SUPPORTED;

    PortObserver* observers[sizeof(mx_signals_t) * 8u] = {};

    size_t scount = 0;

    AllocChecker ac;
    for (size_t ix = 0; ix != countof(observers); ++ix) {
        // extract a single signal bit.
        mx_signals_t one_signal = signals & (0x1u << ix);
        if (!one_signal)
            continue;

        observers[scount] = new (&ac) PortObserver(
            type, handle, mxtl::RefPtr<PortDispatcherV2>(this), key, one_signal);
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
        state_tracker->AddObserver(observers[ix]);
    }

    return NO_ERROR;
}

