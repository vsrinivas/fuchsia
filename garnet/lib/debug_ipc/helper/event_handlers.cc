// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/event_handlers.h"

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"

#include "garnet/lib/debug_ipc/helper/message_loop_async.h"
#include "garnet/lib/debug_ipc/helper/zx_status.h"

namespace debug_ipc {

// SignalHandler ---------------------------------------------------------------

SignalHandler::SignalHandler() = default;
SignalHandler::SignalHandler(int id, zx_handle_t object, zx_signals_t signals)
    : watch_info_id_(id), handle_(std::make_unique<async_wait_t>()) {
  *handle_ = {};  // Need to zero it out.
  handle_->handler = Handler;
  handle_->object = object;
  handle_->trigger = signals;

  WaitForSignals();
}

SignalHandler::~SignalHandler() {
  if (handle_) {
    async_wait_t* wait = handle_.get();

    auto status = async_cancel_wait(async_get_default_dispatcher(), wait);
    FXL_DCHECK(status == ZX_OK) << "Got: " << ZxStatusToString(status);
  }
}

SignalHandler::SignalHandler(SignalHandler&&) = default;
SignalHandler& SignalHandler::operator=(SignalHandler&&) = default;

void SignalHandler::WaitForSignals() const {
  async_wait_t* wait = handle_.get();
  zx_status_t status = async_begin_wait(async_get_default_dispatcher(), wait);
  FXL_DCHECK(status == ZX_OK);
}

void SignalHandler::Handler(async_dispatcher_t*, async_wait_t* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal) {
  FXL_DCHECK(status == ZX_OK);

  auto* loop = MessageLoopAsync::Current();
  FXL_DCHECK(loop);

  // Search for the AsyncHandle that triggered this signal.
  auto handler_it = loop->signal_handlers().find(wait);
  FXL_DCHECK(handler_it != loop->signal_handlers().end());
  const SignalHandler& signal_handler = handler_it->second;

  int watch_info_id = signal_handler.watch_info_id();
  auto* watch_info = loop->FindWatchInfo(watch_info_id);
  FXL_DCHECK(watch_info);

  switch (watch_info->type) {
    case WatchType::kFdio:
      signal_handler.WaitForSignals();
      loop->OnFdioSignal(watch_info_id, *watch_info, signal->observed);
      break;
    case WatchType::kSocket:
      signal_handler.WaitForSignals();
      loop->OnSocketSignal(watch_info_id, *watch_info, signal->observed);
      break;
    case WatchType::kTask:
      FXL_DCHECK(watch_info_id == kTaskSignalKey);
      signal_handler.WaitForSignals();
      loop->CheckAndProcessPendingTasks();
      break;
    case WatchType::kProcessExceptions:
      loop->OnProcessTerminated(*watch_info, signal->observed);
      break;
    case WatchType::kJobExceptions:
      FXL_NOTREACHED();
  }
}

// ExceptionHandler ------------------------------------------------------------

ExceptionHandler::ExceptionHandler() = default;
ExceptionHandler::ExceptionHandler(int id, zx_handle_t object, uint32_t options)
    : watch_info_id_(id), handle_(std::make_unique<async_exception_t>()) {
  *handle_ = {};  // Need to zero it out.
  handle_->state = ASYNC_STATE_INIT;
  handle_->handler = Handler;
  handle_->task = object;
  handle_->options = options;

  zx_status_t status =
      async_bind_exception_port(async_get_default_dispatcher(), handle_.get());
  FXL_DCHECK(status == ZX_OK)
      << "Expected ZX_OK, got " << ZxStatusToString(status);
}

ExceptionHandler::~ExceptionHandler() {
  if (handle_) {
    async_unbind_exception_port(async_get_default_dispatcher(), handle_.get());
  }
}

// Copy constructors are implicitly deleted by unique_ptr
ExceptionHandler::ExceptionHandler(ExceptionHandler&&) = default;
ExceptionHandler& ExceptionHandler::operator=(ExceptionHandler&&) = default;

void ExceptionHandler::Handler(async_dispatcher_t*,
                               async_exception_t* exception, zx_status_t status,
                               const zx_port_packet_t* packet) {
  if (status == ZX_ERR_CANCELED) {
    return;
  }

  FXL_DCHECK(status == ZX_OK)
      << "Unexpected status: " << ZxStatusToString(status);

  auto* loop = MessageLoopAsync::Current();
  FXL_DCHECK(loop);

  // We search for the AsyncHandle that triggered this signal.
  auto handler_it = loop->exception_handlers().find(exception);
  FXL_DCHECK(handler_it != loop->exception_handlers().end());
  const ExceptionHandler& exception_handler = handler_it->second;

  // We copy the packet in order to pass in the key.
  auto new_packet = *packet;
  new_packet.key = exception_handler.watch_info_id_;
  loop->HandleException(exception_handler, std::move(new_packet));
}

}  // namespace debug_ipc
