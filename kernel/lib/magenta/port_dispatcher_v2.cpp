// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher_v2.h>

#include <assert.h>
#include <err.h>
#include <new.h>

#include <arch/ops.h>

#include <magenta/state_tracker.h>
#include <magenta/syscalls/port.h>

#include <kernel/auto_lock.h>

constexpr mx_rights_t kDefaultIOPortRightsV2 =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

PortPacket::PortPacket(PortObserver* obs) : observer(obs), packet{} {
}

// Cannot be called within any lock held.
void PortPacket::Destroy() {
    // TODO(cpu): need more state to track stack allocated packets.
    if (!observer) {
        delete this;
    } else {
        observer->End();
    }
}

PortObserver::PortObserver(uint32_t type, mxtl::RefPtr<PortDispatcherV2> port,
                           uint64_t key, mx_signals_t signals)
    : type_(type),
      key_(key),
      trigger_(signals|MX_SIGNAL_HANDLE_CLOSED),
      packet_(this),
      port_(mxtl::move(port)),
      handle_(nullptr) {
}

void PortObserver::Begin(Handle* handle) {
    handle_ = handle;
}

void PortObserver::End() {
    if (type_ == MX_PKT_TYPE_SIGNAL_ONE) {
        delete this;
    } else {
        SnapCount();
        // For repeating observer we need to wait until cancelation since that is
        // the last time we are going to be called.
        auto& signal = packet_.packet.signal;
        if (__atomic_load_n(&signal.effective, __ATOMIC_RELAXED) & MX_SIGNAL_HANDLE_CLOSED)
            delete this;
    }
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
    // This is called with the |handle| state lock being held.
    if (handle_ != handle)
        return false;
    MaybeQueue(MX_SIGNAL_HANDLE_CLOSED);
    return false;
}

void PortObserver::MaybeQueue(mx_signals_t new_state) {
    // Always called with the object state lock being held.
    if ((trigger_ & new_state) == 0u)
        return;

    if ((type_ == MX_PKT_TYPE_SIGNAL_ONE) || (new_state == MX_SIGNAL_HANDLE_CLOSED))
        remove_ = true;

    auto& packet = packet_.packet;

    // If its a repeating packet, only Queue() it once and
    // update the effective and count values atomically.
    if (type_ == MX_PKT_TYPE_SIGNAL_REP) {
        packet.signal.effective |= new_state;
        if (atomic_add_u64(&packet.signal.count, 1u) > 0u)
            return;
    }

    packet.status = NO_ERROR;
    packet.key = key_;
    packet.type = type_;
    packet.signal.trigger = trigger_;
    packet.signal.effective = new_state;
    packet.signal.count = 1u;

    if (port_->Queue(&packet_) < 0)
        ReapSelf();
}

void::PortObserver::SnapCount() {
    // TODO(cpu): this is not fully cooked. The count might be racy and/or
    // meaninless for multiple signal bits.
    if (type_ == MX_PKT_TYPE_SIGNAL_REP)
        atomic_swap_u64(&packet_.packet.signal.count, 0u);
}

void PortObserver::ReapSelf() {
    // TODO(cpu): convert |handle_| into a refptr Dispatcher and use that
    // to syncronize Destroy() with the state_observer during a dpc call.
    remove_ = true;
    port_.reset();
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
}

void PortDispatcherV2::on_zero_handles() {
    AutoLock al(&lock_);

    while (!packets_.is_empty()) {
        auto packet = packets_.pop_front();
        packet->Destroy();
    }

    zero_handles_ = true;
}

mx_status_t PortDispatcherV2::Queue(mxtl::unique_ptr<PortPacket> packet) {
    auto status = Queue(packet.get());
    if (status == NO_ERROR)
        packet.release();
    return status;
}

mx_status_t PortDispatcherV2::Queue(PortPacket* packet) {
    int wake_count = 0;
    {
        AutoLock al(&lock_);
        if (zero_handles_)
            return ERR_BAD_STATE;
        packets_.push_back(packet);
        wake_count = event_.Signal();
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

mx_status_t PortDispatcherV2::DeQueue(mx_time_t timeout, PortPacket** packet) {
    while (true) {
        {
            AutoLock al(&lock_);
            if (!packets_.is_empty()) {
                *packet = packets_.pop_front();
                return NO_ERROR;
            }
        }

        if (timeout == 0ull)
            return ERR_TIMED_OUT;

        lk_time_t to = mx_time_to_lk(timeout);
        status_t st = event_.Wait((to == 0u) ? 1u : to);
        if (st != NO_ERROR)
            return st;
    }
    return ERR_BAD_STATE;
}

PortObserver* PortDispatcherV2::MakeObserver(uint32_t options, uint64_t key, mx_signals_t signals) {
    auto type = (options & MX_WAIT_ASYNC_REPEATING) ?
        MX_PKT_TYPE_SIGNAL_REP : MX_PKT_TYPE_SIGNAL_ONE;

    AllocChecker ac;
    auto observer = new (&ac) PortObserver(
        type, mxtl::RefPtr<PortDispatcherV2>(this), key, signals);
    if (!ac.check())
        return nullptr;
    return observer;
}

