// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/stub_controller.h"

#include "lib/fidl/cpp/internal/logging.h"
#include "lib/fidl/cpp/internal/pending_response.h"
#include "lib/fidl/cpp/internal/weak_stub_controller.h"

namespace fidl {
namespace internal {

StubController::StubController() : weak_(nullptr), reader_(this) {}

StubController::~StubController() { InvalidateWeakIfNeeded(); }

zx_status_t StubController::Send(const fidl_type_t* type, Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
    return status;
  }
  return message.Write(reader_.channel().get(), 0);
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
  return stub_->Dispatch_(std::move(message), PendingResponse(txid, weak));
}

void StubController::OnChannelGone() { InvalidateWeakIfNeeded(); }

void StubController::InvalidateWeakIfNeeded() {
  if (!weak_)
    return;
  weak_->Invalidate();
  weak_->Release();
  weak_ = nullptr;
}

}  // namespace internal
}  // namespace fidl
