// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/proxy_controller.h"

#include <utility>

#include "lib/fidl/cpp/internal/logging.h"

namespace fidl {
namespace internal {
namespace {

constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

}  // namespace

ProxyController::ProxyController() : reader_(this), next_txid_(1) {}

ProxyController::~ProxyController() = default;

ProxyController::ProxyController(ProxyController&& other)
    : reader_(this),
      handlers_(std::move(other.handlers_)),
      next_txid_(other.next_txid_) {
  reader_.TakeChannelAndErrorHandlerFrom(&other.reader());
  other.Reset();
}

ProxyController& ProxyController::operator=(ProxyController&& other) {
  if (this != &other) {
    reader_.TakeChannelAndErrorHandlerFrom(&other.reader());
    handlers_ = std::move(other.handlers_);
    next_txid_ = other.next_txid_;
    other.Reset();
  }
  return *this;
}

zx_status_t ProxyController::Send(
    const fidl_type_t* type, Message message,
    std::unique_ptr<MessageHandler> response_handler) {
  zx_txid_t txid = 0;
  if (response_handler) {
    txid = next_txid_++ & kUserspaceTxidMask;
    while (!txid || handlers_.find(txid) != handlers_.end())
      txid = next_txid_++ & kUserspaceTxidMask;
    message.set_txid(txid);
  }
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
    return status;
  }
  status = message.Write(reader_.channel().get(), 0);
  if (status != ZX_OK) {
    FIDL_REPORT_CHANNEL_WRITING_ERROR(message, type, status);
    return status;
  }
  if (response_handler)
    handlers_.emplace(txid, std::move(response_handler));
  return ZX_OK;
}

void ProxyController::Reset() {
  reader_.Reset();
  ClearPendingHandlers();
}

zx_status_t ProxyController::OnMessage(Message message) {
  if (!message.has_header())
    return ZX_ERR_INVALID_ARGS;
  zx_txid_t txid = message.txid();
  if (!txid) {
    if (!proxy_)
      return ZX_ERR_NOT_SUPPORTED;
    return proxy_->Dispatch_(std::move(message));
  }
  auto it = handlers_.find(txid);
  if (it == handlers_.end())
    return ZX_ERR_NOT_FOUND;
  std::unique_ptr<MessageHandler> handler = std::move(it->second);
  handlers_.erase(it);
  return handler->OnMessage(std::move(message));
}

void ProxyController::OnChannelGone() { ClearPendingHandlers(); }

void ProxyController::ClearPendingHandlers() {
  handlers_.clear();
  next_txid_ = 1;
}

}  // namespace internal
}  // namespace fidl
