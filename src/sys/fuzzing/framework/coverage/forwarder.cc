// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/forwarder.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

namespace fuzzing {

CoverageForwarder::CoverageForwarder(ExecutorPtr executor) : executor_(std::move(executor)) {
  options_ = MakeOptions();
  events_ = AsyncDeque<CoverageEvent>::MakePtr();
  provider_ = std::make_unique<CoverageProviderImpl>(executor_, options_, events_);
}

fidl::InterfaceRequestHandler<Instrumentation> CoverageForwarder::GetInstrumentationHandler() {
  return [this](fidl::InterfaceRequest<Instrumentation> request) {
    auto target_id = ++last_target_id_;
    auto instrumentation = std::make_unique<InstrumentationImpl>(target_id, options_, events_);
    auto* dispatcher = executor_->dispatcher();
    instrumentations_.AddBinding(std::move(instrumentation), std::move(request), dispatcher);
  };
}

fidl::InterfaceRequestHandler<CoverageProvider> CoverageForwarder::GetCoverageProviderHandler() {
  return [this](fidl::InterfaceRequest<CoverageProvider> request) {
    auto handler = provider_->GetHandler();
    handler(std::move(request));
  };
}

}  // namespace fuzzing
