// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher_v2.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <pow2.h>

#include <magenta/compiler.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/port.h>

#include <mxalloc/new.h>

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
    if ((key_ != key) || (handle_ != handle) || (port_.get() != port))
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

    auto status = port_->Queue(&packet_, new_state, count);

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

    auto status = Queue(port_packet, 0u, 0u);
    if (status < 0)
        delete port_packet;
    return status;
}

mx_status_t PortDispatcherV2::Queue(PortPacket* port_packet,
                                    mx_signals_t observed,
                                    uint64_t count) {
    canary_.Assert();

    int wake_count = 0;
    {
        AutoLock al(&lock_);
        if (zero_handles_)
            return ERR_BAD_STATE;

        if (observed) {
            if (port_packet->InContainer())
                return NO_ERROR;
            port_packet->packet.signal.observed = observed;
            port_packet->packet.signal.count = count;
        }

        packets_.push_back(port_packet);
        wake_count = sema_.Post();
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

mx_status_t PortDispatcherV2::DeQueue(mx_time_t deadline, mx_port_packet_t* packet) {
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
        else if (packet && packet->type == MX_PKT_TYPE_USER)
            delete port_packet;
        return NO_ERROR;

wait:
        status_t st = sema_.Wait(deadline);
        if (st != NO_ERROR)
            return st;
    }
}

PortObserver* PortDispatcherV2::CopyLocked(PortPacket* port_packet, mx_port_packet_t* packet) {
    if (packet)
        *packet = port_packet->packet;

    return (port_packet->type() == MX_PKT_TYPE_USER) ? nullptr : port_packet->observer;
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
    auto type = (options == MX_WAIT_ASYNC_ONCE) ?
        MX_PKT_TYPE_SIGNAL_ONE : MX_PKT_TYPE_SIGNAL_REP;

    auto observer = new (&ac) PortObserver(type,
            handle, mxtl::RefPtr<PortDispatcherV2>(this), key, signals);
    if (!ac.check())
        return ERR_NO_MEMORY;

    dispatcher->add_observer(observer);
    return NO_ERROR;
}
