// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_dispatcher.h>

InterruptDispatcher::InterruptDispatcher() : signals_(0) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
    reported_signals_.store(0);
    memset(slot_map_, 0xff, sizeof(slot_map_));
}

zx_status_t InterruptDispatcher::AddSlotLocked(uint32_t slot, uint32_t vector, uint32_t flags) {
    size_t index = interrupts_.size();
    bool is_virtual = !!(flags & INTERRUPT_VIRTUAL);

    if (slot_map_[slot] != 0xff)
        return ZX_ERR_ALREADY_BOUND;

    for (size_t i = 0; i < index; i++) {
        const auto& interrupt = interrupts_[i];
        if (!is_virtual && !(interrupt.flags & INTERRUPT_VIRTUAL) && interrupt.vector == vector)
            return ZX_ERR_ALREADY_BOUND;
    }

    Interrupt interrupt;
    interrupt.dispatcher = this;
    atomic_store_u64(&interrupt.timestamp, 0);
    interrupt.flags = flags;
    interrupt.vector = static_cast<uint16_t>(vector);
    interrupt.slot = static_cast<uint16_t>(slot);

    fbl::AllocChecker ac;
    interrupts_.push_back(interrupt, &ac);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    if (!is_virtual) {
        zx_status_t status = RegisterInterruptHandler(vector, &interrupts_[index]);
        if (status != ZX_OK) {
            interrupts_.erase(index);
            return status;
        }
    }

    slot_map_[slot] = static_cast<uint8_t>(index);

    return ZX_OK;
}

zx_status_t InterruptDispatcher::WaitForInterrupt(uint64_t* out_slots) {
    while (true) {
        uint64_t signals = signals_.exchange(0);
        if (signals) {
            if (signals & INTERRUPT_CANCEL_MASK)
                return ZX_ERR_CANCELED;

            for (const auto& interrupt : interrupts_) {
                if ((interrupt.flags & INTERRUPT_MASK_POSTWAIT) &&
                        (signals & (SIGNAL_MASK(interrupt.slot))))
                    MaskInterrupt(interrupt.vector);
            }

            reported_signals_.fetch_or(signals);
            *out_slots = signals;
            return ZX_OK;
        }

        uint64_t last_signals = reported_signals_.exchange(0);
        for (auto& interrupt : interrupts_) {
            if ((interrupt.flags & INTERRUPT_UNMASK_PREWAIT) &&
                    (last_signals & (SIGNAL_MASK(interrupt.slot)))) {
                UnmaskInterrupt(interrupt.vector);
            }
        }

        zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
        if (status != ZX_OK) {
            return status;
        }
    }
}

zx_status_t InterruptDispatcher::GetTimeStamp(uint32_t slot, zx_time_t* out_timestamp) {
    if (slot > ZX_INTERRUPT_MAX_SLOTS)
        return ZX_ERR_INVALID_ARGS;

    uint8_t index = slot_map_[slot];
    if (index == 0xff)
        return ZX_ERR_NOT_FOUND;

    Interrupt& interrupt = interrupts_[index];
    zx_time_t timestamp = atomic_swap_u64(&interrupt.timestamp, 0);
    if (timestamp) {
        *out_timestamp = timestamp;
        return ZX_OK;
    } else {
        return ZX_ERR_BAD_STATE;
    }
}

zx_status_t InterruptDispatcher::UserSignal(uint32_t slot, zx_time_t timestamp) {
    if (slot > ZX_INTERRUPT_MAX_SLOTS)
        return ZX_ERR_INVALID_ARGS;

    uint8_t index = slot_map_[slot];
    if (index == 0xff)
        return ZX_ERR_NOT_FOUND;

    Interrupt& interrupt = interrupts_[index];
    if (!(interrupt.flags & INTERRUPT_VIRTUAL))
        return ZX_ERR_BAD_STATE;

    // only record timestamp if this is the first signal since we started waiting
    zx_time_t zero_timestamp = 0;
    atomic_cmpxchg_u64(&interrupt.timestamp, &zero_timestamp, timestamp);

    Signal(SIGNAL_MASK(slot), true);
    return ZX_OK;
}

void InterruptDispatcher::on_zero_handles() {
    for (const auto& interrupt : interrupts_) {
        if (!(interrupt.flags & INTERRUPT_VIRTUAL)) {
            MaskInterrupt(interrupt.vector);
            UnregisterInterruptHandler(interrupt.vector);
        }
    }

    Signal(INTERRUPT_CANCEL_MASK, true);
}
