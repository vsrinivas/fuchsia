// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/testing/relay.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

fidl::InterfaceRequestHandler<Relay> RelayImpl::GetHandler(async_dispatcher_t* dispatcher) {
  return [this, dispatcher](fidl::InterfaceRequest<Relay> request) {
    bindings_.AddBinding(this, std::move(request), dispatcher);
  };
}

void RelayImpl::SetTestData(SignaledBuffer data) {
  data_ = SignaledBuffer::New();
  *data_ = std::move(data);
  MaybeCallback();
}

void RelayImpl::WatchTestData(WatchTestDataCallback callback) {
  callback_ = std::move(callback);
  MaybeCallback();
}

void RelayImpl::MaybeCallback() {
  if (data_ && callback_) {
    callback_(std::move(*data_));
    data_ = nullptr;
    callback_ = nullptr;
  }
}

}  // namespace fuzzing
