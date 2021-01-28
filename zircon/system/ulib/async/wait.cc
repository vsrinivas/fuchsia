// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait.h>
#include <zircon/assert.h>

#include <utility>

namespace async {

WaitBase::WaitBase(zx_handle_t object, zx_signals_t trigger, uint32_t options,
                   async_wait_handler_t* handler)
    : wait_{{ASYNC_STATE_INIT}, handler, object, trigger, options} {}

WaitBase::~WaitBase() {
  // Sub-classes must Cancel() in the sub-class destructor, before ~WaitBase runs.  This rule allows
  // a sub-class member such as handler_ to be what keeps object alive.
  //
  // Here's a sequence illustrating why:
  //   * Client code captures an instance of zx::some_object in a lambda which is assigned as a
  //     handler_ (sub-class member).
  //   * Client code starts a wait operation with this WaitBase sub-class instance, passing the raw
  //     zx_handle_t from the zx::some_object.  WaitBase now has a copy of this raw handle in
  //     wait_.object.
  //   * The WaitBase sub-class instance now starts to destruct.
  //   * After the sub-class destructor has run, but before ~WaitBase starts, the members of the
  //     sub-class destruct.
  //   * When handler_ destructs, the lambda destructs, and all of its captures destruct, including
  //     zx::some_object, which closes the handle.
  //   * Next, when ~WaitBase runs, the raw stashed copy of the handle is no longer valid, as it has
  //     already been closed.
  //   * Because the handle has already been closed by sub-class destruction code, any use of the
  //     handle in ~WaitBase would be a use-after-free, which can result in process termination
  //     (assuming configured policy on use of invalid handle is to terminate, and assuming the
  //     handle value hasn't been re-used already).
  ZX_DEBUG_ASSERT(!dispatcher_);
}

zx_status_t WaitBase::Begin(async_dispatcher_t* dispatcher) {
  if (dispatcher_)
    return ZX_ERR_ALREADY_EXISTS;

  dispatcher_ = dispatcher;
  zx_status_t status = async_begin_wait(dispatcher, &wait_);
  if (status != ZX_OK) {
    dispatcher_ = nullptr;
  }
  return status;
}

zx_status_t WaitBase::Cancel() {
  if (!dispatcher_)
    return ZX_ERR_NOT_FOUND;

  async_dispatcher_t* dispatcher = dispatcher_;
  dispatcher_ = nullptr;

  zx_status_t status = async_cancel_wait(dispatcher, &wait_);
  // |dispatcher| is required to be single-threaded, Cancel() is
  // only supposed to be called on |dispatcher|'s thread, and
  // we verified that the wait was pending before calling
  // async_cancel_wait(). Assuming that |dispatcher| never queues
  // a wait, |wait_| must have been pending with |dispatcher|.
  ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
  return status;
}

Wait::Wait(zx_handle_t object, zx_signals_t trigger, uint32_t options, Handler handler)
    : WaitBase(object, trigger, options, &Wait::CallHandler), handler_(std::move(handler)) {}

Wait::~Wait() {
  // See comment in WaitBase::~WaitBase re. why Cancel() happens in sub-class.
  (void)Cancel();
  ZX_DEBUG_ASSERT(!is_pending());
  // ~handler_
  // ~WaitBase
}

void Wait::CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
  auto self = Dispatch<Wait>(wait);
  self->handler_(dispatcher, self, status, signal);
}

WaitOnce::WaitOnce(zx_handle_t object, zx_signals_t trigger, uint32_t options)
    : WaitBase(object, trigger, options, &WaitOnce::CallHandler) {}

WaitOnce::~WaitOnce() {
  // See comment in WaitBase::~WaitBase re. why Cancel() happens in sub-class.
  (void)Cancel();
  ZX_DEBUG_ASSERT(!is_pending());
  // ~handler_
  // ~WaitBase
}

zx_status_t WaitOnce::Begin(async_dispatcher_t* dispatcher, WaitOnce::Handler handler) {
  // Don't overwrite handler_ until we know that WaitBase::Begin() succeeds, as overwriting handler
  // could otherwise delete the object we're already waiting on.  We expect the Begin() to happen on
  // the same thread as any wait completions.
  zx_status_t status = WaitBase::Begin(dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  handler_ = std::move(handler);
  return ZX_OK;
}

void WaitOnce::CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                           const zx_packet_signal_t* signal) {
  auto self = Dispatch<WaitOnce>(wait);
  // Move the handler to the stack prior to calling.
  auto handler = std::move(self->handler_);
  handler(dispatcher, self, status, signal);
}

}  // namespace async
