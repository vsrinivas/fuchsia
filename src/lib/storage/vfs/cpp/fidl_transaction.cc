// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/fidl_transaction.h"

#include <lib/fidl/epitaph.h>
#include <lib/fidl/txn_header.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace fs {

namespace internal {

zx_status_t FidlTransaction::Reply(fidl::OutgoingMessage* message,
                                   fidl::WriteOptions write_options) {
  ZX_ASSERT(transaction_id_ != 0);
  message->set_txid(transaction_id_);
  transaction_id_ = 0;
  if (auto binding = binding_.lock()) {
    message->Write(binding->channel());
    return message->status();
  }
  return ZX_ERR_CANCELED;
}

void FidlTransaction::Close(zx_status_t epitaph) {
  status_ = epitaph;
  // We need to make sure binding_ is present, since it may have been released if Reply() called
  // Close()
  if (auto binding = binding_.lock()) {
    fidl_epitaph_write(binding->channel().get(), epitaph);
    binding->AsyncTeardown();
  }
}

FidlTransaction::~FidlTransaction() {
  if (auto binding = binding_.lock()) {
    binding->UnregisterInflightTransaction();
    zx_status_t status = binding->StartDispatching();
    ZX_ASSERT_MSG(status == ZX_OK, "Dispatch loop unexpectedly ended");
  }
}

std::unique_ptr<::fidl::Transaction> FidlTransaction::TakeOwnership() {
  return std::make_unique<FidlTransaction>(std::move(*this));
}

FidlTransaction::Result FidlTransaction::ToResult() {
  if (status_ != ZX_OK) {
    if (auto binding = binding_.lock()) {
      binding->UnregisterInflightTransaction();
    }
    binding_.reset();
    return Result::kClosed;
  }
  if (binding_.expired()) {
    return Result::kPendingAsyncReply;
  }
  if (auto binding = binding_.lock()) {
    binding->UnregisterInflightTransaction();
  }
  binding_.reset();
  return Result::kRepliedSynchronously;
}

}  // namespace internal

}  // namespace fs
