// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "request_queue.h"

void RequestQueue::RequestDone() {
  // Free up a request.
  available_requests_++;

  // Start any queued requests.
  while (available_requests_ > 0 && !requests_.empty()) {
    available_requests_--;
    async::PostTask(dispatcher_, [this, request = std::move(requests_.front())]() mutable {
      request(Request(this));
    });
    requests_.pop();
  }
}

RequestQueue::Request::~Request() { Finish(); }

RequestQueue::Request& RequestQueue::Request::operator=(Request&& other) noexcept {
  Finish();
  parent_ = other.parent_;
  other.parent_ = nullptr;
  return *this;
}

void RequestQueue::Request::Finish() {
  if (parent_ != nullptr) {
    parent_->RequestDone();
    parent_ = nullptr;
  }
}
