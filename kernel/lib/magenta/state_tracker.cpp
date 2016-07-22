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
#include <utils/list_utils.h>

StateTracker::StateTracker(mx_signals_state_t signals_state)
    : signals_state_(signals_state),
      io_port_signals_(0u),
      io_port_key_(0u) {
    mutex_init(&lock_);
}

StateTracker::~StateTracker() {
    mutex_destroy(&lock_);
}

mx_status_t StateTracker::BeginWait(WaitEvent* event,
                                    Handle* handle,
                                    mx_signals_t signals,
                                    uint64_t context) {
    auto observer = new StateObserver(event, handle, signals, context);
    if (!observer)
        return ERR_NO_MEMORY;

    bool awoke_threads = false;
    bool io_port_bound = false;

    {
        AutoLock lock(&lock_);

        if (io_port_) {
            // If an IO port is bound, fail regular waits.
            io_port_bound = true;
        } else {
            observers_.push_front(observer);
            // The condition may already be satisfiable; if so signal the event now.
            if (signals & signals_state_.satisfied)
                awoke_threads |= event->Signal(WaitEvent::Result::SATISFIED, context);
            // Or the condition may already be unsatisfiable; if so signal the event now.
            else if (!(signals & signals_state_.satisfiable))
                awoke_threads |= event->Signal(WaitEvent::Result::UNSATISFIABLE, context);
        }
    }

    if (io_port_bound) {
        delete observer;
        return ERR_BUSY;
    }

    if (awoke_threads)
        thread_yield();
    return NO_ERROR;
}

mx_signals_state_t StateTracker::FinishWait(WaitEvent* event) {
    StateObserver* observer = nullptr;
    mx_signals_state_t rv;

    {
        AutoLock lock(&lock_);
        observer = observers_.erase_if([event](const StateObserver& observer) -> bool {
                return (observer.event == event);
            });
        rv = signals_state_;
    }

    if (observer)
        delete observer;
    return rv;
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
            if (signal_match)
                io_port = io_port_;
        } else {
            if (previous_signals_state.satisfied == signals_state_.satisfied &&
                previous_signals_state.satisfiable == signals_state_.satisfiable)
                return;

            awoke_threads |= SignalStateChange_NoLock();
        }
    }
    if (io_port)
        awoke_threads |= SendIOPortPacket_NoLock(io_port.get(), signal_match);
    if (awoke_threads)
        thread_yield();
}

void StateTracker::CancelWait(Handle* handle) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        for (auto& observer : observers_) {
            if (observer.handle == handle) {
                awoke_threads |= !!observer.event->Signal(WaitEvent::Result::CANCELLED,
                                                          observer.context);
            }
        }
    }
    if (awoke_threads)
        thread_yield();
}

bool StateTracker::SignalStateChange_NoLock() {
    bool awoke_threads = false;
    for (auto& observer : observers_) {
        if (observer.signals & signals_state_.satisfied) {
            awoke_threads |= !!observer.event->Signal(WaitEvent::Result::SATISFIED,
                                                      observer.context);
        } else if (!(observer.signals & signals_state_.satisfiable)) {
            awoke_threads |= !!observer.event->Signal(WaitEvent::Result::UNSATISFIABLE,
                                                      observer.context);
        }
    }
    return awoke_threads;
}

bool StateTracker::SendIOPortPacket_NoLock(IOPortDispatcher* io_port, mx_signals_t signals) {
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
