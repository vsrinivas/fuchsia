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

enum class InterruptType {
    INACTIVE,
    VIRTUAL,
    PHYSICAL
};
static_assert(
    static_cast<int>(InterruptType::INACTIVE) == 0 &&
    static_cast<int>(InterruptType::VIRTUAL) == 1 &&
    static_cast<int>(InterruptType::PHYSICAL) == 2,
    "InterruptBitmap relies on these invariants.");

template <uint32_t N>
class InterruptBitmap {
public:
    zx_status_t Init() {
        return bitmap_.Reset(kNumBits);
    }

    InterruptType Get(uint32_t vector) const {
        if (vector >= N) {
            DEBUG_ASSERT(false);
            return InterruptType::INACTIVE;
        }
        size_t first_unset;
        size_t bitoff = vector * 2;
        bitmap_.Get(bitoff, bitoff + 2, &first_unset);
        return static_cast<InterruptType>(first_unset - bitoff);
    }

    void Set(uint32_t vector, InterruptType type) {
        if (vector >= N) {
            DEBUG_ASSERT(false);
            return;
        }
        size_t bitoff = vector * 2;
        bitmap_.Clear(bitoff, bitoff + 2);
        if (type != InterruptType::INACTIVE) {
            auto state_bit = static_cast<size_t>(type);
            bitmap_.Set(bitoff, bitoff + state_bit);
        }
    }

    InterruptType ReverseScan(uint32_t* vector) {
        size_t bitoff;
        bool is_empty = bitmap_.ReverseScan(0, kNumBits, false, &bitoff);
        if (is_empty) {
            return InterruptType::INACTIVE;
        }
        *vector = static_cast<uint32_t>(bitoff / 2);
        if (bitoff % 2 == 0) {
            return InterruptType::VIRTUAL;
        } else {
            return InterruptType::PHYSICAL;
        }
    }

private:
    static constexpr uint32_t kNumBits = N * 2;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumBits>> bitmap_;
};

// |N| is the maximum number of interrupts to be tracked.
template <uint32_t N>
class InterruptTracker {
public:
    zx_status_t Init() {
        event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
        AutoSpinLock lock(&lock_);
        return bitmap_.Init();
    }

    // Returns whether there are pending interrupts.
    bool Pending() {
        uint32_t vector;
        AutoSpinLock lock(&lock_);
        return bitmap_.ReverseScan(&vector) != InterruptType::INACTIVE;
    }

    // Tries to pop the given interrupt.
    InterruptType TryPop(uint32_t vector) {
        AutoSpinLock lock(&lock_);
        InterruptType type = bitmap_.Get(vector);
        if (type != InterruptType::INACTIVE) {
            bitmap_.Set(vector, InterruptType::INACTIVE);
        }
        return type;
    }

    // Pops the highest priority interrupt.
    InterruptType Pop(uint32_t* vector) {
        AutoSpinLock lock(&lock_);
        InterruptType type = bitmap_.ReverseScan(vector);
        if (type != InterruptType::INACTIVE) {
            bitmap_.Set(*vector, InterruptType::INACTIVE);
        }
        return type;
    }

    // Tracks the given interrupt.
    void Track(uint32_t vector, InterruptType type) {
        AutoSpinLock lock(&lock_);
        bitmap_.Set(vector, type);
    }

    // Tracks the given interrupt, and signals any waiters.
    void Interrupt(uint32_t vector, InterruptType type, bool* signaled) {
        Track(vector, type);
        int threads_unblocked = event_signal(&event_, true);
        if (signaled != nullptr) {
            *signaled = threads_unblocked > 0;
        }
    }

    // Tracks the given virtual interrupt, and signals any waiters.
    void VirtualInterrupt(uint32_t vector) {
        Interrupt(vector, hypervisor::InterruptType::VIRTUAL, nullptr);
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
    InterruptBitmap<N> bitmap_ TA_GUARDED(lock_);
};

} // namespace hypervisor
