// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/async_transaction.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/assert.h>

namespace fidl {

namespace internal {

//
// Synchronous transaction methods
//

std::optional<UnbindInfo> SyncTransaction::Dispatch(fidl::IncomingMessage&& msg) {
  ZX_ASSERT(binding_);
  binding_->interface()->dispatch_message(std::move(msg), this);
  return unbind_info_;
}

zx_status_t SyncTransaction::Reply(fidl::OutgoingMessage* message) {
  ZX_ASSERT(txid_ != 0);
  auto txid = txid_;
  txid_ = 0;

  ZX_ASSERT(binding_);
  message->set_txid(txid);
  message->Write(binding_->channel());
  return message->status();
}

void SyncTransaction::EnableNextDispatch() {
  if (!binding_)
    return;
  // Only allow one |EnableNextDispatch| call per transaction instance.
  if (binding_lifetime_extender_)
    return;

  // Keeping another strong reference to the binding ensures that binding
  // teardown will not complete until this |SyncTransaction| destructs, i.e.
  // until the server method handler returns.
  binding_lifetime_extender_ = binding_->shared_from_this();
  if (binding_->CheckForTeardownAndBeginNextWait() == ZX_OK) {
    *next_wait_begun_early_ = true;
  } else {
    // Propagate a placeholder error, such that the message handler will
    // terminate dispatch right after the processing of this transaction.
    unbind_info_ = UnbindInfo::Unbind();
  }
}

void SyncTransaction::Close(zx_status_t epitaph) {
  if (!binding_)
    return;
  binding_ = nullptr;

  // If |EnableNextDispatch| was called, the dispatcher will not monitor
  // our |unbind_info_|; we should asynchronously request teardown.
  if (binding_lifetime_extender_) {
    binding_lifetime_extender_->Close(std::move(binding_lifetime_extender_), epitaph);
    return;
  }

  unbind_info_ = UnbindInfo::Close(epitaph);
}

void SyncTransaction::InternalError(UnbindInfo error) {
  if (!binding_)
    return;
  binding_ = nullptr;

  // If |EnableNextDispatch| was called, the dispatcher will not monitor
  // our |unbind_info_|; we should asynchronously request teardown.
  if (binding_lifetime_extender_) {
    binding_lifetime_extender_->StartTeardownWithInfo(std::move(binding_lifetime_extender_), error);
    return;
  }

  unbind_info_ = error;
}

std::unique_ptr<Transaction> SyncTransaction::TakeOwnership() {
  ZX_ASSERT(binding_);
  auto transaction = std::make_unique<AsyncTransaction>(std::move(*this));
  binding_ = nullptr;
  return transaction;
}

bool SyncTransaction::IsUnbound() { return false; }

//
// Asynchronous transaction methods
//

zx_status_t AsyncTransaction::Reply(fidl::OutgoingMessage* message) {
  ZX_ASSERT(txid_ != 0);
  auto txid = txid_;
  txid_ = 0;

  std::shared_ptr<AsyncServerBinding> binding = binding_.lock();
  if (!binding)
    return ZX_ERR_CANCELED;

  message->set_txid(txid);
  message->Write(binding->channel());
  return message->status();
}

void AsyncTransaction::EnableNextDispatch() {
  // Unreachable. Async completers don't expose |EnableNextDispatch|.
  __builtin_abort();
}

void AsyncTransaction::Close(zx_status_t epitaph) {
  if (auto binding = binding_.lock()) {
    binding->Close(std::move(binding), epitaph);
  }
}

void AsyncTransaction::InternalError(UnbindInfo error) {
  if (auto binding = binding_.lock()) {
    binding->StartTeardownWithInfo(std::move(binding), error);
  }
}

std::unique_ptr<Transaction> AsyncTransaction::TakeOwnership() {
  // Unreachable. Async completers don't expose |ToAsync|.
  __builtin_abort();
}

bool AsyncTransaction::IsUnbound() { return binding_.expired(); }

}  // namespace internal

}  // namespace fidl
