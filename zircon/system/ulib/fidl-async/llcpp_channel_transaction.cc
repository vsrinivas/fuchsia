// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl-async/cpp/channel_transaction.h>
#include <lib/fidl/epitaph.h>
#include <zircon/assert.h>

namespace fidl {

namespace internal {

void ChannelTransaction::Dispatch(fidl_msg_t msg) {
  binding_->dispatch_fn_(binding_->interface_, &msg, this);
}

void ChannelTransaction::Reply(fidl::Message msg) {
  ZX_ASSERT(txid_ != 0);
  if (msg.bytes().actual() < sizeof(fidl_message_header_t)) {
    Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes().data());
  hdr->txid = txid_;
  txid_ = 0;
  auto status = binding_->channel()->write(0, msg.bytes().data(), msg.bytes().actual(),
                                           msg.handles().data(), msg.handles().actual());
  if (status != ZX_OK) {
    Close(status);
  }
  // release ownership on handles, which have been consumed by channel write.
  msg.ClearHandlesUnsafe();
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

std::unique_ptr<Transaction> ChannelTransaction::TakeOwnership() {
  return std::make_unique<ChannelTransaction>(std::move(*this));
}

}  // namespace internal

}  // namespace fidl
