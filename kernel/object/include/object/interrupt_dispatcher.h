// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <magenta/types.h>
#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <sys/types.h>

// TODO:
// - maintain a uint32_t state instead of single bit
// - provide a way to bind an ID to another ID
//   to notify a specific bit in state when that ID trips
//   (by default IDs set bit0 of their own state)
// - provide either a dedicated syscall or wire up UserSignal()
//   to allow userspace to set bits for "virtual" interrupts
// - return state via out param on sys_interrupt_wait

// Note that unlike most Dispatcher subclasses, this one is further
// subclassed, and so cannot be final.
class InterruptDispatcher : public Dispatcher {
public:
    InterruptDispatcher& operator=(const InterruptDispatcher &) = delete;

    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_INTERRUPT; }

    // Notify the system that the caller has finished processing the interrupt.
    // Required before the handle can be waited upon again.
    virtual mx_status_t InterruptComplete() = 0;

    // Signal the IRQ from non-IRQ state in response to a user-land request.
    virtual mx_status_t UserSignal() = 0;

    mx_status_t WaitForInterrupt() {
        return event_wait_deadline(&event_, INFINITE_TIME, true);
    }

    virtual void on_zero_handles() final {
        // Ensure any waiters stop waiting
        event_signal_etc(&event_, false, MX_ERR_CANCELED);
    }

protected:
    InterruptDispatcher() {
        event_init(&event_, false, 0);
    }
    int signal(bool resched = false) {
        return event_signal(&event_, resched);
    }
    void unsignal() {
        event_unsignal(&event_);
    }

private:
    fbl::Canary<fbl::magic("INTD")> canary_;
    event_t event_;
};
