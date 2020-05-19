// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/async_transaction.h>
#include <lib/fidl/epitaph.h>
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
  std::scoped_lock lock(lock_);

  // If the channel wasn't ever bound, just exit.
  if (!begun_)
    return;
  ZX_ASSERT(unbind_);

  // Send the epitaph if required.
  if (unbind_info_.reason == UnboundReason::kClose)
    unbind_info_.status = fidl_epitaph_write(channel_.get(), unbind_info_.status);

  // If there is an unbound hook, execute it within a separate dispatcher task, as this destructor
  // could have been invoked from anywhere.
  if (!on_unbound_fn_)
    return;
  auto* unbound_task = new UnboundTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnboundTask, async_now(dispatcher_)},
      .on_unbound_fn = std::move(on_unbound_fn_),
      .intf = interface_,
      .channel = std::move(channel_),
      .status = unbind_info_.status,
      .reason = unbind_info_.reason};
  auto status = async_post_task(dispatcher_, &unbound_task->task);
  // The dispatcher must not be shutdown while there are any pending unbound hooks.
  ZX_ASSERT(status == ZX_OK);
}

void AsyncBinding::OnUnbind(zx_status_t status, UnboundReason reason) {
  ZX_ASSERT(keep_alive_);

  {
    std::scoped_lock lock(lock_);

    // Indicate that no other thread should wait for unbind.
    unbind_ = true;

    // If the peer was closed or unbind_info_ was otherwise not set (kUnbind is the default), update
    // unbind_info_ with reason and status.
    if (reason == UnboundReason::kPeerClosed || unbind_info_.reason == UnboundReason::kUnbind)
      unbind_info_ = {reason, status};
  }

  // It is safe to delete the internal reference. This will trigger the destructor if there are no
  // transient references.
  keep_alive_ = nullptr;
}

void AsyncBinding::MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) {
  ZX_ASSERT(keep_alive_);

  if (status != ZX_OK)
    return OnUnbind(status, UnboundReason::kInternalError);

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
        return OnUnbind(status, UnboundReason::kInternalError);
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
    OnUnbind(ZX_ERR_PEER_CLOSED, UnboundReason::kPeerClosed);
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
  // The dispatcher must not be shutdown while there are any active bindings.
  ZX_ASSERT(status != ZX_ERR_BAD_STATE);
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

    // Attempt to cancel the current wait. On failure, a dispatcher thread (possibly this thread)
    // will invoke OnUnbind() before returning to the dispatcher.
    auto status = async_cancel_wait(dispatcher_, this);
    if (status != ZX_OK) {
      // Must only fail due to the wait not being found.
      ZX_ASSERT(status == ZX_ERR_NOT_FOUND);
      return;
    }
  }

  // Only one thread should ever reach this point. It is safe to delete the internal reference.
  // The destructor will run here if there are no transient references.
  keep_alive_ = nullptr;
}

void AsyncBinding::OnDispatchError(zx_status_t error) {
  ZX_ASSERT(error != ZX_OK);
  if (error == ZX_ERR_CANCELED) {
    OnUnbind(ZX_OK, UnboundReason::kUnbind);
    return;
  }
  OnUnbind(error, UnboundReason::kInternalError);
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
