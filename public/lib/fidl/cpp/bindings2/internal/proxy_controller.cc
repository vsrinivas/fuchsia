// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"

#include <utility>

namespace fidl {
namespace internal {

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
    MessageBuilder* builder,
    std::unique_ptr<MessageHandler> response_handler) {
  zx_txid_t txid = 0;
  if (response_handler) {
    txid = next_txid_++;
    while (!txid || handlers_.find(txid) != handlers_.end())
      txid = next_txid_++;
    builder->header()->txid = txid;
  }
  Message message;
  const char* error_msg = nullptr;
  zx_status_t status = builder->Encode(&message, &error_msg);
  if (status != ZX_OK)
    return status;
  status = message.Write(reader_.channel().get(), 0);
  if (status != ZX_OK)
    return status;
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
  // TODO(abarth): Implement events.
  if (!txid)
    return ZX_ERR_NOT_SUPPORTED;
  auto it = handlers_.find(txid);
  if (it == handlers_.end())
    return ZX_ERR_NOT_FOUND;
  std::unique_ptr<MessageHandler> handler = std::move(it->second);
  handlers_.erase(it);
  return handler->OnMessage(std::move(message));
}

void ProxyController::OnChannelGone() {
  ClearPendingHandlers();
}

void ProxyController::ClearPendingHandlers() {
  handlers_.clear();
  next_txid_ = 1;
}

}  // namespace internal
}  // namespace fidl
