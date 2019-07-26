// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <lib/zx/interrupt.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <dispatcher-pool/dispatcher-event-source.h>

namespace dispatcher {

// class Interrupt
//
// Interrupt is one of the EventSources in the dispatcher framework and is used to
// manage dispatching hardware interrupts received from a zx::interrupt object.
//
// :: Handler ::
//
// Interrupt defines a single handler (ProcessHandler) which runs when the Interrupt has
// become signalled.  The CLOCK_MONOTONIC time of the IRQ signalling will be
// delivered to the user as a parameter to this handle.  Returning an error from
// the process handler will cause the Interrupt to automatically become deactivated.
// Returning ZX_OK will cause the Interrupt to become re-armed.
//
// :: Activation ::
//
// Activation requires a user to provide a valid ExecutionDomain, a
// zx::interrupt,  and a valid ProcessHandler.  The Interrupt object takes ownership
// of the zx::interrupt object.
//
class Interrupt : public EventSource {
 public:
  static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;
  using ProcessHandler =
      fbl::InlineFunction<zx_status_t(Interrupt*, zx_time_t), MAX_HANDLER_CAPTURE_SIZE>;

  static fbl::RefPtr<Interrupt> Create();

  zx_status_t Activate(fbl::RefPtr<ExecutionDomain> domain, zx::interrupt irq,
                       ProcessHandler process_handler);
  virtual void Deactivate() __TA_EXCLUDES(obj_lock_) override;

 protected:
  void Dispatch(ExecutionDomain* domain) __TA_EXCLUDES(obj_lock_) override;

  zx_status_t DoPortWaitLocked() __TA_REQUIRES(obj_lock_) override;
  zx_status_t DoPortCancelLocked() __TA_REQUIRES(obj_lock_) override;

 private:
  friend class fbl::RefPtr<Interrupt>;

  Interrupt() : EventSource(0) {}

  ProcessHandler process_handler_;
  bool irq_bound_ __TA_GUARDED(obj_lock_) = false;
};

}  // namespace dispatcher
