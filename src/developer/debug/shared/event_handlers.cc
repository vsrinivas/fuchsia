// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/event_handlers.h"

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>

#include "src/developer/debug/shared/message_loop_async.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {

// SignalHandler ---------------------------------------------------------------

SignalHandler::SignalHandler() = default;
SignalHandler::~SignalHandler() {
  if (!handle_)
    return;

  async_wait_t* wait = handle_.get();
  auto status = async_cancel_wait(async_get_default_dispatcher(), wait);
  FXL_DCHECK(status == ZX_OK) << "Got: " << ZxStatusToString(status);
}

SignalHandler::SignalHandler(SignalHandler&&) = default;
SignalHandler& SignalHandler::operator=(SignalHandler&&) = default;

zx_status_t SignalHandler::Init(int id, zx_handle_t object,
                                zx_signals_t signals) {
  handle_ = std::make_unique<async_wait_t>();
  *handle_ = {};  // Need to zero it out.
  handle_->handler = Handler;
  handle_->object = object;
  handle_->trigger = signals;

  watch_info_id_ = id;
  return WaitForSignals();
}

zx_status_t SignalHandler::WaitForSignals() const {
  async_wait_t* wait = handle_.get();
  zx_status_t status = async_begin_wait(async_get_default_dispatcher(), wait);
  return status;
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

  // async-loop will remove the handler for this event, so we need to re-add it.
  signal_handler.WaitForSignals();
  switch (watch_info->type) {
    case WatchType::kFdio:
      loop->OnFdioSignal(watch_info_id, *watch_info, signal->observed);
      break;
    case WatchType::kSocket:
      loop->OnSocketSignal(watch_info_id, *watch_info, signal->observed);
      break;
    case WatchType::kTask:
      FXL_DCHECK(watch_info_id == kTaskSignalKey);
      loop->CheckAndProcessPendingTasks();
      break;
    case WatchType::kProcessExceptions:
      loop->OnProcessTerminated(*watch_info, signal->observed);
      break;
    case WatchType::kJobExceptions:
      FXL_NOTREACHED();
  }

  // "this" might be deleted at this point, so it should never be used.
}

// ExceptionHandler ------------------------------------------------------------

ExceptionHandler::ExceptionHandler() = default;
ExceptionHandler::~ExceptionHandler() {
  if (!handle_)
    return;

  zx_status_t status = async_unbind_exception_port(
      async_get_default_dispatcher(), handle_.get());
  FXL_DCHECK(status == ZX_OK)
      << "Expected ZX_OK, got " << ZxStatusToString(status);
}

ExceptionHandler::ExceptionHandler(ExceptionHandler&&) = default;
ExceptionHandler& ExceptionHandler::operator=(ExceptionHandler&&) = default;

zx_status_t ExceptionHandler::Init(int id, zx_handle_t object,
                                   uint32_t options) {
  handle_ = std::make_unique<async_exception_t>();
  *handle_ = {};  // Need to zero it out.
  handle_->state = ASYNC_STATE_INIT;
  handle_->handler = Handler;
  handle_->task = object;
  handle_->options = options;

  zx_status_t status =
      async_bind_exception_port(async_get_default_dispatcher(), handle_.get());

  watch_info_id_ = id;
  return status;
}

void ExceptionHandler::Handler(async_dispatcher_t*,
                               async_exception_t* exception, zx_status_t status,
                               const zx_port_packet_t* packet) {
  if (status == ZX_ERR_CANCELED)
    return;

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
