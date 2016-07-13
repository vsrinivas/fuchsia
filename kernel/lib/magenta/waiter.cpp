// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/waiter.h>

#include <kernel/auto_spinlock.h>
#include <kernel/mutex.h>

#include <platform.h>

#include <utils/intrusive_single_list.h>
#include <utils/list_utils.h>


Waiter::Waiter()
    : lock_(SPIN_LOCK_INITIAL_VALUE), signals_(0u), io_port_signals_(0u), io_port_key_(0u) {
}

mx_status_t Waiter::BeginWait(event_t* event, Handle* handle, mx_signals_t signals) {
    auto node = new WaitNode{nullptr, event, handle, signals};
    if (!node)
        return ERR_NO_MEMORY;

    int wake_count = 0;
    bool io_port_bound = false;

    {
        AutoSpinLock<> lock(&lock_);

        if (io_port_) {
            // If an IO port is bound, fail regular waits.
            io_port_bound = true;
        } else {
            nodes_.push_front(node);
            // The condition might be already satisfiable, if so signal the event now.
            if (signals & signals_)
                wake_count = event_signal_etc(event, false, NO_ERROR);
        }
    }

    if (io_port_bound) {
        delete node;
        return ERR_BUSY;
    }

    if (wake_count)
        thread_yield();
    return NO_ERROR;
}

mx_signals_t Waiter::FinishWait(event_t* event) {
    WaitNode* node = nullptr;
    {
        AutoSpinLock<> lock(&lock_);
        node = utils::pop_if(&nodes_, [event](WaitNode* node) {
            return (node->event == event);
        });
    }
    if (node)
        delete node;
    return signals_;
}

bool Waiter::BindIOPort(utils::RefPtr<IOPortDispatcher> io_port, uintptr_t key, mx_signals_t signals) {
    {
        AutoSpinLock<> lock(&lock_);
        if (!signals) {
            // Unbind the IO Port.
            if (io_port_) {
                io_port_signals_ = 0u;
                io_port_key_ = 0u;
                io_port_.reset();
                return true;
            }
            return false;
        } else if (!nodes_.is_empty()) {
            // Can't bind the IO Port if we are doing handle_wait IO style.
            return false;
        } else {
            // Bind IO port. Possibly unbiding an existing IO Port.
            io_port_ = utils::move(io_port);
            io_port_signals_ = signals;
            io_port_key_ = key;
        }
    }
    return true;
}

void Waiter::ClearSignal(mx_signals_t signals) {
    AutoSpinLock<> lock(&lock_);
    signals_ &= ~signals;
}

bool Waiter::Modify(mx_signals_t set_mask, mx_signals_t clear_mask, bool yield) {
    int wake_count = 0;
    mx_signals_t signal_match = 0u;
    utils::RefPtr<IOPortDispatcher> io_port;

    {
        AutoSpinLock<> lock(&lock_);

        auto prev = signals_;
        signals_ = (signals_ & (~clear_mask)) | set_mask;

        if (prev == signals_)
            return false;

        if (io_port_) {
            signal_match = signals_ & io_port_signals_;
            // If there is signal match, we need to ref-up the io port because we are going
            // to call it from outside the lock.
            if (signal_match)
                io_port = io_port_;
        } else {
            wake_count = SignalComplete_NoLock();
        }
    }

    if (io_port)
        return SendIOPortPacket_NoLock(io_port.get(), signal_match);

    if (yield & (wake_count > 0))
        thread_yield();
    return true;
}

bool Waiter::CancelWait(Handle* handle) {
    int wake_count = 0;
    {
        AutoSpinLock<> lock(&lock_);

        utils::for_each(&nodes_, [handle, &wake_count](WaitNode* node) {
            if (node->handle == handle)
                wake_count += event_signal_etc(node->event, false, ERR_CANCELLED);
        });
    }

    if (wake_count)
        thread_yield();

    return true;
}

int Waiter::SignalComplete_NoLock() {
    int wake_count = 0;

    utils::for_each(&nodes_, [this, &wake_count](WaitNode* node) {
        if (node->signals & this->signals_)
            wake_count += event_signal_etc(node->event, false, NO_ERROR);
    });
    return wake_count;
}

bool Waiter::SendIOPortPacket_NoLock(IOPortDispatcher* io_port, mx_signals_t signals) {
    IOP_Packet packet ={
        {
            io_port_key_,
            current_time_hires(),
            0u,                     //  TODO(cpu): support bytes (for pipes)
            signals,
            0u,
        }
    };
    return io_port->Queue(&packet) == NO_ERROR;
}
