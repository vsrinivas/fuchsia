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


PortPacket::PortPacket(PortObserver* obs) : observer(obs), packet{} {
}

// Cannot be called within any lock held.
void PortPacket::Destroy() {
    // Here we meed more state to track stack allocated packets.
    if (!observer) {
        delete this;
    } else {
        observer->End();
        delete observer;
    }
}

PortObserver::PortObserver(PortDispatcherV2* port, uint64_t key, mx_signals_t signals)
    : key_(key),
      trigger_(signals | MX_SIGNAL_HANDLE_CLOSED),
      packet_(this),
      port_(port),
      handle_(nullptr) {
}

PortObserver::~PortObserver() {
    DEBUG_ASSERT(dispatcher_ == nullptr);
}

mx_status_t PortObserver::Begin(Handle* handle) {
    // This is called with the table lock being held, so |handle| is valid.
    auto dispatcher = handle->dispatcher();
    auto state_tracker = dispatcher->get_state_tracker();
    if (!state_tracker)
        return ERR_NOT_SUPPORTED;

    handle_ = handle;
    dispatcher_ = mxtl::move(dispatcher);
    state_tracker->AddObserver(this);
    return NO_ERROR;
}

void PortObserver::End() {
    dispatcher_->get_state_tracker()->RemoveObserver(this);
    dispatcher_.reset();
}

bool PortObserver::MaybeQueue(mx_signals_t new_state) {
    // Always called with the object state lock being held.
    if (trigger_ & new_state) {
        //  TODO(cpu): |port_| is used as one-shot flag. Fix this at the
        //  state tracker layer.
        if (!port_)
            return false;

        packet_.packet.status = NO_ERROR;
        packet_.packet.key = key_;
        packet_.packet.type = MX_PKT_TYPE_SIGNAL;
        packet_.packet.signal.trigger = trigger_;
        packet_.packet.signal.effective = new_state;
        port_->Queue(&packet_);
        port_ = nullptr;
    }
    return false;
}

bool PortObserver::OnInitialize(mx_signals_t initial_state) {
    return MaybeQueue(initial_state);
}

bool PortObserver::OnStateChange(mx_signals_t new_state) {
    return MaybeQueue(new_state);
}

bool PortObserver::OnCancel(Handle* handle, bool* should_remove) {
    // This is called with the |handle| state lock being held.
    if (handle_ != handle)
        return false;
    return MaybeQueue(MX_SIGNAL_HANDLE_CLOSED);
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
    : event_(EVENT_FLAG_AUTOUNSIGNAL) {
}

PortDispatcherV2::~PortDispatcherV2() {
    while (!observers_.is_empty()) {
        auto observer = observers_.pop_front();
        observer->End();
        delete observer;
    }

    while (!packets_.is_empty()) {
        auto packet = packets_.pop_front();
        packet->Destroy();
    }
}

void PortDispatcherV2::on_zero_handles() {
}

mx_status_t PortDispatcherV2::Queue(PortPacket* packet) {
    int wake_count = 0;
    {
        AutoLock al(&lock_);

        if (packet->observer) {
            // TODO(cpu): Do this only for one-shot observers.
            observers_.erase(*packet->observer);
        }

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

PortObserver* PortDispatcherV2::MakeObserver(uint64_t key, mx_signals_t signals) {
    AllocChecker ac;
    auto observer = new (&ac) PortObserver(this, key, signals);
    if (!ac.check())
        return nullptr;

    {
        AutoLock al(&lock_);
        observers_.push_front(observer);
    }
    return observer;
}

void PortDispatcherV2::CancelObserver(PortObserver* observer) {
    AutoLock al(&lock_);
    delete observers_.erase(*observer);
}
