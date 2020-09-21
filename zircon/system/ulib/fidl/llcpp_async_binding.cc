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

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, zx::unowned_channel channel)
    : async_wait_t({{ASYNC_STATE_INIT},
                    &AsyncBinding::OnMessage,
                    channel->get(),
                    ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                    0}),
      dispatcher_(dispatcher) {
  ZX_ASSERT(dispatcher_);
  ZX_ASSERT(handle() != ZX_HANDLE_INVALID);
}

AsyncBinding::~AsyncBinding() {
  if (on_delete_)
    sync_completion_signal(on_delete_);
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
      if (!canceled_)
        return;
      // No other thread will touch the internal reference.
      keep_alive_ = nullptr;
    }
    unbind_ = sync_unbind_ = true;

    // If the peer was not closed, and the user invoked Close() or there was a dispatch error,
    // overwrite the unbound reason and recover the epitaph or error status. Note that
    // UnbindInfo::kUnbind is simply the default value for unbind_info_.reason.
    if (info.reason != UnbindInfo::kPeerClosed &&
        unbind_info_.reason != UnbindInfo::kUnbind) {
      info = unbind_info_;
    }
  }

  FinishUnbind(std::move(binding), info);
}

void AsyncBinding::MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) {
  ZX_ASSERT(keep_alive_);

  if (status != ZX_OK)
    return OnUnbind(std::move(keep_alive_), {UnbindInfo::kDispatcherError, status});

  if (signal->observed & ZX_CHANNEL_READABLE) {
    char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint64_t i = 0; i < signal->count; i++) {
      fidl_msg_t msg = {
          .bytes = bytes,
          .handles = handles,
          .num_bytes = 0u,
          .num_handles = 0u,
      };
      fidl_trace(WillLLCPPAsyncChannelRead);
      status = zx_channel_read(handle(), 0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                               ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
      if (status != ZX_OK)
        return OnUnbind(std::move(keep_alive_), {UnbindInfo::kChannelError, status});

      // Do basic validation on the message.
      status = msg.num_bytes < sizeof(fidl_message_header_t)
          ? ZX_ERR_INVALID_ARGS
          : fidl_validate_txn_header(reinterpret_cast<fidl_message_header_t*>(msg.bytes));
      if (status != ZX_OK) {
        zx_handle_close_many(msg.handles, msg.num_handles);
        return OnUnbind(std::move(keep_alive_), {UnbindInfo::kUnexpectedMessage, status});
      }
      fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes, msg.num_bytes,
                 msg.num_handles);

      // Flag indicating whether this thread still has access to the binding.
      bool binding_released = false;
      // Dispatch the message.
      auto maybe_unbind = Dispatch(&msg, &binding_released);

      // If binding_released is not set, keep_alive_ is still valid and this thread will continue to
      // read messages on this binding.
      if (binding_released)
        return;
      ZX_ASSERT(keep_alive_);

      // If there was any error enabling dispatch or an unexpected message, destroy the binding.
      if (maybe_unbind) {
        if (maybe_unbind->status == ZX_ERR_PEER_CLOSED)
          maybe_unbind->reason = UnbindInfo::kPeerClosed;
        return OnUnbind(std::move(keep_alive_), *maybe_unbind);
      }
    }

    // Add the wait back to the dispatcher.
    // NOTE: If EnableNextDispatch() fails due to a dispatcher error, unbind_info_ will override the
    // arguments passed to OnUnbind().
    if (EnableNextDispatch() != ZX_OK)
      OnUnbind(std::move(keep_alive_), {UnbindInfo::kUnbind, ZX_OK});
  } else {
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    OnUnbind(std::move(keep_alive_), {UnbindInfo::kPeerClosed, ZX_ERR_PEER_CLOSED});
  }
}

zx_status_t AsyncBinding::BeginWait() {
  std::scoped_lock lock(lock_);
  ZX_ASSERT(!begun_);
  auto status = async_begin_wait(dispatcher_, this);
  // On error, release the internal reference so it can be destroyed.
  if (status != ZX_OK) {
    keep_alive_ = nullptr;
    return status;
  }
  begun_ = true;
  return ZX_OK;
}

zx_status_t AsyncBinding::EnableNextDispatch() {
  std::scoped_lock lock(lock_);
  if (unbind_)
    return ZX_ERR_CANCELED;
  auto status = async_begin_wait(dispatcher_, this);
  if (status != ZX_OK && unbind_info_.status == ZX_OK)
    unbind_info_ = {UnbindInfo::kDispatcherError, status};
  return status;
}

