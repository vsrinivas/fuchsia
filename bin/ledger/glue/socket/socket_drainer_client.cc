// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/socket/socket_drainer_client.h"

#include <utility>

namespace glue {

SocketDrainerClient::SocketDrainerClient() : drainer_(this) {}

SocketDrainerClient::~SocketDrainerClient() {}

void SocketDrainerClient::Start(
    mx::socket source,
    const std::function<void(std::string)>& callback) {
  callback_ = callback;
  drainer_.Start(std::move(source));
}

void SocketDrainerClient::OnDataAvailable(const void* data, size_t num_bytes) {
  data_.append(static_cast<const char*>(data), num_bytes);
}

void SocketDrainerClient::OnDataComplete() {
  ftl::Closure on_empty_callback = std::move(on_empty_callback_);
  callback_(data_);
  // This class might be deleted here. Do not access any field.
  if (on_empty_callback)
    on_empty_callback();
}

}  // namespace glue
