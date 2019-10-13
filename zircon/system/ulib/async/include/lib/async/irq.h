// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_IRQ_H_
#define LIB_ASYNC_IRQ_H_

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles interrupt.
//
// The |status| is |ZX_OK| if the IRQ was signalled.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
// the task's handler ran or the task was canceled.
typedef void(async_irq_handler_t)(async_dispatcher_t* dispatcher, async_irq_t* irq,
                                  zx_status_t status, const zx_packet_interrupt_t* signal);

// Similar to async_wait, but holds state for an interrupt.
struct async_irq {
  // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
  async_state_t state;

  // The wait's handler function.
  async_irq_handler_t* handler;

  // The object to wait for signals on.
  zx_handle_t object;
};

// Begins asynchronously waiting on an IRQ specified in |irq|.
// Invokes the handler when the wait completes.
// The wait's handler will be invoked exactly once unless the wait is canceled.
// When the dispatcher is shutting down (being destroyed), the handlers of
// all remaining waits will be invoked with a status of |ZX_ERR_CANCELED|.
//
// Returns |ZX_OK| if the wait was successfully begun.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_bind_irq(async_dispatcher_t* dispatcher, async_irq_t* irq);

// Unbinds the IRQ associated with |irq|.
//
// If successful, the IRQ will be unbound from the async loop.
//
// Returns |ZX_OK| if the IRQ has been successfully unbound.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_unbind_irq(async_dispatcher_t* dispatcher, async_irq_t* irq);

__END_CDECLS

#endif  // LIB_ASYNC_IRQ_H_