void AsyncBinding::UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) {
  ZX_ASSERT(calling_ref);
  // Move the calling reference into this scope.
  auto binding = std::move(calling_ref);

  std::scoped_lock lock(lock_);
  // Another thread has entered this critical section already via Unbind(), Close(), or
  // OnUnbind(). Release our reference and return to unblock that caller.
  if (unbind_)
    return;
  unbind_ = true;  // Indicate that waits should no longer be added to the dispatcher.
  unbind_info_ = info;  // Store the reason for unbinding.

  // Attempt to add a task to unbind the channel. On failure, the dispatcher was shutdown,
  // and another thread will do the unbinding.
  auto* unbind_task = new UnbindTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnbindTask, async_now(dispatcher_)},
      .binding = binding,
  };
  if (async_post_task(dispatcher_, &unbind_task->task) != ZX_OK) {
    delete unbind_task;
    return;
  }

  // Attempt to cancel the current wait. On failure, a dispatcher thread (possibly this thread)
  // will invoke OnUnbind() before returning to the dispatcher.
  canceled_ = async_cancel_wait(dispatcher_, this) == ZX_OK;
}

std::shared_ptr<AsyncServerBinding> AsyncServerBinding::Create(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn) {
  auto ret = std::shared_ptr<AsyncServerBinding>(new AsyncServerBinding(
      dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_unbound_fn)));
  ret->keep_alive_ = ret;  // We keep the binding alive until somebody decides to close the channel.
  return ret;
}

AsyncServerBinding::AsyncServerBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn)
    : AsyncBinding(dispatcher, channel.borrow()),
      channel_(std::move(channel)),
      interface_(impl),
      dispatch_fn_(dispatch_fn),
      on_unbound_fn_(std::move(on_unbound_fn)) {}

std::optional<UnbindInfo> AsyncServerBinding::Dispatch(fidl_msg_t* msg, bool* binding_released) {
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  AsyncTransaction txn(hdr->txid, dispatch_fn_, binding_released);
  return txn.Dispatch(std::move(keep_alive_), msg);
}

void AsyncServerBinding::FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref,
                                      UnbindInfo info) {
  auto binding = std::move(calling_ref);  // Move binding into scope.

  // Stash state required after deleting the binding.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;
  auto channel = std::move(channel_);

  sync_completion_t on_delete;
  on_delete_ = &on_delete;
  // Delete the calling reference. Wait for any transient references to be released.
  binding = nullptr;
  ZX_ASSERT(sync_completion_wait(&on_delete, ZX_TIME_INFINITE) == ZX_OK);
  // `this` is no longer valid.

  // If required, send the epitaph.
  if (info.reason == UnbindInfo::kClose)
    info.status = fidl_epitaph_write(channel.get(), info.status);

  // Execute the unbound hook if specified.
  if (on_unbound_fn)
    on_unbound_fn(intf, info, std::move(channel));
}

std::shared_ptr<AsyncClientBinding> AsyncClientBinding::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<ChannelRef> channel,
    std::shared_ptr<ClientBase> client, OnClientUnboundFn on_unbound_fn) {
  auto ret = std::shared_ptr<AsyncClientBinding>(new AsyncClientBinding(
      dispatcher, std::move(channel), std::move(client), std::move(on_unbound_fn)));
  ret->keep_alive_ = ret;  // Keep the binding alive until an unbind operation or channel error.
  return ret;
}

AsyncClientBinding::AsyncClientBinding(
    async_dispatcher_t* dispatcher, std::shared_ptr<ChannelRef> channel,
    std::shared_ptr<ClientBase> client, OnClientUnboundFn on_unbound_fn)
    : AsyncBinding(dispatcher, zx::unowned_channel(channel->handle())),
      channel_(std::move(channel)),
      client_(std::move(client)),
      on_unbound_fn_(std::move(on_unbound_fn)) {}

std::optional<UnbindInfo> AsyncClientBinding::Dispatch(fidl_msg_t* msg, bool*) {
  return client_->Dispatch(msg);
}

void AsyncClientBinding::FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref,
                                      UnbindInfo info) {
  auto binding = std::move(calling_ref);  // Move binding into scope.

  // Stash state required after deleting the binding.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto client = std::move(client_);

  // Delete the calling reference. Transient references don't access the channel, so don't wait.
  binding = nullptr;
  // `this` is no longer valid.

  // Outstanding async responses will no longer be received, so release the contexts.
  client->ReleaseResponseContextsWithError();
  client = nullptr;

  // Execute the unbound hook if specified.
  if (on_unbound_fn)
    on_unbound_fn(info);
}

}  // namespace internal
}  // namespace fidl
