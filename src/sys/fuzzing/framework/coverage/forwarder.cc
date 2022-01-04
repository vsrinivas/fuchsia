// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/forwarder.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

namespace fuzzing {

CoverageForwarder::CoverageForwarder() : events_(std::make_shared<CoverageEventQueue>()) {
  provider_ = std::make_unique<CoverageProviderImpl>(events_);
}

fidl::InterfaceRequestHandler<Instrumentation> CoverageForwarder::GetInstrumentationHandler() {
  return [this](fidl::InterfaceRequest<Instrumentation> request) {
    if (closing_) {
      return;
    }
    auto target_id = ++last_target_id_;
    auto instrumentation = std::make_unique<InstrumentationImpl>(target_id, events_);
    instrumentations_.AddBinding(std::move(instrumentation), std::move(request), dispatcher_.get());
  };
}

fidl::InterfaceRequestHandler<CoverageProvider> CoverageForwarder::GetCoverageProviderHandler() {
  return [this](fidl::InterfaceRequest<CoverageProvider> request) {
    auto handler = provider_->GetHandler();
    handler(std::move(request));
  };
}

void CoverageForwarder::Run() {
  provider_->AwaitConnect();
  provider_->AwaitClose();
  closing_ = true;
  SyncWait sync;
  dispatcher_.PostTask([this, &sync]() {
    instrumentations_.CloseAll();
    sync.Signal();
  });
  sync.WaitFor("instrumentation clients to disconnect");
}

}  // namespace fuzzing
