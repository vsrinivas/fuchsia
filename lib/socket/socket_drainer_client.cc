// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/socket/socket_drainer_client.h"

#include <utility>

namespace socket {

SocketDrainerClient::SocketDrainerClient() : drainer_(this) {}

SocketDrainerClient::~SocketDrainerClient() {}

void SocketDrainerClient::Start(
    zx::socket source, const std::function<void(std::string)>& callback) {
  callback_ = callback;
  drainer_.Start(std::move(source));
}

void SocketDrainerClient::OnDataAvailable(const void* data, size_t num_bytes) {
  data_.append(static_cast<const char*>(data), num_bytes);
}

void SocketDrainerClient::OnDataComplete() {
  if (destruction_sentinel_.DestructedWhile([this] { callback_(data_); })) {
    return;
  }
  if (on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace socket
