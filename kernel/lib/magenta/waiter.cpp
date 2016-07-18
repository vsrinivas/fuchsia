// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/waiter.h>

#include <kernel/auto_spinlock.h>
#include <kernel/mutex.h>

#include <magenta/wait_event.h>

#include <platform.h>

#include <utils/intrusive_single_list.h>
#include <utils/list_utils.h>

Waiter::Waiter(mx_signals_state_t signals_state)
    : lock_(SPIN_LOCK_INITIAL_VALUE),
      signals_state_(signals_state),
      io_port_signals_(0u),
      io_port_key_(0u) {
}

mx_status_t Waiter::BeginWait(WaitEvent* event, Handle* handle, mx_signals_t signals, uint64_t context) {
    auto node = new WaitNode{nullptr, event, handle, signals, context};
    if (!node)
        return ERR_NO_MEMORY;

    bool awoke_threads = false;
    bool io_port_bound = false;

    {
        AutoSpinLock<> lock(&lock_);

        if (io_port_) {
            // If an IO port is bound, fail regular waits.
            io_port_bound = true;
        } else {
            nodes_.push_front(node);
            // The condition may already be satisfiable; if so signal the event now.
            if (signals & signals_state_.satisfied)
                awoke_threads |= event->Signal(WaitEvent::Result::SATISFIED, context);
            // Or the condition may already be unsatisfiable; if so signal the event now.
            else if (!(signals & signals_state_.satisfiable))
                awoke_threads |= event->Signal(WaitEvent::Result::UNSATISFIABLE, context);
        }
    }

    if (io_port_bound) {
        delete node;
        return ERR_BUSY;
    }

    if (awoke_threads)
        thread_yield();
    return NO_ERROR;
}

mx_signals_state_t Waiter::FinishWait(WaitEvent* event) {
    WaitNode* node = nullptr;
    mx_signals_state_t rv;

    {
        AutoSpinLock<> lock(&lock_);
        node = utils::pop_if(&nodes_, [event](WaitNode* node) {
            return (node->event == event);
        });

        rv = signals_state_;
    }

    if (node)
        delete node;
    return rv;
}

bool Waiter::BindIOPort(utils::RefPtr<IOPortDispatcher> io_port, uint64_t key, mx_signals_t signals) {
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

bool Waiter::UpdateState(mx_signals_t satisfied_set_mask,
                         mx_signals_t satisfied_clear_mask,
                         mx_signals_t satisfiable_set_mask,
                         mx_signals_t satisfiable_clear_mask,
                         bool yield) {
    bool awoke_threads = false;
    mx_signals_t signal_match = 0u;
    utils::RefPtr<IOPortDispatcher> io_port;

    {
        AutoSpinLock<> lock(&lock_);

        auto previous_signals_state = signals_state_;
        signals_state_.satisfied &= ~satisfied_clear_mask;
        signals_state_.satisfied |= satisfied_set_mask;
        signals_state_.satisfiable &= ~satisfiable_clear_mask;
        signals_state_.satisfiable |= satisfiable_set_mask;

        if (io_port_) {
            if (previous_signals_state.satisfied == signals_state_.satisfied)
                return false;

            signal_match = signals_state_.satisfied & io_port_signals_;
            // If there is signal match, we need to ref-up the io port because we are going
            // to call it from outside the lock.
            if (signal_match)
                io_port = io_port_;
        } else {
            if (previous_signals_state.satisfied == signals_state_.satisfied &&
                previous_signals_state.satisfiable == signals_state_.satisfiable)
                return false;

            awoke_threads |= SignalStateChange_NoLock();
        }
    }

    if (io_port)
        return SendIOPortPacket_NoLock(io_port.get(), signal_match);

    if (yield && awoke_threads)
        thread_yield();
    return true;
}

bool Waiter::CancelWait(Handle* handle) {
    bool awoke_threads = false;
    {
        AutoSpinLock<> lock(&lock_);

        utils::for_each(&nodes_, [handle, &awoke_threads](WaitNode* node) {
            if (node->handle == handle)
                awoke_threads |= node->event->Signal(WaitEvent::Result::CANCELLED, node->context);
        });
    }

    if (awoke_threads)
        thread_yield();

    return true;
}

bool Waiter::SignalStateChange_NoLock() {
    bool awoke_threads = false;

    utils::for_each(&nodes_, [this, &awoke_threads](WaitNode* node) {
        if (node->signals & signals_state_.satisfied)
            awoke_threads |= node->event->Signal(WaitEvent::Result::SATISFIED, node->context);
        else if (!(node->signals & signals_state_.satisfiable))
            awoke_threads |= node->event->Signal(WaitEvent::Result::UNSATISFIABLE, node->context);
    });
    return awoke_threads;
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
