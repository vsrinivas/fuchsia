// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/waiter.h>

#include <kernel/auto_spinlock.h>
#include <kernel/mutex.h>

#include <utils/intrusive_single_list.h>
#include <utils/list_utils.h>

Waiter::Waiter()
    : lock_(SPIN_LOCK_INITIAL_VALUE), signals_(0u) {
}

Waiter* Waiter::BeginWait(event_t* event, Handle* handle, mx_signals_t signals) {
    auto node = new WaitNode{nullptr, event, handle, signals};
    if (!node)
        return nullptr;

    int wake_count = 0;
    {
        AutoSpinLock<> lock(&lock_);
        nodes_.push_front(node);

        if (signals & signals_)
            wake_count = event_signal_etc(event, false, NO_ERROR);
    }

    if (wake_count)
        thread_yield();

    return this;
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

bool Waiter::Signal(mx_signals_t signals) {
    int wake_count = 0;
    {
        AutoSpinLock<> lock(&lock_);

        auto prev = signals_;
        signals_ |= signals;

        if (prev == signals_)
            return false;

        wake_count = SignalComplete_NoLock();
    }

    if (wake_count)
        thread_yield();

    return true;
}

void Waiter::ClearSignal(mx_signals_t signals) {
    signals_ &= ~signals;
}

void Waiter::Modify(mx_signals_t set_mask, mx_signals_t clear_mask) {
    int wake_count = 0;
    {
        AutoSpinLock<> lock(&lock_);

        auto prev = signals_;
        signals_ = (signals_ & (~clear_mask)) | set_mask;

        if (prev == signals_)
            return;

        wake_count = SignalComplete_NoLock();
    }

    if (wake_count)
        thread_yield();
}

bool Waiter::Reset() {
    AutoSpinLock<> lock(&lock_);
    utils::for_each(&nodes_, [](WaitNode* node) {
        event_unsignal(node->event);
    });
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
