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
    : wait_({{ASYNC_STATE_INIT},
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
  ZX_ASSERT(channel_);
}

AsyncBinding::~AsyncBinding() {
  ZX_ASSERT(channel_);
  if (on_delete_) {
    if (out_channel_)
      *out_channel_ = std::move(channel_);
    sync_completion_signal(on_delete_);
  }
}

void AsyncBinding::OnUnbind(zx_status_t status, UnboundReason reason) {
  ZX_ASSERT(keep_alive_);
  // Move the internal reference into this scope.
  auto binding = std::move(keep_alive_);

  {
    std::scoped_lock lock(lock_);
    // Indicate that no other thread should wait for unbind.
    unbind_ = true;

    // If the peer was not closed, and the user invoked Close() or there was a dispatch error,
    // overwrite the unbound reason and recover the epitaph or error status. Note that
    // UnboundReason::kUnbind is simply the default value for unbind_info_.reason.
    if (reason != UnboundReason::kPeerClosed && unbind_info_.reason != UnboundReason::kUnbind) {
      reason = unbind_info_.reason;
      status = unbind_info_.status;
    }
  }

  // Store the error handler and interface pointers before the binding is deleted.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;

  // Release the internal reference and wait for the deleter to run.
  auto channel = WaitForDelete(std::move(binding));

  // If required, send the epitaph.
  if (channel && reason == UnboundReason::kClose) {
    status = fidl_epitaph_write(channel.get(), status);
  }

  // Execute the unbound hook if specified.
  if (on_unbound_fn) {
    on_unbound_fn(intf, reason, status, std::move(channel));
  }

  // With no unbound callback, we close the channel here.
}

void AsyncBinding::MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) {
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
      // Dispatch the message. If binding_released is not set, keep_alive_ is still valid and this
      // thread will continue to read messages on this binding.
      dispatch_fn_(keep_alive_, &msg, &binding_released, &status);
      if (binding_released)
        return;
      // If there was any error enabling dispatch, destroy the binding.
      if (status != ZX_OK)
        return OnDispatchError(status);
    }

    // Add the wait back to the dispatcher.
    if ((status = EnableNextDispatch()) != ZX_OK)
      return OnDispatchError(status);
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    OnUnbind(ZX_ERR_PEER_CLOSED, UnboundReason::kPeerClosed);
  }
}

zx_status_t AsyncBinding::BeginWait() {
  std::scoped_lock lock(lock_);
  ZX_ASSERT(!begun_);
  begun_ = true;
  auto status = async_begin_wait(dispatcher_, &wait_);
  // On error, release the internal reference so it can be destroyed.
  if (status != ZX_OK)
    keep_alive_ = nullptr;
  return status;
}

zx_status_t AsyncBinding::EnableNextDispatch() {
  std::scoped_lock lock(lock_);
  if (unbind_)
    return ZX_ERR_CANCELED;
  auto status = async_begin_wait(dispatcher_, &wait_);
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
    // Attempt to cancel the current wait. On failure, a dispatcher thread will invoke OnUnbind().
    if (async_cancel_wait(dispatcher_, &wait_) != ZX_OK) {
      if (epitaph)  // Store the epitaph in binding state.
        unbind_info_ = {is_server_ ? UnboundReason::kClose : UnboundReason::kPeerClosed, *epitaph};
      return;
    }
  }

  keep_alive_ = nullptr;  // No one left to access the internal reference.

  // Stash data which must outlive the AsyncBinding.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;
  auto* dispatcher = dispatcher_;
  UnboundReason reason = UnboundReason::kUnbind;
  if (epitaph) {
    // For a client binding, epitaph is only non-null when the epitaph message is received. As this
    // function will have been invoked from the message handler, the async_cancel_wait() above will
    // necessarily fail. As such, this code should only be executed on a server binding.
    ZX_ASSERT(is_server_);

    // TODO(madhaviyengar): Once Transaction::Reply() returns a status instead of invoking Close(),
    // reason should only ever be UnboundReason::kClose.
    reason = *epitaph == ZX_ERR_PEER_CLOSED ? UnboundReason::kPeerClosed : UnboundReason::kClose;
  }

  // Wait for deletion and take the channel. This will only wait on internal code which will not
  // block indefinitely.
  auto channel = WaitForDelete(std::move(binding));

  // If required, send the epitaph. UnboundReason::kClose is passed to the channel unbound hook
  // indicating that the epitaph was sent as well as the return status of the send.
  if (channel && reason == UnboundReason::kClose)
    *epitaph = fidl_epitaph_write(channel.get(), *epitaph);

  if (!on_unbound_fn)
    return;  // channel goes out of scope here and gets closed.

  // Send the error handler as part of a new task on the dispatcher. This avoids nesting user code
  // in the same thread context which could cause deadlock.
  auto task = new UnboundTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnboundTask, async_now(dispatcher)},
      .on_unbound_fn = std::move(on_unbound_fn),
      .intf = intf,
      .channel = std::move(channel),
      .status = epitaph ? *epitaph : ZX_OK,
      .reason = reason};
  ZX_ASSERT(async_post_task(dispatcher, &task->task) == ZX_OK);
}

zx::channel AsyncBinding::WaitForDelete(std::shared_ptr<AsyncBinding>&& calling_ref) {
  sync_completion_t on_delete;
  calling_ref->on_delete_ = &on_delete;
  zx::channel channel;
  calling_ref->out_channel_ = &channel;
  calling_ref.reset();
  ZX_ASSERT(sync_completion_wait(&on_delete, ZX_TIME_INFINITE) == ZX_OK);
  return channel;
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
