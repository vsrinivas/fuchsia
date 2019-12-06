// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-async/cpp/async_bind_internal.h>
#include <lib/fidl-async/cpp/async_transaction.h>
#include <zircon/assert.h>

namespace fidl {

namespace internal {

void AsyncTransaction::Dispatch(std::shared_ptr<AsyncBinding>&& binding, fidl_msg_t msg) {
  ZX_ASSERT(!owned_binding_);
  ZX_ASSERT(!moved_);
  bool moved = false;
  moved_ = &moved;
  // Take ownership of the internal (dispatcher) reference to the AsyncBinding. Until code executed
  // in this scope releases ownership, no other thread may access the binding via keep_alive_.
  owned_binding_ = std::move(binding);
  owned_binding_->dispatch_fn_(owned_binding_->interface_, &msg, this);
  if (moved)
    return;  // Return if `this` is no longer valid.
  moved_ = nullptr;
  // Transfer ownership of the binding back to the dispatcher if we still have it.
  if (owned_binding_) {
    auto* binding = owned_binding_.get();
    binding->keep_alive_ = std::move(owned_binding_);
  }
}

void AsyncTransaction::Reply(fidl::Message msg) {
  ZX_ASSERT(txid_ != 0);
  auto txid = txid_;
  txid_ = 0;

  // Get a strong reference to the binding. Avoid unnecessarily copying the reference if
  // owned_binding_ is valid. On error, the reference will be consumed by Close().
  std::shared_ptr<AsyncBinding> tmp = owned_binding_ ? nullptr : unowned_binding_.lock();
  auto& binding = owned_binding_ ? owned_binding_ : tmp;
  if (!binding)
    return;

  if (msg.bytes().actual() < sizeof(fidl_message_header_t)) {
    // TODO(42086): Propagate this error back up to the user.
    binding->Close(std::move(binding), ZX_ERR_INVALID_ARGS);
    return;
  }
  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes().data());
  hdr->txid = txid;
  auto status = binding->channel()->write(0, msg.bytes().data(), msg.bytes().actual(),
                                          msg.handles().data(), msg.handles().actual());
  if (status != ZX_OK)
    binding->Close(std::move(binding), status);
  // release ownership on handles, which have been consumed by channel write.
  msg.ClearHandlesUnsafe();
}

void AsyncTransaction::EnableNextDispatch() {
  if (!owned_binding_)
    return;  // Has no effect if the Transaction does not own the binding.
  auto* binding = owned_binding_.get();
  unowned_binding_ = owned_binding_;  // Preserve a weak reference to the binding.
  binding->keep_alive_ = std::move(owned_binding_);
  if ((*resume_status_ = binding->EnableNextDispatch()) == ZX_OK)
    *binding_released_ = true;
}

void AsyncTransaction::Close(zx_status_t epitaph) {
  if (!owned_binding_) {
    if (auto binding = unowned_binding_.lock())
      binding->Close(std::move(binding), epitaph);
    return;
  }
  *resume_status_ = ZX_ERR_CANCELED;  // OnUnbind() will run after Dispatch() returns.
  // Close() will not be able to cancel the wait. Restore the internal reference.
  owned_binding_->keep_alive_ = owned_binding_;
  owned_binding_->Close(std::move(owned_binding_), epitaph);
}

std::unique_ptr<Transaction> AsyncTransaction::TakeOwnership() {
  ZX_ASSERT(owned_binding_);
  ZX_ASSERT(moved_);
  *moved_ = true;
  moved_ = nullptr;                   // This should only ever be called once.
  unowned_binding_ = owned_binding_;  // Preserve a weak reference to the binding.
  auto* binding = owned_binding_.get();
  binding->keep_alive_ = std::move(owned_binding_);
  return std::make_unique<AsyncTransaction>(std::move(*this));
}

}  // namespace internal

}  // namespace fidl
