// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_endpoint.h"

namespace netemul {

FakeEndpoint::FakeEndpoint(data::BusConsumer::Ptr sink,
                           fidl::InterfaceRequest<FakeEndpoint::FFakeEndpoint> request,
                           async_dispatcher_t* dispatcher)
    : sink_(std::move(sink)),
      binding_(this, std::move(request), dispatcher),
      weak_ptr_factory_(this),
      dropped_(0) {
  binding_.set_error_handler([this](zx_status_t err) {
    if (on_disconnected_) {
      on_disconnected_();
    }
  });
}

void FakeEndpoint::SetOnDisconnected(OnDisconnectedCallback cl) {
  on_disconnected_ = std::move(cl);
}
fxl::WeakPtr<data::Consumer> FakeEndpoint::GetPointer() { return weak_ptr_factory_.GetWeakPtr(); }

void FakeEndpoint::Consume(const void* data, size_t len) {
  // copy data to fidl vec:
  std::vector<uint8_t> vec(len);
  memcpy(vec.data(), data, len);

  pending_frames_.push(std::move(vec));
  if (pending_frames_.size() > kMaxPendingFrames) {
    pending_frames_.pop();
    dropped_++;
  }
  PopReadQueue();
}

void FakeEndpoint::Write(::std::vector<uint8_t> data, WriteCallback callback) {
  if (!sink_) {
    // sink has disappeared from under us!
    // close the binding
    binding_.Close(ZX_ERR_PEER_CLOSED);
    return;
  }

  sink_->Consume(data.data(), data.size(), GetPointer());
  callback();
}

void FakeEndpoint::Read(ReadCallback callback) {
  if (pending_callback_.has_value()) {
    // Not allowed to enqueue two calls.
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }
  pending_callback_ = std::move(callback);
  PopReadQueue();
}

void FakeEndpoint::PopReadQueue() {
  if (pending_callback_.has_value() && !pending_frames_.empty()) {
    (*pending_callback_)(std::move(pending_frames_.front()), dropped_);
    dropped_ = 0;
    pending_frames_.pop();
    pending_callback_.reset();
  }
}

}  // namespace netemul
