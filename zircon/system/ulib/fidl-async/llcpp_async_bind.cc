// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/async_bind_internal.h>
#include <lib/fidl-async/cpp/async_transaction.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {

namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                           TypeErasedDispatchFn dispatch_fn,
                           TypeErasedOnChannelErrorFn on_channel_error_fn,
                           TypeErasedOnChannelClosedFn on_channel_closed_fn)
    : dispatcher_(dispatcher),
      channel_(std::move(channel)),
      interface_(impl),
      dispatch_fn_(dispatch_fn),
      on_channel_error_fn_(std::move(on_channel_error_fn)),
      on_channel_closed_fn_(std::move(on_channel_closed_fn)) {
  callback_.set_object(channel_.get());
  callback_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
}

AsyncBinding::~AsyncBinding() {
  if (epitaph_ != ZX_OK) {
    fidl_epitaph_write(channel_.get(), epitaph_);
  }
  if (on_channel_closed_fn_) {
    on_channel_closed_fn_(interface_);
  }
}

void AsyncBinding::OnChannelError(zx_status_t epitaph, ErrorType error_type) {
  auto local_keep_alive = keep_alive_;  // Potential last reference dropped outside the object.
  closing_ = true;
  if (epitaph_ == ZX_OK) {
    epitaph_ = epitaph;  // We use the first one set.
  }
  if (on_channel_error_fn_ && error_type != ErrorType::kNoError) {
    on_channel_error_fn_(interface_, error_type);
    on_channel_error_fn_ = nullptr;
  }
  keep_alive_ = nullptr;  // Binding can be destroyed now or when the last transaction is done.
}

void AsyncBinding::MessageHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnChannelError(status, ErrorType::kErrorInternal);
    return;
  }

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
        if (status == ZX_OK) {
          status = ZX_ERR_INTERNAL;
        }
        OnChannelError(status, ErrorType::kErrorChannelRead);
        return;
      }

      auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
      AsyncTransaction txn(hdr->txid, keep_alive_);  // txn takes a weak_ptr on keep_alive_.
      txn.Dispatch(msg);                             // txn ownership may be transferred out.
    }
    callback_.Begin(dispatcher_);
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    // No epitaph triggered by error due to a PEER_CLOSED.
    OnChannelError(ZX_OK, ErrorType::kErrorPeerClosed);
  }
}

void AsyncBinding::Close(zx_status_t epitaph, std::shared_ptr<AsyncBinding> binding) {
  // std::shared_ptr<AsyncBinding> keeps binding alive while closing.
  async::PostTask(dispatcher_, [this, binding, epitaph]() {
    ScopedToken t(domain_token());
    binding->callback_.Cancel();
    OnChannelError(epitaph, ErrorType::kNoError);
  });
}

void AsyncBinding::Unbind() {
  ScopedToken t(domain_token());  // Must be on the dispatcher thread.
  callback_.Cancel();
  if (epitaph_ == ZX_OK) {
    epitaph_ = ZX_ERR_CANCELED;  // We use the first one set.
  }
  keep_alive_ = nullptr;  // We don't wait for in-flight transactions.
}

std::shared_ptr<internal::AsyncBinding> AsyncBinding::CreateSelfManagedBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnChannelErrorFn on_channel_error_fn,
    TypeErasedOnChannelClosedFn on_channel_closed_fn) {
  auto ret = std::shared_ptr<internal::AsyncBinding>(
      new internal::AsyncBinding(dispatcher, std::move(channel), impl, dispatch_fn,
                                 std::move(on_channel_error_fn), std::move(on_channel_closed_fn)),
      Deleter());
  ret->keep_alive_ = ret;  // We keep the binding alive until somebody decides to close the channel.
  return ret;
}

fit::result<BindingRef, zx_status_t> AsyncTypeErasedBind(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnChannelErrorFn on_channel_error_fn,
    TypeErasedOnChannelClosedFn on_channel_closed_fn) {
  auto internal_binding = internal::AsyncBinding::CreateSelfManagedBinding(
      dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_channel_error_fn),
      std::move(on_channel_closed_fn));
  auto status = internal_binding->BeginWait();
  if (status == ZX_OK) {
    return fit::ok(fidl::BindingRef(internal_binding));
  } else {
    return fit::error(status);
  }
}

}  // namespace internal

void BindingRef::Unbind() {
  ZX_ASSERT(binding_->deleter_ == nullptr);  // We unbind only once.
  sync_completion_t deleter = {};
  // We setup getting signaled once the binding is destroyed.
  binding_->deleter_ = &deleter;
  binding_->Unbind();
  // Destroy the binding as soon as any transaction's temporary references are gone.
  binding_ = nullptr;
  // Wait for the binding object to get destroyed, potentially involving temporary strong
  // references from outstanding transactions to be dropped as well.
  auto status = sync_completion_wait(&deleter, ZX_TIME_INFINITE);
  ZX_ASSERT(status == ZX_OK);
}

}  // namespace fidl
