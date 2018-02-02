// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <hypervisor/state_invalidator.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>

namespace hypervisor {

// |N| is the maximum number of interrupts to be tracked.
template <uint32_t N>
class InterruptTracker {
public:
    zx_status_t Init() {
        event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
        return bitmap_.Reset(N);
    }

    // Returns whether there are pending interrupts.
    bool Pending() {
        AutoSpinLock lock(&lock_);
        return bitmap_.Scan(0, N, false) != N;
    }

    // Pops the highest priority interrupt.
    zx_status_t Pop(uint32_t* vector) {
        uint32_t value;
        {
            AutoSpinLock lock(&lock_);
            value = static_cast<uint32_t>(bitmap_.Scan(0, N, false));
            if (value == N) {
                return ZX_ERR_NOT_FOUND;
            }
            bitmap_.ClearOne(value);
        }
        // Reverse the value to get the actual interrupt.
        *vector = N - value - 1;
        return ZX_OK;
    }

    // Tracks the given interrupt.
    zx_status_t Track(uint32_t vector) {
        if (vector >= N) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        AutoSpinLock lock(&lock_);
        // We reverse the value, as RawBitmapGeneric::Scan will return the
        // lowest priority interrupt, but we need the highest priority.
        return bitmap_.SetOne(N - vector - 1);
    }

    // Tracks the given interrupt, and signals any waiters.
    zx_status_t Interrupt(uint32_t vector, bool* signaled) {
        zx_status_t status = Track(vector);
        if (status != ZX_OK) {
            return status;
        }
        int threads_unblocked = event_signal(&event_, true);
        if (signaled != nullptr) {
            *signaled = threads_unblocked > 0;
        }
        return ZX_OK;
    }

    // Waits for an interrupt.
    zx_status_t Wait(StateInvalidator* invalidator) {
        if (invalidator != nullptr) {
            invalidator->Invalidate();
        }
        do {
            zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
            if (status != ZX_OK) {
                return ZX_ERR_CANCELED;
            }
        } while (!Pending());
        return ZX_OK;
    }

private:
    event_t event_;
    SpinLock lock_;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<N>> bitmap_;
};

} // namespace hypervisor
