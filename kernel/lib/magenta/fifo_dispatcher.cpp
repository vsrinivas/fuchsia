// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/fifo_dispatcher.h>

#include <new.h>
#include <kernel/auto_lock.h>

constexpr mx_rights_t kDefaultFifoRights = MX_FIFO_PRODUCER_RIGHTS | MX_FIFO_CONSUMER_RIGHTS;

class FifoDispatcher::StateUpdater {
public:
    StateUpdater(const FifoDispatcher* fifo, mx_fifo_state_t* state)
        : fifo_(fifo), state_(state) {}
    ~StateUpdater() {
        if (fifo_ && state_) {
            *state_ = fifo_->state_;
        }
    }

private:
    const FifoDispatcher* fifo_;
    mx_fifo_state_t* state_;
};

status_t FifoDispatcher::Create(uint64_t count, mxtl::RefPtr<Dispatcher>* dispatcher,
                                 mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) FifoDispatcher(count);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultFifoRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

FifoDispatcher::FifoDispatcher(uint64_t count)
    : count_(count) {
    state_.head = state_.tail = 0;
    state_tracker_.set_initial_signals_state(MX_FIFO_EMPTY|MX_FIFO_NOT_FULL);
}

FifoDispatcher::~FifoDispatcher() {}

void FifoDispatcher::GetState(mx_fifo_state_t* out) const {
    AutoLock lock(&lock_);
    StateUpdater updater(this, out);
    // nothing else to do, other than update the state (via the StateUpdater)
}

status_t FifoDispatcher::AdvanceHead(uint64_t count, mx_fifo_state_t* out) {
    AutoLock lock(&lock_);
    StateUpdater updater(this, out);

    if (!count) return NO_ERROR;
    if (state_.head + count - state_.tail > count_) return ERR_OUT_OF_RANGE;

    auto prev_head = state_.head;
    state_.head += count;

    if (prev_head == state_.tail) {
        state_tracker_.UpdateState(MX_FIFO_EMPTY, MX_FIFO_NOT_EMPTY);
    }
    if (state_.head - state_.tail == count_) {
        state_tracker_.UpdateState(MX_FIFO_NOT_FULL, MX_FIFO_FULL);
    }

    return NO_ERROR;
}

status_t FifoDispatcher::AdvanceTail(uint64_t count, mx_fifo_state_t* out) {
    AutoLock lock(&lock_);
    StateUpdater updater(this, out);

    if (!count) return NO_ERROR;
    if (state_.tail + count > state_.head) return ERR_OUT_OF_RANGE;

    auto prev_tail = state_.tail;
    state_.tail += count;

    if (state_.head - prev_tail == count_) {
        state_tracker_.UpdateState(MX_FIFO_FULL, MX_FIFO_NOT_FULL);
    }
    if (state_.head == state_.tail) {
        state_tracker_.UpdateState(MX_FIFO_NOT_EMPTY, MX_FIFO_EMPTY);
    }

    return NO_ERROR;
}

status_t FifoDispatcher::SetException(mx_signals_t signal, bool set, mx_fifo_state_t* out) {
    AutoLock lock(&lock_);
    StateUpdater updater(this, out);

    if (signal != MX_FIFO_PRODUCER_EXCEPTION &&
        signal != MX_FIFO_CONSUMER_EXCEPTION) {
        return ERR_INVALID_ARGS;
    }

    if (set) {
        state_tracker_.UpdateState(0u, signal);
    } else {
        state_tracker_.UpdateState(signal, 0u);
    }
    return NO_ERROR;
}
