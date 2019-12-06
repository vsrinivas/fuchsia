// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/transaction.h>

namespace fidl {

CompleterBase& CompleterBase::operator=(CompleterBase&& other) noexcept {
  if (this != &other) {
    DropTransaction();
    transaction_ = other.transaction_;
    owned_ = other.owned_;
    needs_to_reply_ = other.needs_to_reply_;
    other.transaction_ = nullptr;
    other.owned_ = false;
    other.needs_to_reply_ = false;
  }
  return *this;
}

void CompleterBase::Close(zx_status_t status) {
  EnsureHasTransaction();
  transaction_->Close(status);
  DropTransaction();
}

void CompleterBase::EnableNextDispatch() {
  EnsureHasTransaction();
  transaction_->EnableNextDispatch();
}

CompleterBase::CompleterBase(CompleterBase&& other) noexcept
    : transaction_(other.transaction_),
      owned_(other.owned_),
      needs_to_reply_(other.needs_to_reply_) {
  other.transaction_ = nullptr;
  other.owned_ = false;
  other.needs_to_reply_ = false;
}

CompleterBase::~CompleterBase() {
  ZX_ASSERT(!needs_to_reply_);
  DropTransaction();
}

std::unique_ptr<Transaction> CompleterBase::TakeOwnership() {
  EnsureHasTransaction();
  std::unique_ptr<Transaction> clone = transaction_->TakeOwnership();
  DropTransaction();
  return clone;
}

void CompleterBase::SendReply(Message msg) {
  EnsureHasTransaction();
  ZX_ASSERT(needs_to_reply_);
  transaction_->Reply(std::move(msg));
  needs_to_reply_ = false;
}

void CompleterBase::EnsureHasTransaction() { ZX_ASSERT(transaction_); }

void CompleterBase::DropTransaction() {
  if (owned_) {
    owned_ = false;
    delete transaction_;
  }
  transaction_ = nullptr;
  needs_to_reply_ = false;
}

}  // namespace fidl
