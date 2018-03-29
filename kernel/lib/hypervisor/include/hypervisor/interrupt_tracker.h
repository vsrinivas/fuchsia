// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <hypervisor/ktrace.h>
#include <hypervisor/state_invalidator.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>
#include <lib/ktrace.h>

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
        return !bitmap_.Scan(0, N, false);
    }

    bool TryPop(uint32_t vector) {
        AutoSpinLock lock(&lock_);
        bool has_vector = bitmap_.GetOne(reverse(vector));
        if (has_vector) {
            bitmap_.ClearOne(vector);
        }
        return has_vector;
    }

    // Pops the highest priority interrupt.
    zx_status_t Pop(uint32_t* vector) {
        size_t value;
        {
            AutoSpinLock lock(&lock_);
            if (bitmap_.Scan(0, N, false, &value)) {
                return ZX_ERR_NOT_FOUND;
            }
            bitmap_.ClearOne(value);
        }
        *vector = reverse(static_cast<uint32_t>(value));
        return ZX_OK;
    }

    // Tracks the given interrupt.
    zx_status_t Track(uint32_t vector) {
        if (vector >= N) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        AutoSpinLock lock(&lock_);
        bitmap_.SetOne(reverse(vector));
        return ZX_OK;
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
        ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_INTERRUPT);
        do {
            zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
            if (status != ZX_OK) {
                ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_INTERRUPT);
                return ZX_ERR_CANCELED;
            }
        } while (!Pending());
        ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_INTERRUPT);
        return ZX_OK;
    }

private:
    event_t event_;
    SpinLock lock_;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<N>> bitmap_;

    // We reverse the value, as RawBitmapGeneric::Scan will return the
    // lowest priority interrupt, but we need the highest priority.
    static uint32_t reverse(uint32_t vector) {
        return N - vector - 1;
    }
};

} // namespace hypervisor
