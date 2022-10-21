// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/simple_binding.h"

#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/txn_header.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace devfs {

void ChannelTransaction::Dispatch(fidl::IncomingHeaderAndMessage& msg) {
  binding_->interface_->dispatch_message(std::move(msg), this, nullptr);
}

zx_status_t ChannelTransaction::Reply(fidl::OutgoingMessage* message,
                                      fidl::WriteOptions write_options) {
  ZX_ASSERT(txid_ != 0);
  message->set_txid(txid_);
  txid_ = 0;
  message->Write(binding_->channel());
  return message->status();
}

void ChannelTransaction::Close(zx_status_t epitaph) {
  // We need to make sure binding_ is present, since it may have been released
  // if Reply() called Close()
  if (binding_) {
    fidl_epitaph_write(binding_->channel()->get(), epitaph);
    binding_.reset();
  }
}

ChannelTransaction::~ChannelTransaction() {
  if (binding_) {
    BeginWait(&binding_);
  }
}

std::unique_ptr<fidl::Transaction> ChannelTransaction::TakeOwnership() {
  return std::make_unique<ChannelTransaction>(std::move(*this));
}

SimpleBinding::SimpleBinding(async_dispatcher_t* dispatcher, zx::channel channel,
                             fidl::internal::IncomingMessageDispatcher* interface,
                             AnyOnChannelClosedFn on_channel_closed_fn)
    : async_wait_t({
          .state = ASYNC_STATE_INIT,
          .handler = &MessageHandler,
          .object = channel.release(),
          .trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
          .options = 0,
      }),
      dispatcher_(dispatcher),
      interface_(interface),
      on_channel_closed_fn_(std::move(on_channel_closed_fn)) {}

SimpleBinding::~SimpleBinding() {
  zx_handle_close(async_wait_t::object);
  if (on_channel_closed_fn_) {
    on_channel_closed_fn_(interface_);
  }
}

void SimpleBinding::MessageHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                                   zx_status_t dispatcher_status,
                                   const zx_packet_signal_t* signal) {
  std::unique_ptr<SimpleBinding> binding(static_cast<SimpleBinding*>(wait));
  if (dispatcher_status != ZX_OK) {
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint64_t i = 0; i < signal->count; i++) {
      fidl_trace(WillLLCPPAsyncChannelRead);
      fidl::IncomingHeaderAndMessage msg = fidl::MessageRead(
          zx::unowned_channel(wait->object), fidl::ChannelMessageStorageView{
                                                 .bytes = fidl::BufferSpan(bytes, std::size(bytes)),
                                                 .handles = handles,
                                                 .handle_metadata = handle_metadata,
                                                 .handle_capacity = ZX_CHANNEL_MAX_MSG_HANDLES,
                                             });
      if (!msg.ok())
        return;
      fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes, msg.byte_actual(),
                 msg.handle_actual());

      auto* hdr = msg.header();
      ChannelTransaction txn(hdr->txid, std::move(binding));
      txn.Dispatch(msg);
      binding = txn.TakeBinding();
      if (!binding) {
        return;
      }
    }

    // Will only get here if every single message was handled synchronously and successfully.
    BeginWait(&binding);
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  }
}

zx_status_t BeginWait(std::unique_ptr<SimpleBinding>* unique_binding) {
  SimpleBinding* binding = unique_binding->release();
  zx_status_t status;
  if ((status = async_begin_wait(binding->dispatcher_, binding)) != ZX_OK) {
    // Failed to transfer binding ownership to async dispatcher.
    unique_binding->reset(binding);
  }
  return status;
}

}  // namespace devfs
