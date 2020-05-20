// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/async_transaction.h>
#include <lib/fidl/epitaph.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {
namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                           bool is_server, TypeErasedOnUnboundFn on_unbound_fn,
                           DispatchFn dispatch_fn)
    : async_wait_t({{ASYNC_STATE_INIT},
                    &AsyncBinding::OnMessage,
                    channel.get(),
                    ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                    0}),
      dispatcher_(dispatcher),
      channel_(std::move(channel)),
      interface_(impl),
      on_unbound_fn_(std::move(on_unbound_fn)),
      dispatch_fn_(std::move(dispatch_fn)),
      is_server_(is_server) {
  ZX_ASSERT(dispatcher);
  ZX_ASSERT(channel_);
  ZX_ASSERT(dispatch_fn_);
}

AsyncBinding::~AsyncBinding() {
  ZX_ASSERT(channel_);
  if (on_delete_) {
    if (out_channel_)
      *out_channel_ = std::move(channel_);
    sync_completion_signal(on_delete_);
  }
}

void AsyncBinding::OnUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t status,
                            UnboundReason reason) {
  auto binding = std::move(calling_ref);  // Move calling_ref into this scope.

  {
    std::scoped_lock lock(lock_);

    // Only one thread should wait for unbind.
    if (sync_unbind_)
      return;
    unbind_ = sync_unbind_ = true;

    // If the async_cancel_wait() in UnbindInternal() succeeded, no dispatcher thread should be able
    // to access keep_alive_, and it must be freed.
    if (canceled_)
      keep_alive_ = nullptr;

    // If the peer was not closed, and the user invoked Close() or there was a dispatch error,
    // overwrite the unbound reason and recover the epitaph or error status. Note that
    // UnboundReason::kUnbind is simply the default value for unbind_info_.reason.
    if (reason != UnboundReason::kPeerClosed && unbind_info_.reason != UnboundReason::kUnbind) {
      reason = unbind_info_.reason;
      status = unbind_info_.status;
    }
  }

  // Stash any state required after deleting the binding.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;

  sync_completion_t on_delete;
  on_delete_ = &on_delete;
  zx::channel channel;
  out_channel_ = &channel;

  // Delete the calling reference. Wait for any transient references to be released.
  binding = nullptr;
  // TODO(45407): Currently, this could wait for a synchronous call from a fidl::Client<> to
  // complete. Once it is possible to interrupt ongoing calls, do so to avoid potentially unbounded
  // blocking here.
  ZX_ASSERT(sync_completion_wait(&on_delete, ZX_TIME_INFINITE) == ZX_OK);

  // If required, send the epitaph.
  if (channel && reason == UnboundReason::kClose) {
    status = fidl_epitaph_write(channel.get(), status);
  }

  // Execute the unbound hook if specified.
  if (on_unbound_fn) {
    on_unbound_fn(intf, reason, status, std::move(channel));
  }
}

void AsyncBinding::MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) {
  ZX_ASSERT(keep_alive_);

  if (status != ZX_OK)
    return OnUnbind(std::move(keep_alive_), status, UnboundReason::kInternalError);

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
      status = channel_.read(0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                             ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
      if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t)) {
        if (status == ZX_OK)
          status = ZX_ERR_INTERNAL;
        return OnUnbind(std::move(keep_alive_), status, UnboundReason::kInternalError);
      }

      // Flag indicating whether this thread still has access to the binding.
      bool binding_released = false;
      // Dispatch the message.
      dispatch_fn_(keep_alive_, &msg, &binding_released, &status);

      // If binding_released is not set, keep_alive_ is still valid and this thread will continue to
      // read messages on this binding.
      if (binding_released)
        return;
      ZX_ASSERT(keep_alive_);

      // If there was any error enabling dispatch, destroy the binding.
      if (status != ZX_OK)
        return OnDispatchError(status);
    }

    // Add the wait back to the dispatcher.
    if ((status = EnableNextDispatch()) != ZX_OK)
      return OnDispatchError(status);
  } else {
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    OnUnbind(std::move(keep_alive_), ZX_ERR_PEER_CLOSED, UnboundReason::kPeerClosed);
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
    unbind_info_ = {UnboundReason::kInternalError, status};
  return status;
}

void AsyncBinding::UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref,
                                  zx_status_t* epitaph) {
  ZX_ASSERT(calling_ref);
  // Move the calling reference into this scope.
  auto binding = std::move(calling_ref);

  {
    std::scoped_lock lock(lock_);
    // Another thread has entered this critical section already via Unbind(), Close(), or
    // OnUnbind(). Release our reference and return to unblock that caller.
    if (unbind_)
      return;
    unbind_ = true;  // Indicate that waits should no longer be added to the dispatcher.

    if (epitaph) {  // Store the epitaph in binding state.
      unbind_info_ = {is_server_ ? UnboundReason::kClose : UnboundReason::kPeerClosed, *epitaph};

      // TODO(madhaviyengar): Once Transaction::Reply() returns a status instead of invoking
      // Close(), reason should only ever be UnboundReason::kClose for a server.
      if (unbind_info_.status == ZX_ERR_PEER_CLOSED)
        unbind_info_.reason = UnboundReason::kPeerClosed;
    }

    // Attempt to add a task to unbind the channel. On failure, the dispatcher was shutdown,
    // and another thread will do the unbinding.
    auto* unbind_task = new UnbindTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnbindTask, async_now(dispatcher_)},
      .binding = binding
    };
    if (async_post_task(dispatcher_, &unbind_task->task) != ZX_OK) {
      delete unbind_task;
      return;
    }

    // Attempt to cancel the current wait. On failure, a dispatcher thread (possibly this thread)
    // will invoke OnUnbind() before returning to the dispatcher.
    canceled_ = async_cancel_wait(dispatcher_, this) == ZX_OK;
  }
}

void AsyncBinding::OnDispatchError(zx_status_t error) {
  ZX_ASSERT(error != ZX_OK);
  if (error == ZX_ERR_CANCELED) {
    OnUnbind(std::move(keep_alive_), ZX_OK, UnboundReason::kUnbind);
    return;
  }
  OnUnbind(std::move(keep_alive_), error, UnboundReason::kInternalError);
}

std::shared_ptr<AsyncBinding> AsyncBinding::CreateServerBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn) {
  auto ret = std::shared_ptr<AsyncBinding>(new AsyncBinding(
      dispatcher, std::move(channel), impl, true, std::move(on_unbound_fn),
      [dispatch_fn](std::shared_ptr<AsyncBinding>& binding, fidl_msg_t* msg,
                    bool* binding_released, zx_status_t* status) {
        auto hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
        AsyncTransaction txn(hdr->txid, dispatch_fn, binding_released, status);
        txn.Dispatch(std::move(binding), msg);
      }));
  ret->keep_alive_ = ret;  // We keep the binding alive until somebody decides to close the channel.
  return ret;
}

std::shared_ptr<AsyncBinding> AsyncBinding::CreateClientBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl, DispatchFn dispatch_fn,
    TypeErasedOnUnboundFn on_unbound_fn) {
  auto ret = std::shared_ptr<AsyncBinding>(new AsyncBinding(
      dispatcher, std::move(channel), impl, false, std::move(on_unbound_fn),
      std::move(dispatch_fn)));
  ret->keep_alive_ = ret;  // Keep the binding alive until an unbind operation or channel error.
  return ret;
}

}  // namespace internal
}  // namespace fidl
