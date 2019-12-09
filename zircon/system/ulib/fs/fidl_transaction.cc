// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/internal/fidl_transaction.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace fs {

namespace internal {

void FidlTransaction::Reply(fidl::Message msg) {
  ZX_ASSERT(transaction_id_ != 0);
  if (msg.bytes().actual() < sizeof(fidl_message_header_t)) {
    Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes().data());
  hdr->txid = transaction_id_;
  transaction_id_ = 0;
  if (auto binding = binding_.lock()) {
    zx_status_t status = binding->channel().write(0, msg.bytes().data(), msg.bytes().actual(),
                                                  msg.handles().data(), msg.handles().actual());
    if (status != ZX_OK) {
      Close(status);
    }
    // Release ownership on handles, which have been consumed by channel write.
    msg.ClearHandlesUnsafe();
  }
}

void FidlTransaction::Close(zx_status_t epitaph) {
  status_ = epitaph;
  // We need to make sure binding_ is present, since it may have been released
  // if Reply() called Close()
  if (auto binding = binding_.lock()) {
    fidl_epitaph_write(binding->channel().get(), epitaph);
    binding->AsyncTeardown();
  }
}

FidlTransaction::~FidlTransaction() {
  if (auto binding = binding_.lock()) {
    zx_status_t status = binding->StartDispatching();
    ZX_ASSERT_MSG(status == ZX_OK, "Dispatch loop unexpectedly ended");
  }
}

std::unique_ptr<::fidl::Transaction> FidlTransaction::TakeOwnership() {
  return std::make_unique<FidlTransaction>(std::move(*this));
}

FidlTransaction::Result FidlTransaction::ToResult() {
  if (status_ != ZX_OK) {
    binding_.reset();
    return Result::kClosed;
  }
  if (binding_.expired()) {
    return Result::kPendingAsyncReply;
  }
  binding_.reset();
  return Result::kRepliedSynchronously;
}

}  // namespace internal

}  // namespace fs
