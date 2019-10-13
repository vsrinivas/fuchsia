// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/irq.h>
#include <zircon/assert.h>

#include <utility>

namespace async {

IrqBase::IrqBase(zx_handle_t object, zx_signals_t trigger, uint32_t options,
                 async_irq_handler_t* handler)
    : irq_{{ASYNC_STATE_INIT}, handler, object} {}

IrqBase::~IrqBase() {
  if (dispatcher_) {
    // Failure to cancel here may result in a dangling pointer...
    zx_status_t status = async_unbind_irq(dispatcher_, &irq_);
    ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
  }
}

zx_status_t IrqBase::Begin(async_dispatcher_t* dispatcher) {
  if (dispatcher_)
    return ZX_ERR_ALREADY_EXISTS;

  dispatcher_ = dispatcher;
  zx_status_t status = async_bind_irq(dispatcher, &irq_);
  if (status != ZX_OK) {
    dispatcher_ = nullptr;
  }
  return status;
}

zx_status_t IrqBase::Cancel() {
  if (!dispatcher_)
    return ZX_ERR_NOT_FOUND;

  async_dispatcher_t* dispatcher = dispatcher_;
  dispatcher_ = nullptr;

  zx_status_t status = async_unbind_irq(dispatcher, &irq_);
  // |dispatcher| is required to be single-threaded, Cancel() is
  // only supposed to be called on |dispatcher|'s thread, and
  // we verified that the wait was pending before calling
  // async_cancel_wait(). Assuming that |dispatcher| never queues
  // a wait, |wait_| must have been pending with |dispatcher|.
  ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
  return status;
}

Irq::Irq(zx_handle_t object, zx_signals_t trigger, uint32_t options, Handler handler)
    : IrqBase(object, trigger, options, &Irq::CallHandler), handler_(std::move(handler)) {}

Irq::~Irq() = default;

void Irq::CallHandler(async_dispatcher_t* dispatcher, async_irq_t* irq, zx_status_t status,
                      const zx_packet_interrupt_t* signal) {
  auto self = Dispatch<Irq>(irq);
  self->handler_(dispatcher, self, status, signal);
}

}  // namespace async
