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
    : timestamp_(0), state_(InterruptState::IDLE) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

zx_status_t InterruptDispatcher::WaitForInterrupt(zx_time_t* out_timestamp) {
    while (true) {
        {
            Guard<SpinLock, IrqSave> guard{&spinlock_};
            if (port_dispatcher_) {
                return ZX_ERR_BAD_STATE;
            }
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
                    UnmaskInterrupt();
                }
                break;
            case InterruptState::IDLE:
                break;
            default:
                return ZX_ERR_BAD_STATE;
            }
            state_ = InterruptState::WAITING;
        }

        {
            ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::INTERRUPT);
            zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
            if (status != ZX_OK) {
                // The event_wait call was interrupted and we need to retry
                // but before we retry we will set the interrupt state
                // back to IDLE if we are still in the WAITING state
                Guard<SpinLock, IrqSave> guard{&spinlock_};
                if (state_ == InterruptState::WAITING) {
                    state_ = InterruptState::IDLE;
                }
                return status;
            }
        }
    }
}

bool InterruptDispatcher::SendPacketLocked(zx_time_t timestamp) {
    bool status = port_dispatcher_->QueueInterruptPacket(&port_packet_, timestamp);
    if (flags_ & INTERRUPT_MASK_POSTWAIT) {
        MaskInterrupt();
    }
    timestamp_ = 0;
    return status;
}

zx_status_t InterruptDispatcher::Trigger(zx_time_t timestamp) {

    // TODO(braval): Currently @johngro's driver uses zx_interrupt_trigger
    // to wake up a thread waiting on a physical interrupt
    // Once his driver adopts the interrupt with ports bounded, we can enforce
    // the below condition again
    /*
    if (!(interrupt_.flags & INTERRUPT_VIRTUAL))
        return ZX_ERR_BAD_STATE;
    */

    Guard<SpinLock, IrqSave> guard{&spinlock_};
    // only record timestamp if this is the first signal since we started waiting
    if (!timestamp_) {
        timestamp_ = timestamp;
    }
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }
    if (state_ == InterruptState::NEEDACK && port_dispatcher_) {
        // Cannot trigger a interrupt without ACK
        // only record timestamp if this is the first signal since we started waiting
        return ZX_OK;
    }

    if (port_dispatcher_) {
        SendPacketLocked(timestamp);
        state_ = InterruptState::NEEDACK;
    } else {
        Signal();
        state_ = InterruptState::TRIGGERED;
    }
    return ZX_OK;
}

void InterruptDispatcher::InterruptHandler() {
    Guard<SpinLock, IrqSave> guard{&spinlock_};

    // only record timestamp if this is the first IRQ since we started waiting
    if (!timestamp_) {
        timestamp_ = current_time();
    }
    if (state_ == InterruptState::NEEDACK && port_dispatcher_) {
        return;
    }
    if (port_dispatcher_) {
        SendPacketLocked(timestamp_);
        state_ = InterruptState::NEEDACK;
    } else {
        if (flags_ & INTERRUPT_MASK_POSTWAIT) {
            MaskInterrupt();
        }
        Signal();
        state_ = InterruptState::TRIGGERED;
    }
}

zx_status_t InterruptDispatcher::Destroy() {
    Guard<SpinLock, IrqSave> guard{&spinlock_};

    MaskInterrupt();
    UnregisterInterruptHandler();

    if (port_dispatcher_) {
        bool packet_was_in_queue = port_dispatcher_->RemoveInterruptPacket(&port_packet_);
        if ((state_ == InterruptState::NEEDACK) &&
            !packet_was_in_queue) {
            state_ = InterruptState::DESTROYED;
            return ZX_ERR_NOT_FOUND;
        }
        if ((state_ == InterruptState::IDLE) ||
            ((state_ == InterruptState::NEEDACK) &&
             packet_was_in_queue)) {
            state_ = InterruptState::DESTROYED;
            return ZX_OK;
        }
    } else {
        state_ = InterruptState::DESTROYED;
        Signal();
    }
    return ZX_OK;
}

zx_status_t InterruptDispatcher::Bind(fbl::RefPtr<PortDispatcher> port_dispatcher,
                                      fbl::RefPtr<InterruptDispatcher> interrupt, uint64_t key) {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }
    if (port_dispatcher_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    if (state_ == InterruptState::WAITING) {
        return ZX_ERR_BAD_STATE;
    }

    port_dispatcher_ = fbl::move(port_dispatcher);
    port_packet_.key = key;
    return ZX_OK;
}

zx_status_t InterruptDispatcher::Ack() {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    if (port_dispatcher_ == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }
    if (state_ == InterruptState::NEEDACK) {
        if (flags_ & INTERRUPT_UNMASK_PREWAIT) {
            UnmaskInterrupt();
        }
        if (timestamp_) {
            if (!SendPacketLocked(timestamp_)) {
                // We cannot queue another packet here.
                // If we reach here it means that the
                // interrupt packet has not been processed,
                // another interrupt has occurred & then the
                // interrupt was ACK'd
                return ZX_ERR_BAD_STATE;
            }
        } else {
            state_ = InterruptState::IDLE;
        }
    }
    return ZX_OK;
}

void InterruptDispatcher::on_zero_handles() {
    Destroy();
}
