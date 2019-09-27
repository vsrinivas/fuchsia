// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/socket/socket_drainer_client.h"

#include <lib/fit/function.h>

#include <utility>

namespace socket {

SocketDrainerClient::SocketDrainerClient() : drainer_(this) {}

SocketDrainerClient::~SocketDrainerClient() {}

void SocketDrainerClient::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool SocketDrainerClient::IsDiscardable() const { return discardable_; }

void SocketDrainerClient::Start(zx::socket source, fit::function<void(std::string)> callback) {
  callback_ = std::move(callback);
  drainer_.Start(std::move(source));
}

void SocketDrainerClient::OnDataAvailable(const void* data, size_t num_bytes) {
  data_.append(static_cast<const char*>(data), num_bytes);
}

void SocketDrainerClient::OnDataComplete() {
  if (destruction_sentinel_.DestructedWhile([this] { callback_(data_); })) {
    return;
  }
  discardable_ = true;
  if (on_discardable_) {
    on_discardable_();
  }
}

}  // namespace socket
