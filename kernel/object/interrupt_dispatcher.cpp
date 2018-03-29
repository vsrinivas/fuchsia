// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_dispatcher.h>
#include <zircon/syscalls/port.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <dev/interrupt.h>
#include <platform.h>

#if ENABLE_NEW_IRQ_API
InterruptDispatcher::InterruptDispatcher()
    : timestamp_(0), state_(InterruptState::IDLE), is_bound_(false) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}
#else
InterruptDispatcher::InterruptDispatcher()
    : signals_(0) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
    reported_signals_.store(0);
    memset(slot_map_, 0xff, sizeof(slot_map_));
}
#endif

#if ENABLE_NEW_IRQ_API
zx_status_t InterruptDispatcher::RegisterInterruptHandler_HelperLocked(uint32_t vector,
                                                                       uint32_t flags) {
    bool is_virtual = !!(flags & INTERRUPT_VIRTUAL);

    flags_ = flags;
    vector_ = static_cast<uint16_t>(vector);

    if (!is_virtual) {
        zx_status_t status = RegisterInterruptHandler(vector, this);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}
#endif

zx_status_t InterruptDispatcher::AddSlotLocked(uint32_t slot, uint32_t vector, uint32_t flags) {
#if ENABLE_NEW_IRQ_API
    return ZX_ERR_NOT_SUPPORTED;
#else
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
#endif
}

#if ENABLE_NEW_IRQ_API
zx_status_t InterruptDispatcher::WaitForInterrupt(zx_time_t* out_timestamp) {
    while (true) {
        {
            AutoSpinLock guard(&spinlock_);
            switch (state_) {
            case InterruptState::DESTROYED:
                return ZX_ERR_CANCELED;
            case InterruptState::TRIGGERED:
                state_ = InterruptState::NEEDACK;
                *out_timestamp = timestamp_;
                timestamp_ = 0;
                return event_unsignal(&event_);
            case InterruptState::NEEDACK:
                if (flags_ & INTERRUPT_UNMASK_PREWAIT) {
                    UnmaskInterrupt(vector_);
                }
                break;
            case InterruptState::IDLE:
                break;
            default:
                return ZX_ERR_BAD_STATE;
            }
            state_ = InterruptState::WAITING;
        }

        zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
        if (status != ZX_OK) {
            return status;
        }
    }
}
#else
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
#endif

zx_status_t InterruptDispatcher::GetTimeStamp(uint32_t slot, zx_time_t* out_timestamp) {
#if ENABLE_NEW_IRQ_API
    return ZX_ERR_NOT_SUPPORTED;
#else
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
#endif
}

#if ENABLE_NEW_IRQ_API
zx_status_t InterruptDispatcher::SendPacket(zx_time_t timestamp) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InterruptDispatcher::UserSignal(zx_time_t timestamp) {

    // TODO(braval): Currently @johngro's driver uses zx_interrupt_trigger
    // to wake up a thread waiting on a physical interrupt
    // Once his driver adopts the interrupt with ports bounded, we can enforce
    // the below condition again
    /*
    if (!(interrupt_.flags & INTERRUPT_VIRTUAL))
        return ZX_ERR_BAD_STATE;
    */

    zx_status_t status;
    AutoSpinLock guard(&spinlock_);
    // only record timestamp if this is the first signal since we started waiting
    if (!timestamp_) {
        timestamp_ = timestamp;
    }
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }

    state_ = InterruptState::TRIGGERED;

    if (is_bound_) {
        status = SendPacket(timestamp);
    } else {
        Signal();
        status = ZX_OK;
    }
    return status;
}
#else
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
#endif

#if ENABLE_NEW_IRQ_API
void InterruptDispatcher::InterruptHandler(bool pci) {
    AutoSpinLock guard(&spinlock_);
    // only record timestamp if this is the first IRQ since we started waiting
    if (!timestamp_) {
        timestamp_ = current_time();
    }
    state_ = InterruptState::TRIGGERED;

    if (!pci && (flags_ & INTERRUPT_MASK_POSTWAIT))
        mask_interrupt(vector_);

    if (is_bound_) {
        SendPacket(timestamp_);
    } else {
        Signal();
    }
}

zx_status_t InterruptDispatcher::Destroy() {
    AutoSpinLock guard(&spinlock_);
    state_ = InterruptState::DESTROYED;
    if (!(flags_ & INTERRUPT_VIRTUAL)) {
        MaskInterrupt(vector_);
        UnregisterInterruptHandler(vector_);
    }
    if (is_bound_) {
        // TODO(braval): Do whatever is needed with the port dispatcher here
        // upon teardown
    } else {
        Signal();
    }
    return ZX_OK;
}
#endif

void InterruptDispatcher::on_zero_handles() {
#if ENABLE_NEW_IRQ_API
    Destroy();
#else
    for (const auto& interrupt : interrupts_) {
        if (!(interrupt.flags & INTERRUPT_VIRTUAL)) {
            MaskInterrupt(interrupt.vector);
            UnregisterInterruptHandler(interrupt.vector);
        }
    }

    Signal(INTERRUPT_CANCEL_MASK, true);
#endif
}
