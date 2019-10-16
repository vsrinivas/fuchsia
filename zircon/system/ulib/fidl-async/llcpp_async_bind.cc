// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/fidl-async/cpp/async_transaction.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {

namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                           TypeErasedDispatchFn dispatch_fn,
                           TypeErasedOnChannelCloseFn on_channel_closing_fn,
                           TypeErasedOnChannelCloseFn on_channel_closed_fn)
    : dispatcher_(dispatcher),
      channel_(std::move(channel)),
      interface_(impl),
      dispatch_fn_(dispatch_fn),
      on_channel_closing_fn_(std::move(on_channel_closing_fn)),
      on_channel_closed_fn_(std::move(on_channel_closed_fn)) {
  callback_.set_object(channel_.get());
  callback_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
}

AsyncBinding::~AsyncBinding() {
  if (epitaph_ != ZX_OK) {
    fidl_epitaph_write(channel_.get(), epitaph_);
  }
  ZX_ASSERT(on_channel_closing_fn_ == nullptr);
  if (on_channel_closed_fn_) {
    on_channel_closed_fn_(interface_);
  }
}

void AsyncBinding::OnChannelClosing() {
  if (on_channel_closing_fn_) {
    on_channel_closing_fn_(interface_);
    on_channel_closing_fn_ = nullptr;
  }
  auto local_keep_alive = keep_alive_;  // Potential last reference dropped outside the object.
  keep_alive_ = nullptr;  // Binding can be destroyed now or when the last transaction is done.
}

void AsyncBinding::MessageHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    epitaph_ = status;  // May be overwriten, we use the last one set.
    OnChannelClosing();
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
        epitaph_ = status;  // May be overwriten, we use the last one set.
        OnChannelClosing();
        return;
      }

      auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
      AsyncTransaction txn(hdr->txid, keep_alive_);
      txn.Dispatch(msg);  // txn ownership may be transferred out.
    }
    callback_.Begin(dispatcher_);
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    OnChannelClosing();
  }
}

void AsyncBinding::Close(zx_status_t epitaph) {
  async::PostTask(dispatcher_, [this, epitaph]() {
    ScopedToken t(domain_token());
    callback_.Cancel();
    epitaph_ = epitaph;  // May be overwriten, we use the last one set.
    OnChannelClosing();
  });
}

void AsyncBinding::Release(std::shared_ptr<AsyncBinding> reference) {
  async::PostTask(dispatcher_, [reference]() {});
}

std::shared_ptr<AsyncBinding> AsyncBinding::CreateSelfManagedBinding(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnChannelCloseFn on_channel_closing_fn,
    TypeErasedOnChannelCloseFn on_channel_closed_fn) {
  auto ret = std::shared_ptr<AsyncBinding>(
      new AsyncBinding(dispatcher, std::move(channel), impl, dispatch_fn,
                       std::move(on_channel_closing_fn), std::move(on_channel_closed_fn)));
  ret->keep_alive_ = ret;  // We keep the binding alive until somebody decides to close the channel.
  return ret;
}

zx_status_t AsyncTypeErasedBind(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                                TypeErasedDispatchFn dispatch_fn,
                                TypeErasedOnChannelCloseFn on_channel_closing_fn,
                                TypeErasedOnChannelCloseFn on_channel_closed_fn) {
  auto binding = AsyncBinding::CreateSelfManagedBinding(
      dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_channel_closing_fn),
      std::move(on_channel_closed_fn));
  return binding->BeginWait();
}

}  // namespace internal

}  // namespace fidl
