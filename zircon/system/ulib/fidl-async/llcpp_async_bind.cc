// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/fidl-async/cpp/async_bind_internal.h>
#include <lib/fidl-async/cpp/async_transaction.h>
#include <lib/fidl/epitaph.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {

namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                           TypeErasedDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn)
    : wait_({{ASYNC_STATE_INIT},
             &AsyncBinding::OnMessage,
             channel.get(),
             ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
             0}),
      dispatcher_(dispatcher),
      channel_(std::move(channel)),
      interface_(impl),
      dispatch_fn_(dispatch_fn),
      on_unbound_fn_(std::move(on_unbound_fn)) {
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

void AsyncBinding::OnUnbind(zx_status_t epitaph, UnboundReason reason) {
  ZX_ASSERT(keep_alive_);
  // Move the internal reference into this scope.
  auto binding = std::move(keep_alive_);

  bool send_epitaph = false;
  {
    std::scoped_lock lock(lock_);
    // Indicate that no other thread should wait for unbind.
    unbind_ = true;
    // Determine the epitaph and whether to send it.
    if (reason == UnboundReason::kInternalError || epitaph_.send) {
      send_epitaph = true;
      if (epitaph_.status != ZX_OK)
        epitaph = epitaph_.status;
    }
  }

  // Update the reason on a peer closed status.
  if (epitaph == ZX_ERR_PEER_CLOSED)
    reason = UnboundReason::kPeerClosed;

  // Store the error handler and interface pointers before the binding is deleted.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;

  // Release the internal reference and wait for the deleter to run.
  auto channel = WaitForDelete(std::move(binding), reason != UnboundReason::kPeerClosed);

  // If required, send the epitaph and close the channel.
  if (send_epitaph && channel) {
    auto tmp = std::move(channel);
    fidl_epitaph_write(tmp.get(), epitaph);
  }

  // Execute the unbound hook if specified.
  if (on_unbound_fn)
    on_unbound_fn(intf, reason, std::move(channel));
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
      auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
      AsyncTransaction txn(hdr->txid, &binding_released, &status);
      // Transfer keep_alive_ to the AsyncTransaction. If binding_released is false after Dispatch()
      // returns, keep_alive_ is still valid and this thread may continue to access the binding.
      txn.Dispatch(std::move(keep_alive_), msg);  // txn may be moved, cannot access after this.
      if (binding_released)
        return;
      // If there was any error enabling dispatch, destroy the binding.
      if (status != ZX_OK)
        return OnEnableNextDispatchError(status);
    }

    // Add the wait back to the dispatcher.
    if ((status = EnableNextDispatch()) != ZX_OK)
      return OnEnableNextDispatchError(status);
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    // No epitaph triggered by error due to a PEER_CLOSED.
    OnUnbind(ZX_OK, UnboundReason::kPeerClosed);
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
  if (status != ZX_OK)
    epitaph_ = {epitaph_.status == ZX_OK ? status : epitaph_.status, true};
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
      if (epitaph)
        epitaph_ = {*epitaph, true};  // Store the epitaph in binding state.
      return;
    }
  }

  keep_alive_ = nullptr;  // No one left to access the internal reference.

  // Stash data which must outlive the AsyncBinding.
  auto on_unbound_fn = std::move(on_unbound_fn_);
  auto* intf = interface_;
  auto* dispatcher = dispatcher_;
  bool peer_closed = epitaph && *epitaph == ZX_ERR_PEER_CLOSED;
  // Wait for deletion and take the channel. This will only wait on internal code which will not
  // block indefinitely.
  auto channel = WaitForDelete(std::move(binding), !peer_closed);

  // If required, send the epitaph and close the channel.
  if (channel && epitaph) {
    auto tmp = std::move(channel);
    fidl_epitaph_write(tmp.get(), *epitaph);
  }

  if (!on_unbound_fn)
    return;
  // Send the error handler as part of a new task on the dispatcher. This avoids nesting user code
  // in the same thread context which could cause deadlock.
  auto task = new UnboundTask{
      .task = {{ASYNC_STATE_INIT}, &AsyncBinding::OnUnboundTask, async_now(dispatcher)},
      .on_unbound_fn = std::move(on_unbound_fn),
      .intf = intf,
      .channel = std::move(channel),
      .reason = peer_closed ? UnboundReason::kPeerClosed : UnboundReason::kUnbind};
  ZX_ASSERT(async_post_task(dispatcher, &task->task) == ZX_OK);
}

zx::channel AsyncBinding::WaitForDelete(std::shared_ptr<AsyncBinding>&& calling_ref,
                                        bool get_channel) {
  sync_completion_t on_delete;
  calling_ref->on_delete_ = &on_delete;
  zx::channel channel;
  if (get_channel)
    calling_ref->out_channel_ = &channel;
  calling_ref.reset();
  ZX_ASSERT(sync_completion_wait(&on_delete, ZX_TIME_INFINITE) == ZX_OK);
  return channel;
}

void AsyncBinding::OnEnableNextDispatchError(zx_status_t error) {
  ZX_ASSERT(error != ZX_OK);
  if (error == ZX_ERR_CANCELED) {
    OnUnbind(ZX_OK, UnboundReason::kUnbind);
    return;
  }
  OnUnbind(error, UnboundReason::kInternalError);
}

std::shared_ptr<internal::AsyncBinding> AsyncBinding::CreateSelfManagedBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn) {
  auto ret = std::shared_ptr<internal::AsyncBinding>(new internal::AsyncBinding(
      dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_unbound_fn)));
  ret->keep_alive_ = ret;  // We keep the binding alive until somebody decides to close the channel.
  return ret;
}

fit::result<BindingRef, zx_status_t> AsyncTypeErasedBind(async_dispatcher_t* dispatcher,
                                                         zx::channel channel, void* impl,
                                                         TypeErasedDispatchFn dispatch_fn,
                                                         TypeErasedOnUnboundFn on_unbound_fn) {
  auto internal_binding = internal::AsyncBinding::CreateSelfManagedBinding(
      dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_unbound_fn));
  auto status = internal_binding->BeginWait();
  if (status == ZX_OK) {
    return fit::ok(fidl::BindingRef(std::move(internal_binding)));
  } else {
    return fit::error(status);
  }
}

}  // namespace internal

void BindingRef::Unbind() {
  if (binding_)
    binding_->Unbind(std::move(binding_));
}

void BindingRef::Close(zx_status_t epitaph) {
  if (binding_)
    binding_->Close(std::move(binding_), epitaph);
}

}  // namespace fidl
