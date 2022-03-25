// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/provider.h"

namespace fuzzing {

CoverageProviderImpl::CoverageProviderImpl(ExecutorPtr executor, OptionsPtr options,
                                           AsyncDequePtr<CoverageEvent> events)
    : binding_(this),
      executor_(std::move(executor)),
      options_(std::move(options)),
      events_(std::move(events)) {}

fidl::InterfaceRequestHandler<CoverageProvider> CoverageProviderImpl::GetHandler() {
  return [this](fidl::InterfaceRequest<CoverageProvider> request) {
    binding_.Bind(std::move(request), executor_->dispatcher());
  };
}

void CoverageProviderImpl::SetOptions(Options options) { *options_ = std::move(options); }

void CoverageProviderImpl::WatchCoverageEvent(WatchCoverageEventCallback callback) {
  auto task = fpromise::make_promise([this, event = Future<CoverageEvent>(),
                                      callback = std::move(callback)](
                                         Context& context) mutable -> Result<> {
                if (!event) {
                  event = events_->Receive();
                }
                if (!event(context)) {
                  return fpromise::pending();
                }
                if (event.is_ok()) {
                  callback(event.take_value());
                } else {
                  binding_.Unbind();
                }
                return fpromise::ok();
              }).wrap_with(scope_);
  executor_->schedule_task(std::move(task));
}

}  // namespace fuzzing
