// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/async_transaction.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/trace.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {
namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, const zx::unowned_channel& channel)
    : async_wait_t({{ASYNC_STATE_INIT},
                    &AsyncBinding::OnMessage,
                    channel->get(),
                    ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                    0}),
      dispatcher_(dispatcher) {
  ZX_ASSERT(dispatcher_);
  ZX_ASSERT(handle() != ZX_HANDLE_INVALID);
}

void AsyncBinding::OnUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info,
                            bool is_unbind_task) {
  ZX_DEBUG_ASSERT(calling_ref.get() == this);
  auto binding = std::move(calling_ref);  // Move calling_ref into this scope.

  {
    std::scoped_lock lock(lock_);

    // Only one thread should wait for unbind.
    if (sync_unbind_)
      return;
    if (is_unbind_task) {
      // If the async_cancel_wait() in UnbindInternal() failed, another dispatcher thread has
      // access to keep_alive_ and may already be waiting on other references to be released.
      if (begun_ && !canceled_)
        return;
      // No other thread will touch the internal reference.
      keep_alive_ = nullptr;
    }
    sync_unbind_ = true;

    // If the unbind info was already set, respect the existing.
    if (unbind_info_.has_value()) {
      info = *unbind_info_;
    } else {
      unbind_info_ = info;
    }
  }

  FinishUnbind(std::move(binding), info);
}

void AsyncBinding::MessageHandler(zx_status_t dispatcher_status, const zx_packet_signal_t* signal) {
  ZX_ASSERT(keep_alive_);

  if (dispatcher_status != ZX_OK)
    return OnUnbind(std::move(keep_alive_), UnbindInfo::DispatcherError(dispatcher_status));

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint64_t i = 0; i < signal->count; i++) {
      fidl_trace(WillLLCPPAsyncChannelRead);
      IncomingMessage msg = fidl::ChannelReadEtc(
          handle(), 0, fidl::BufferSpan(bytes, std::size(bytes)), cpp20::span(handles));
      if (!msg.ok()) {
        return OnUnbind(std::move(keep_alive_), fidl::UnbindInfo{msg});
      }
      fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes, msg.byte_actual(),
                 msg.handle_actual());

      // Flag indicating whether this thread still has access to the binding.
      bool binding_released = false;
      // Dispatch the message.
      std::optional<fidl::UnbindInfo> maybe_unbind = Dispatch(msg, &binding_released);

      // If binding_released is not set, keep_alive_ is still valid and this thread will continue to
      // read messages on this binding.
      if (binding_released)
        return;
      ZX_ASSERT(keep_alive_);

      // If there was any error enabling dispatch or an unexpected message, destroy the binding.
      if (maybe_unbind) {
        return OnUnbind(std::move(keep_alive_), *maybe_unbind);
      }
    }

    // Add the wait back to the dispatcher.
    // NOTE: If EnableNextDispatch() fails due to a dispatcher error, unbind_info_ will override the
    // arguments passed to OnUnbind().
    if (EnableNextDispatch() != ZX_OK)
      OnUnbind(std::move(keep_alive_), UnbindInfo::Unbind());
  } else {
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    OnUnbind(std::move(keep_alive_), UnbindInfo::PeerClosed(ZX_ERR_PEER_CLOSED));
  }
}

void AsyncBinding::BeginWait() {
  zx_status_t status;
  {
    std::scoped_lock lock(lock_);
    ZX_ASSERT(!begun_);
    status = async_begin_wait(dispatcher_, this);
    if (status == ZX_OK) {
      begun_ = true;
      return;
    }
  }

  // If the first |async_begin_wait| failed, attempt to report the error through
  // the |on_unbound| handler - the interface was effectively unbound
  // immediately on first dispatch.
  //
  // There are two possible error cases:
  //
  // - The server endpoint does not have the |ZX_RIGHT_WAIT| right. Since the
  //   server endpoint may be of foreign origin, asynchronously report the error
  //   through the |on_unbound| handler.
  //
  // - The dispatcher does not support waiting on a port, or was shutdown. This
  //   is a programming error. The user code should either switch to a
  //   supporting dispatcher, or properly implement teardown by not shutting
  //   down the event loop until all current incoming events have been
  //   processed.
  //
  using Result = AsyncBinding::UnboundNotificationPostingResult;
  Result result = InternalError(std::shared_ptr(keep_alive_), UnbindInfo::DispatcherError(status));
  switch (result) {
    case Result::kDispatcherError:
      // We are crashing the process anyways, but clearing |keep_alive_| helps
      // death-tests pass the leak-sanitizer.
      keep_alive_ = nullptr;
      ZX_PANIC(
          "When binding FIDL connection: "
          "dispatcher was shutdown, or unsupported dispatcher.");
    case Result::kRacedWithInProgressUnbind:
      // Should never happen - the binding was only just created.
      __builtin_unreachable();
    case Result::kOk:
      return;
  }
}

