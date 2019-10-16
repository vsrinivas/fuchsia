// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/fidl-async/cpp/async_transaction.h>
#include <zircon/assert.h>

namespace fidl {

namespace internal {

void AsyncTransaction::Dispatch(fidl_msg_t msg) {
  binding_->dispatch_fn_(binding_->interface_, &msg, this);
}

void AsyncTransaction::Reply(fidl::Message msg) {
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

void AsyncTransaction::Close(zx_status_t epitaph) { binding_->Close(epitaph); }

std::unique_ptr<Transaction> AsyncTransaction::TakeOwnership() {
  return std::make_unique<AsyncTransaction>(std::move(*this));
}

}  // namespace internal

}  // namespace fidl
