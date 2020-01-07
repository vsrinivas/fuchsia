// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/interrupt_dispatcher.h"

#include <platform.h>
#include <zircon/syscalls/port.h>

#include <dev/interrupt.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>

InterruptDispatcher::InterruptDispatcher() : timestamp_(0), state_(InterruptState::IDLE) {
  event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

zx_status_t InterruptDispatcher::WaitForInterrupt(zx_time_t* out_timestamp) {
  bool defer_unmask = false;
  while (true) {
    {
      Guard<SpinLock, IrqSave> guard{&spinlock_};
      if (port_dispatcher_ || HasVcpu()) {
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
          } else if (flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED) {
            defer_unmask = true;
          }
          break;
        case InterruptState::IDLE:
          break;
        default:
          return ZX_ERR_BAD_STATE;
      }
      state_ = InterruptState::WAITING;
    }

    if (defer_unmask) {
      UnmaskInterrupt();
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
  if (!(flags_ & INTERRUPT_VIRTUAL))
    return ZX_ERR_BAD_STATE;

  // Using AutoReschedDisable is necessary for correctness to prevent
  // context-switching to the woken thread while holding spinlock_.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
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
  // Using AutoReschedDisable is not necessary for correctness, since we should
  // be in an interrupt context with preemption disabled, but we re-disable anyway
  // for clarity and robustness.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
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
  // The interrupt may presently have been fired and we could already be about to acquire the
  // spinlock_ in InterruptHandler. If we were to call UnregisterInterruptHandler whilst holding
  // the spinlock_ then we risk a deadlock scenario where the platform interrupt code may have
  // taken a lock to call InterruptHandler, and it might take the same lock when we call
  // UnregisterInterruptHandler.
  MaskInterrupt();
  UnregisterInterruptHandler();

  // Using AutoReschedDisable is necessary for correctness to prevent
  // context-switching to the woken thread while holding spinlock_.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
  Guard<SpinLock, IrqSave> guard{&spinlock_};

  if (port_dispatcher_) {
    bool packet_was_in_queue = port_dispatcher_->RemoveInterruptPacket(&port_packet_);
    if ((state_ == InterruptState::NEEDACK) && !packet_was_in_queue) {
      state_ = InterruptState::DESTROYED;
      return ZX_ERR_NOT_FOUND;
    }
    if ((state_ == InterruptState::IDLE) ||
        ((state_ == InterruptState::NEEDACK) && packet_was_in_queue)) {
      state_ = InterruptState::DESTROYED;
      return ZX_OK;
    }
  } else {
    state_ = InterruptState::DESTROYED;
    Signal();
  }
  return ZX_OK;
}

zx_status_t InterruptDispatcher::Bind(fbl::RefPtr<PortDispatcher> port_dispatcher, uint64_t key) {
  Guard<SpinLock, IrqSave> guard{&spinlock_};
  if (state_ == InterruptState::DESTROYED) {
    return ZX_ERR_CANCELED;
  } else if (state_ == InterruptState::WAITING) {
    return ZX_ERR_BAD_STATE;
  } else if (port_dispatcher_ || HasVcpu()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  // If an interrupt is bound to a port there is a conflict between UNMASK_PREWAIT_UNLOCKED
  // and MASK_POSTWAIT because the mask operation will by necessity happen before leaving the
  // dispatcher spinlock, leading to a mask operation immediately followed by the deferred
  // unmask operation.
  if ((flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED) && (flags_ & INTERRUPT_MASK_POSTWAIT)) {
    return ZX_ERR_INVALID_ARGS;
  }

  port_dispatcher_ = ktl::move(port_dispatcher);
  port_packet_.key = key;
  return ZX_OK;
}

zx_status_t InterruptDispatcher::Unbind(fbl::RefPtr<PortDispatcher> port_dispatcher) {
  // Moving port_dispatcher_ to the local variable ensures it will not be destroyed while
  // holding this spinlock.
  fbl::RefPtr<PortDispatcher> dispatcher;
  {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    if (port_dispatcher_ != port_dispatcher) {
      // This case also covers the HasVcpu() case.
      return ZX_ERR_NOT_FOUND;
    }
    if (state_ == InterruptState::DESTROYED) {
      return ZX_ERR_CANCELED;
    }
    // Remove the packet for this interrupt from this port on an unbind before actually
    // doing the unbind. This protects against the case where the interrupt dispatcher
    // goes away between an unbind and a port_wait.
    port_dispatcher_->RemoveInterruptPacket(&port_packet_);
    port_packet_.key = 0;
    dispatcher.swap(port_dispatcher_);
  }
  return ZX_OK;
}

zx_status_t InterruptDispatcher::Ack() {
  bool defer_unmask = false;
  // Using AutoReschedDisable is necessary for correctness to prevent
  // context-switching to the woken thread while holding spinlock_.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
  {
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
      } else if (flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED) {
        defer_unmask = true;
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
  }

  if (defer_unmask) {
    UnmaskInterrupt();
  }
  return ZX_OK;
}

zx_status_t InterruptDispatcher::set_flags(uint32_t flags) {
  if ((flags & INTERRUPT_UNMASK_PREWAIT) && (flags & INTERRUPT_UNMASK_PREWAIT_UNLOCKED)) {
    return ZX_ERR_INVALID_ARGS;
  }
  flags_ = flags;
  return ZX_OK;
}

void InterruptDispatcher::on_zero_handles() { Destroy(); }
