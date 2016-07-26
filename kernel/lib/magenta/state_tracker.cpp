// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/state_tracker.h>

#include <platform.h>

#include <kernel/auto_lock.h>

#include <magenta/wait_event.h>

#include <utils/intrusive_single_list.h>

StateTracker::StateTracker(mx_signals_state_t signals_state)
    : signals_state_(signals_state),
      io_port_signals_(0u),
      io_port_key_(0u) {
    mutex_init(&lock_);
}

StateTracker::~StateTracker() {
    mutex_destroy(&lock_);
}

mx_status_t StateTracker::AddObserver(StateObserver* observer) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        if (io_port_)
            return ERR_BUSY;

        observers_.push_front(observer);
        awoke_threads = observer->OnInitialize(signals_state_);
    }
    if (awoke_threads)
        thread_yield();
    return NO_ERROR;
}

mx_signals_state_t StateTracker::RemoveObserver(StateObserver* observer) {
    AutoLock lock(&lock_);
    observers_.erase(observer);
    return signals_state_;
}

bool StateTracker::BindIOPort(utils::RefPtr<IOPortDispatcher> io_port,
                              uint64_t key,
                              mx_signals_t signals) {
    {
        AutoLock lock(&lock_);
        if (!signals) {
            // Unbind the IO Port.
            if (io_port_) {
                io_port_signals_ = 0u;
                io_port_key_ = 0u;
                io_port_.reset();
                return true;
            }
            return false;
        } else if (!observers_.is_empty()) {
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

void StateTracker::UpdateState(mx_signals_t satisfied_set_mask,
                               mx_signals_t satisfied_clear_mask,
                               mx_signals_t satisfiable_set_mask,
                               mx_signals_t satisfiable_clear_mask) {
    bool awoke_threads = false;
    mx_signals_t signal_match = 0u;
    utils::RefPtr<IOPortDispatcher> io_port;
    uint64_t key;

    {
        AutoLock lock(&lock_);

        auto previous_signals_state = signals_state_;
        signals_state_.satisfied &= ~satisfied_clear_mask;
        signals_state_.satisfied |= satisfied_set_mask;
        signals_state_.satisfiable &= ~satisfiable_clear_mask;
        signals_state_.satisfiable |= satisfiable_set_mask;

        if (io_port_) {
            if (previous_signals_state.satisfied == signals_state_.satisfied)
                return;

            signal_match = signals_state_.satisfied & io_port_signals_;
            // If there is signal match, we need to ref-up the io port because we are going
            // to call it from outside the lock.
            if (signal_match) {
                io_port = io_port_;
                key = io_port_key_;
            }
        } else {
            if (previous_signals_state.satisfied == signals_state_.satisfied &&
                previous_signals_state.satisfiable == signals_state_.satisfiable)
                return;

            for (auto& observer : observers_) {
                awoke_threads |= observer.OnStateChange(signals_state_);
            }
        }
    }
    if (io_port)
        awoke_threads |= SendIOPortPacket(io_port.get(), key, signal_match);
    if (awoke_threads)
        thread_yield();
}

void StateTracker::Cancel(Handle* handle) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);
        for (auto& observer : observers_) {
            awoke_threads |= observer.OnCancel(handle);
        }
    }
    if (awoke_threads)
        thread_yield();
}

bool StateTracker::SendIOPortPacket(IOPortDispatcher* io_port,
                                    uint64_t key,
                                    mx_signals_t signals) {
    IOP_Packet packet ={
        {
            key,
            current_time_hires(),
            0u,                     //  TODO(cpu): support bytes (for pipes)
            signals,
            0u,
        }
    };
    return io_port->Queue(&packet) == NO_ERROR;
}
