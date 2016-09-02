// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <sys/types.h>

class InterruptDispatcher : public Dispatcher {
public:
    InterruptDispatcher& operator=(const InterruptDispatcher &) = delete;

    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_INTERRUPT; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }

    status_t UserSignal(uint32_t clear_mask, uint32_t set_mask) final {
        if ((set_mask & ~MX_SIGNAL_SIGNALED) || (clear_mask & ~MX_SIGNAL_SIGNALED))
            return ERR_INVALID_ARGS;

        state_tracker_.UpdateSatisfied(clear_mask, set_mask);
        return NO_ERROR;
    }

    // Notify the system that the caller has finished processing the interrupt.
    // Required before the handle can be waited upon again.
    virtual status_t InterruptComplete() = 0;

protected:
    InterruptDispatcher()
        : state_tracker_(true, mx_signals_state_t{0u, MX_SIGNAL_SIGNALED}) { }

    IrqStateTracker state_tracker_;
};
