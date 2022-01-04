// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/provider.h"

namespace fuzzing {

CoverageProviderImpl::CoverageProviderImpl(std::shared_ptr<CoverageEventQueue> events)
    : binding_(this), events_(events) {
  loop_ = std::thread([this]() FXL_LOCKS_EXCLUDED(mutex_) {
    while (true) {
      request_.WaitFor("coverage request");
      WatchCoverageEventCallback callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = std::move(callback_);
        callback_ = nullptr;
        request_.Reset();
      }
      if (closing_) {
        break;
      }
      auto event = events_->GetEvent();
      if (!event) {
        break;
      }
      binding_.PostTask([callback = std::move(callback), event = std::move(*event)]() mutable {
        callback(std::move(event));
      });
    }
    binding_.Unbind();
  });
}

CoverageProviderImpl::~CoverageProviderImpl() {
  closing_ = true;
  connect_.Signal();
  request_.Signal();
  events_->Stop();
  if (loop_.joinable()) {
    loop_.join();
  }
}

fidl::InterfaceRequestHandler<CoverageProvider> CoverageProviderImpl::GetHandler() {
  return [this](fidl::InterfaceRequest<CoverageProvider> request) {
    binding_.Bind(std::move(request));
    connect_.Signal();
  };
}

void CoverageProviderImpl::SetOptions(Options options) { events_->SetOptions(std::move(options)); }

void CoverageProviderImpl::WatchCoverageEvent(WatchCoverageEventCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
  request_.Signal();
}

void CoverageProviderImpl::AwaitConnect() { connect_.WaitFor("connection"); }

void CoverageProviderImpl::AwaitClose() { binding_.AwaitClose(); }

}  // namespace fuzzing
