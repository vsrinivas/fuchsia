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
    method_expects_reply_ = other.method_expects_reply_;
    other.transaction_ = nullptr;
    other.owned_ = false;
  }
  return *this;
}

void CompleterBase::Close(zx_status_t status) {
  EnsureHasTransaction();
  transaction_->Close(status);
  DropTransaction();
}

CompleterBase::CompleterBase(CompleterBase&& other) noexcept
    : transaction_(other.transaction_),
      owned_(other.owned_),
      method_expects_reply_(other.method_expects_reply_) {
  other.transaction_ = nullptr;
  other.owned_ = false;
}

CompleterBase::~CompleterBase() { ZX_ASSERT(!method_expects_reply() || !pending()); }

std::unique_ptr<Transaction> CompleterBase::TakeOwnership() {
  EnsureHasTransaction();
  std::unique_ptr<Transaction> clone = transaction_->TakeOwnership();
  DropTransaction();
  return clone;
}

void CompleterBase::SendReply(Message msg) {
  EnsureHasTransaction();
  transaction_->Reply(std::move(msg));
  DropTransaction();
}

void CompleterBase::EnsureHasTransaction() { ZX_ASSERT(transaction_); }

void CompleterBase::DropTransaction() {
  if (owned_) {
    owned_ = false;
    delete transaction_;
  }
  transaction_ = nullptr;
}

}  // namespace fidl
