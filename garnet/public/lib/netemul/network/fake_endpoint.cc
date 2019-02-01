// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_endpoint.h"

namespace netemul {

FakeEndpoint::FakeEndpoint(
    data::BusConsumer::Ptr sink,
    fidl::InterfaceRequest<FakeEndpoint::FFakeEndpoint> request,
    async_dispatcher_t* dispatcher)
    : sink_(std::move(sink)),
      binding_(this, std::move(request), dispatcher),
      weak_ptr_factory_(this) {
  binding_.set_error_handler([this](zx_status_t err) {
    if (on_disconnected_) {
      on_disconnected_(this);
    }
  });
}

void FakeEndpoint::SetOnDisconnected(OnDisconnectedCallback cl) {
  on_disconnected_ = std::move(cl);
}
fxl::WeakPtr<data::Consumer> FakeEndpoint::GetPointer() {
  return weak_ptr_factory_.GetWeakPtr();
}

void FakeEndpoint::Consume(const void* data, size_t len) {
  // copy data to fidl vec:
  fidl::VectorPtr<uint8_t> vec(len);
  memcpy(vec->data(), data, len);
  binding_.events().OnData(std::move(vec));
}

void FakeEndpoint::Write(::std::vector<uint8_t> data) {
  if (!sink_) {
    // sink has disappeared from under us!
    // close the binding
    binding_.Close(ZX_ERR_PEER_CLOSED);
    return;
  }

  sink_->Consume(data.data(), data.size(), GetPointer());
}

}  // namespace netemul
