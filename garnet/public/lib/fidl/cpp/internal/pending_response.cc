// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/pending_response.h"

#include "lib/fidl/cpp/internal/logging.h"
#include "lib/fidl/cpp/internal/stub_controller.h"
#include "lib/fidl/cpp/internal/weak_stub_controller.h"

namespace fidl {
namespace internal {

PendingResponse::PendingResponse() : txid_(0), weak_controller_(nullptr) {}

PendingResponse::PendingResponse(zx_txid_t txid,
                                 WeakStubController* weak_controller)
    : txid_(txid), weak_controller_(weak_controller) {
  if (weak_controller_)
    weak_controller_->AddRef();
}

PendingResponse::~PendingResponse() {
  if (weak_controller_)
    weak_controller_->Release();
}

PendingResponse::PendingResponse(const PendingResponse& other)
    : PendingResponse(other.txid_, other.weak_controller_) {}

PendingResponse& PendingResponse::operator=(const PendingResponse& other) {
  if (this == &other)
    return *this;
  txid_ = other.txid_;
  if (weak_controller_)
    weak_controller_->Release();
  weak_controller_ = other.weak_controller_;
  if (weak_controller_)
    weak_controller_->AddRef();
  return *this;
}

PendingResponse::PendingResponse(PendingResponse&& other)
    : txid_(other.txid_), weak_controller_(other.weak_controller_) {
  other.weak_controller_ = nullptr;
}

PendingResponse& PendingResponse::operator=(PendingResponse&& other) {
  if (this == &other)
    return *this;
  txid_ = other.txid_;
  if (weak_controller_)
    weak_controller_->Release();
  weak_controller_ = other.weak_controller_;
  other.weak_controller_ = nullptr;
  return *this;
}

zx_status_t PendingResponse::Send(const fidl_type_t* type, Message message) {
  if (!weak_controller_)
    return ZX_ERR_BAD_STATE;
  StubController* controller = weak_controller_->controller();
  if (!controller)
    return ZX_ERR_BAD_STATE;
  message.set_txid(txid_);
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
    return status;
  }
  zx_handle_t channel = controller->reader().channel().get();
  return message.Write(channel, 0);
}

}  // namespace internal
}  // namespace fidl