zx_status_t AsyncBinding::EnableNextDispatch() {
  std::scoped_lock lock(lock_);
  if (unbind_info_.has_value())
    return ZX_ERR_CANCELED;
  zx_status_t status = async_begin_wait(dispatcher_, this);
  if (status != ZX_OK)
    unbind_info_ = fidl::UnbindInfo::DispatcherError(status);
  return status;
}

auto AsyncBinding::UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info)
    -> UnboundNotificationPostingResult {
  ZX_ASSERT(calling_ref);
  // Move the calling reference into this scope.
  auto binding = std::move(calling_ref);

  std::scoped_lock lock(lock_);
  // Another thread has entered this critical section already via Unbind(), Close(), or
  // OnUnbind(). Release our reference and return to unblock that caller.
  if (unbind_info_.has_value())
    return UnboundNotificationPostingResult::kRacedWithInProgressUnbind;
  // Indicate that waits should no longer be added to the dispatcher.
  // Store the reason for unbinding.
  unbind_info_ = info;

  // Attempt to add a task to unbind the channel. On failure, the dispatcher was shutdown;
  // if another thread was monitoring incoming messages, that thread would do the unbinding.
  auto* unbind_task = new UnbindTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnbindTask, async_now(dispatcher_)},
      .binding = binding,
  };
  if (async_post_task(dispatcher_, &unbind_task->task) != ZX_OK) {
    delete unbind_task;
    return UnboundNotificationPostingResult::kDispatcherError;
  }

  // Attempt to cancel the current wait. On failure, a dispatcher thread (possibly this thread)
  // will invoke OnUnbind() before returning to the dispatcher.
  canceled_ = async_cancel_wait(dispatcher_, this) == ZX_OK;
  return UnboundNotificationPostingResult::kOk;
}

std::optional<UnbindInfo> AnyAsyncServerBinding::Dispatch(fidl::IncomingMessage& msg,
                                                          bool* binding_released) {
  auto* hdr = msg.header();
  AsyncTransaction txn(hdr->txid, binding_released);
  return txn.Dispatch(std::move(keep_alive_), std::move(msg));
}

std::shared_ptr<AsyncClientBinding> AsyncClientBinding::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<ChannelRef> channel,
    std::shared_ptr<ClientBase> client, std::shared_ptr<AsyncEventHandler>&& event_handler) {
  auto ret = std::shared_ptr<AsyncClientBinding>(new AsyncClientBinding(
      dispatcher, std::move(channel), std::move(client), std::move(event_handler)));
  ret->keep_alive_ = ret;  // Keep the binding alive until an unbind operation or channel error.
  return ret;
}

AsyncClientBinding::AsyncClientBinding(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<ChannelRef> channel,
                                       std::shared_ptr<ClientBase> client,
                                       std::shared_ptr<AsyncEventHandler>&& event_handler)
    : AsyncBinding(dispatcher, zx::unowned_channel(channel->handle())),
      channel_(std::move(channel)),
      client_(std::move(client)),
      event_handler_(std::move(event_handler)) {}

std::optional<UnbindInfo> AsyncClientBinding::Dispatch(fidl::IncomingMessage& msg, bool*) {
  return client_->Dispatch(msg, event_handler_.get());
}

void AsyncClientBinding::FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref,
                                      UnbindInfo info) {
  // Move binding into scope.
  std::shared_ptr<AsyncBinding> binding = std::move(calling_ref);

  // Stash state required after deleting the binding.
  std::shared_ptr<AsyncEventHandler> event_handler = std::move(event_handler_);
  std::shared_ptr<ClientBase> client = std::move(client_);

  // Delete the calling reference. Transient references don't access the channel, so don't wait.
  binding = nullptr;
  // `this` is no longer valid.

  // Outstanding async responses will no longer be received, so release the contexts.
  client->ReleaseResponseContextsWithError();
  client = nullptr;

  // Execute the error hook if specified.
  if (info.reason() != fidl::Reason::kUnbind) {
    if (event_handler != nullptr)
      event_handler->on_fidl_error(info);
  }

  // Execute the unbound hook if specified.
  if (event_handler != nullptr)
    event_handler->Unbound(info);
}

}  // namespace internal
}  // namespace fidl
