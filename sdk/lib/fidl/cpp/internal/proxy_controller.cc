// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/proxy_controller.h"

#include <atomic>
#include <cstdint>
#include <utility>

#include "lib/fidl/cpp/internal/logging.h"

namespace fidl {
namespace internal {
namespace {

constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

// Enables client side error callbacks, which will eventually always be
// enabled. This should generally not be set, but exists to provide a
// mechanism to denylist test cases
// TODO(fxbug.dev/68206) Remove this.
static std::atomic<uint32_t> transitory_clientside_error_disable_count(0);

}  // namespace

ProxyController::ProxyController() : reader_(this), next_txid_(1) {}

ProxyController::~ProxyController() = default;

ProxyController::ProxyController(ProxyController&& other)
    : reader_(this), handlers_(std::move(other.handlers_)), next_txid_(other.next_txid_) {
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

void ProxyController::Send(const fidl_type_t* type, HLCPPOutgoingMessage message,
                           std::unique_ptr<SingleUseMessageHandler> response_handler) {
  zx_txid_t txid = 0;
  if (response_handler) {
    txid = next_txid_++ & kUserspaceTxidMask;
    while (!txid || handlers_.find(txid) != handlers_.end())
      txid = next_txid_++ & kUserspaceTxidMask;
    message.set_txid(txid);
  }

  if (type != nullptr) {
    const char* error_msg = nullptr;
    zx_status_t status = message.Validate(type, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_VALIDATING_ERROR(message, type, error_msg);
      if (transitory_clientside_error_disable_count == 0) {
        if (reader_.error_handler_ != nullptr) {
          reader_.error_handler_(status);
        }
        reader_.Reset();
      }
      return;
    }
  } else if (unlikely(!message.has_only_header())) {
    return;
  }

  zx_status_t status = message.Write(reader_.channel().get(), 0);
  if (status != ZX_OK) {
    // Channel closure always races with any channel write that's been started but not yet
    // completed, so ZX_ERR_PEER_CLOSED is expected to occur sometimes under normal operation.
    if (status != ZX_ERR_PEER_CLOSED) {
      FIDL_REPORT_CHANNEL_WRITING_ERROR(message, type, status);
      if (transitory_clientside_error_disable_count == 0) {
        if (reader_.error_handler_ != nullptr) {
          reader_.error_handler_(status);
        }
        reader_.Reset();
      }
    }
    return;
  }
  if (response_handler)
    handlers_.emplace(txid, std::move(response_handler));
}

void ProxyController::Reset() {
  reader_.Reset();
  ClearPendingHandlers();
}

zx_status_t ProxyController::OnMessage(HLCPPIncomingMessage message) {
  zx_txid_t txid = message.txid();
  if (!txid) {
    if (!proxy_)
      return ZX_ERR_NOT_SUPPORTED;
    return proxy_->Dispatch_(std::move(message));
  }
  auto it = handlers_.find(txid);
  if (it == handlers_.end())
    return ZX_ERR_NOT_FOUND;
  std::unique_ptr<SingleUseMessageHandler> handler = std::move(it->second);
  handlers_.erase(it);
  return (*handler)(std::move(message));
}

void ProxyController::OnChannelGone() { ClearPendingHandlers(); }

void ProxyController::ClearPendingHandlers() {
  // Avoid reentrancy problems by first copying the handlers map.
  auto doomed = std::move(handlers_);
  next_txid_ = 1;
  doomed.clear();
}

TransitoryProxyControllerClientSideErrorDisabler::
    TransitoryProxyControllerClientSideErrorDisabler() {
  ++transitory_clientside_error_disable_count;
}
TransitoryProxyControllerClientSideErrorDisabler::
    ~TransitoryProxyControllerClientSideErrorDisabler() {
  --transitory_clientside_error_disable_count;
}

}  // namespace internal
}  // namespace fidl
