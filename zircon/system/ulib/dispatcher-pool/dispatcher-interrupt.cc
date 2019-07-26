// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/timer.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-interrupt.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>

#include <utility>

namespace dispatcher {

// static
fbl::RefPtr<Interrupt> Interrupt::Create() {
  fbl::AllocChecker ac;

  auto ptr = new (&ac) Interrupt();
  if (!ac.check()) {
    return nullptr;
  }

  return fbl::AdoptRef(ptr);
}

zx_status_t Interrupt::Activate(fbl::RefPtr<ExecutionDomain> domain, zx::interrupt irq,
                                ProcessHandler process_handler) {
  if (process_handler == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock obj_lock(&obj_lock_);

  zx_status_t res = ActivateLocked(std::move(irq), std::move(domain));
  if (res != ZX_OK) {
    return res;
  }

  res = WaitOnPortLocked();
  if (res != ZX_OK) {
    InternalDeactivateLocked();
    return res;
  }

  process_handler_ = std::move(process_handler);

  return ZX_OK;
}

void Interrupt::Deactivate() {
  ProcessHandler old_process_handler;

  {
    fbl::AutoLock obj_lock(&obj_lock_);
    InternalDeactivateLocked();

    // If we are in the process of actively dispatching, do not discard our
    // handler just yet.  It is currently being used by the dispatch thread.
    // Instead, wait until the dispatch thread unwinds and allow it to clean
    // up the handler.
    //
    // Otherwise, transfer the handler state into local storage and let it
    // destruct after we have released the object lock.
    if (dispatch_state() != DispatchState::Dispatching) {
      ZX_DEBUG_ASSERT((dispatch_state() == DispatchState::Idle) ||
                      (dispatch_state() == DispatchState::WaitingOnPort));
      old_process_handler = std::move(process_handler_);
    }
  }
}

void Interrupt::Dispatch(ExecutionDomain* domain) {
  ZX_DEBUG_ASSERT(domain != nullptr);
  ZX_DEBUG_ASSERT(process_handler_ != nullptr);
  ZX_DEBUG_ASSERT(pending_pkt_.type == ZX_PKT_TYPE_INTERRUPT);

  zx_status_t res = process_handler_(this, pending_pkt_.interrupt.timestamp);
  ProcessHandler old_process_handler;
  {
    fbl::AutoLock obj_lock(&obj_lock_);
    ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
    dispatch_state_ = DispatchState::Idle;

    // Was there a problem during processing?  If so, make sure that we
    // de-activate ourselves.
    if (res != ZX_OK) {
      InternalDeactivateLocked();
    }

    // Are we still active?  If so, ack the interrupt so that it can produce
    // new messages.
    if (is_active()) {
      res = WaitOnPortLocked();

      if (res != ZX_OK) {
        dispatch_state_ = DispatchState::Idle;
        InternalDeactivateLocked();
      }
    }

    // Have we become deactivated (either during dispatching or just now)?
    // If so, move our process handler state outside of our lock so that it
    // can safely destruct.
    if (!is_active()) {
      old_process_handler = std::move(process_handler_);
    }
  }
}

zx_status_t Interrupt::DoPortWaitLocked() {
  // Interrupt Event Sources are a bit different from other event sources
  // because of the differences in how zircon handles associating a physical
  // interrupt with a port as compared to other handles..
  //
  // Zircon allows an interrupt to be bound to a port once, at which point in
  // time it remains bound to the port until it is destroyed.  There is no way
  // to "unbind" an interrupt from a port without destroying the interrupt
  // object with an explicit call to zx_interrupt_destroy.
  //
  // When a zircon interrupt fires while bound to a port, it posts a message
  // to the port.  It then will not post any further messages to the port
  // until zx_interrupt_ack is explicitly called.
  //
  // So, when an Interrupt event source in the dispatcher-pool framework
  // becomes activated, the first time we "wait-on-port" results in a call to
  // zx_interrupt_bind (via the ThreadPool's BindIrqToPort).  Subsequently,
  // every time that an interrupt fires and is dispatched, we call
  // zx_interrupt_ack as part of unwinding after dispatch is complete,
  // assuming that both execution domain and the interrupt event source are
  // still active.
  //
  // The only time that an interrupt event source is canceled
  // (DoPortCancelLocked) is as a side effect of de-activation, either because
  // the specific event source is being shut down, or because the entire
  // execution domain is being shut down.  If a method were to be introduced
  // to allow users to require manual re-arming of an interrupt event source,
  // new state would need to be introduced to the Interrupt object to allow
  // for this.

  zx_status_t res;
  if (irq_bound_) {
    // If we have already been bound, then ack the interrupt it order to
    // cause it to become re-armed.
    res = zx_interrupt_ack(handle_.get());
  } else {
    // We have not yet been bound, do so now.
    ZX_DEBUG_ASSERT(thread_pool_ != nullptr);
    res = thread_pool_->BindIrqToPort(handle_, reinterpret_cast<uint64_t>(this));
    if (res == ZX_OK) {
      irq_bound_ = true;
    }
  }

  return res;
}

zx_status_t Interrupt::DoPortCancelLocked() { return zx_interrupt_destroy(handle_.get()); }

}  // namespace dispatcher
