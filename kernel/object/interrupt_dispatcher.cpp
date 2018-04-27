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

InterruptDispatcher::InterruptDispatcher()
    : timestamp_(0), state_(InterruptState::IDLE), is_bound_(false) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

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

void InterruptDispatcher::on_zero_handles() {
    Destroy();
}
