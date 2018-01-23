// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/internal/stub_controller.h"

#include "lib/fidl/cpp/bindings2/internal/pending_response.h"
#include "lib/fidl/cpp/bindings2/internal/weak_stub_controller.h"

namespace fidl {
namespace internal {

StubController::StubController() : weak_(nullptr), reader_(this) {}

StubController::~StubController() {
  InvalidateWeakIfNeeded();
}

zx_status_t StubController::OnMessage(Message message) {
  if (!message.has_header())
    return ZX_ERR_INVALID_ARGS;
  zx_txid_t txid = message.txid();
  WeakStubController* weak = nullptr;
  if (txid) {
    if (!weak_)
      weak_ = new WeakStubController(this);
    weak = weak_;
  }
  return stub_->Dispatch(std::move(message), PendingResponse(txid, weak));
}

void StubController::OnChannelGone() {
  InvalidateWeakIfNeeded();
}

void StubController::InvalidateWeakIfNeeded() {
  if (!weak_)
    return;
  weak_->Invalidate();
  weak_->Release();
  weak_ = nullptr;
}

}  // namespace internal
}  // namespace fidl
