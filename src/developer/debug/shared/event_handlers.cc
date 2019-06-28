// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/event_handlers.h"

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/fit/defer.h>
#include <lib/zx/exception.h>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/zircon_utils.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {

namespace {

// |signals| are the signals we're going to observe.
std::unique_ptr<async_wait_t> CreateSignalHandle(zx_handle_t object, zx_signals_t signals,
                                                 SignalHandlerFunc handler_func) {
  auto handle = std::make_unique<async_wait_t>();
  *handle = {};  // Need to zero it out.
  handle->handler = handler_func;
  handle->object = object;
  handle->trigger = signals;

  return handle;
}

// Sets a particular signal handler to start listening on the async loop.
zx_status_t StartListening(async_wait_t* signal_handle) {
  return async_begin_wait(async_get_default_dispatcher(), signal_handle);
}

}  // namespace

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

zx_status_t SignalHandler::Init(int id, zx_handle_t object, zx_signals_t signals) {
  handle_ = CreateSignalHandle(object, signals, Handler);
  watch_info_id_ = id;

  // We start listening.
  return StartListening(handle_.get());
}

// static
void SignalHandler::Handler(async_dispatcher_t*, async_wait_t* wait, zx_status_t status,
                            const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Got error on receiving exception: " << ZxStatusToString(status);
    FXL_NOTREACHED();
    return;
  }

  auto* loop = MessageLoopTarget::Current();
  FXL_DCHECK(loop);

  // Search for the AsyncHandle that triggered this signal.
  auto handler_it = loop->signal_handlers().find(wait);
  FXL_DCHECK(handler_it != loop->signal_handlers().end());
  const SignalHandler& signal_handler = handler_it->second;

  int watch_info_id = signal_handler.watch_info_id();
  auto* watch_info = loop->FindWatchInfo(watch_info_id);
  FXL_DCHECK(watch_info);

  // async-loop will remove the handler for this event, so we need to re-add it.
  status = StartListening(signal_handler.handle_.get());
  FXL_DCHECK(status == ZX_OK) << "Got: " << ZxStatusToString(status);
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

// ChannelExceptionHandler -----------------------------------------------------

ChannelExceptionHandler::ChannelExceptionHandler() = default;
ChannelExceptionHandler::~ChannelExceptionHandler() {
  if (!handle_)
    return;

  async_wait_t* wait = handle_.get();
  auto status = async_cancel_wait(async_get_default_dispatcher(), wait);
  FXL_DCHECK(status == ZX_OK) << "Got: " << ZxStatusToString(status);
}

ChannelExceptionHandler::ChannelExceptionHandler(ChannelExceptionHandler&&) = default;

ChannelExceptionHandler& ChannelExceptionHandler::operator=(ChannelExceptionHandler&&) = default;

zx_status_t ChannelExceptionHandler::Init(int id, zx_handle_t object, uint32_t options) {
  zx_status_t status =
      zx_task_create_exception_channel(object, options, exception_channel_.reset_and_get_address());
  if (status != ZX_OK)
    return status;

  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  handle_ = CreateSignalHandle(exception_channel_.get(), signals, Handler);
  watch_info_id_ = id;
  return StartListening(handle_.get());
}

// static
void ChannelExceptionHandler::Handler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Got error on receiving exception: " << ZxStatusToString(status);
    FXL_NOTREACHED();
    return;
  }

  auto* loop = MessageLoopTarget::Current();
  FXL_DCHECK(loop);

  // Search for the AsyncHandle that triggered this signal.
  auto handler_it = loop->channel_exception_handlers().find(wait);
  FXL_DCHECK(handler_it != loop->channel_exception_handlers().end());
  const ChannelExceptionHandler& handler = handler_it->second;

  auto cleanup = fit::defer([handle = handler.handle_.get()] {
    zx_status_t status = StartListening(handle);
    FXL_DCHECK(status == ZX_OK) << "Got: " << ZxStatusToString(status);
  });

  int watch_info_id = handler.watch_info_id();
  auto* watch_info = loop->FindWatchInfo(watch_info_id);
  FXL_DCHECK(watch_info);

  // async-loop will remove the handler for this event, so we need to re-add it.
  // We should only receive exceptions here.
  if (watch_info->type != WatchType::kProcessExceptions &&
      watch_info->type != WatchType::kJobExceptions) {
    FXL_NOTREACHED() << "Should only watch for exceptions on this handler.";
    return;
  }

  bool peer_closed = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  bool readable = signal->observed & ZX_CHANNEL_READABLE;

  FXL_DCHECK(peer_closed || readable);

  if (peer_closed)
    return;

  // We obtain the exception from the channel.
  zx::exception exception;
  zx_exception_info_t exception_info;
  status = handler.exception_channel_.read(0, &exception_info, exception.reset_and_get_address(),
                                           sizeof(exception_info), 1, nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Got error when reading from exception channel: "
                     << ZxStatusToString(status);
    FXL_NOTREACHED();
    return;
  }

  loop->HandleChannelException(handler, std::move(exception), exception_info);
}

}  // namespace debug_ipc
