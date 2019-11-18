// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/proxy_controller.h"

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <utility>

#include "lib/fidl/cpp/internal/logging.h"

namespace fidl {
namespace internal {
namespace {

constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

// RAII managed heap allocated storage for raw message bytes. Used to hold
// the temporary output of fidl_transform (see ProxyController::Send)
struct HeapAllocatedMessage {
  HeapAllocatedMessage() : data(static_cast<uint8_t*>(malloc(ZX_CHANNEL_MAX_MSG_BYTES))) {}
  ~HeapAllocatedMessage() { free(data); }

  uint8_t* data;
};

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

zx_status_t ProxyController::Send(const fidl_type_t* type, Message message,
                                  std::unique_ptr<MessageHandler> response_handler) {
  zx_txid_t txid = 0;
  if (response_handler) {
    txid = next_txid_++ & kUserspaceTxidMask;
    while (!txid || handlers_.find(txid) != handlers_.end())
      txid = next_txid_++ & kUserspaceTxidMask;
    message.set_txid(txid);
  }
  const char* error_msg = nullptr;
  auto header = message.header();
  if (!fidl_should_decode_union_from_xunion(&header)) {
    zx_status_t status = message.Validate(type, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
      return status;
    }
  } else {
    // When the FIDL bindings are configured to write wire format v1, the Message
    // bytes and coding table passed to ProxyController::Send are not in a format
    // that can be validated using fidl_validate. To get around this, we call
    // fidl_transform to write the message bytes into the old format and then call
    // fidl_validate on it, which also serves to validate the message bytes in the v1
    // format.
    HeapAllocatedMessage old_bytes;
    if (!old_bytes.data) {
      return ZX_ERR_BAD_STATE;
    }
    uint32_t actual_old_bytes;
    fidl_type_t v1_type = get_alt_type(type);
    zx_status_t status = fidl_transform(
        FIDL_TRANSFORMATION_V1_TO_OLD, &v1_type, message.bytes().data(), message.bytes().actual(),
        old_bytes.data, ZX_CHANNEL_MAX_MSG_BYTES, &actual_old_bytes, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
      return status;
    }

    status = fidl_validate(type, old_bytes.data, actual_old_bytes, message.handles().actual(),
                           &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
      return status;
    }
  }
  zx_status_t status = message.Write(reader_.channel().get(), 0);
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
  // Avoid reentrancy problems by first copying the handlers map.
  auto doomed = std::move(handlers_);
  next_txid_ = 1;
  doomed.clear();
}

}  // namespace internal
}  // namespace fidl
