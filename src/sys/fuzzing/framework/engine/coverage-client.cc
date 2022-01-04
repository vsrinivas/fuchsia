// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <atomic>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"

namespace fuzzing {

using fuchsia::fuzzer::CoverageEventPtr;
using fuchsia::fuzzer::CoverageProvider;
using fuchsia::fuzzer::CoverageProviderPtr;

CoverageProviderClient::CoverageProviderClient() : dispatcher_(std::make_shared<Dispatcher>()) {
  request_ = provider_.NewRequest(dispatcher_->get());
}

CoverageProviderClient::~CoverageProviderClient() { Close(); }

void CoverageProviderClient::Configure(const std::shared_ptr<Options>& options) {
  provider_->SetOptions(CopyOptions(*options));
}

fidl::InterfaceRequest<CoverageProvider> CoverageProviderClient::TakeRequest() {
  FX_CHECK(request_);
  return std::move(request_);
}

void CoverageProviderClient::OnEvent(fit::function<void(CoverageEvent)> on_event) {
  FX_CHECK(!loop_.joinable());
  loop_ = std::thread([this, on_event = std::move(on_event)]() {
    while (true) {
      CoverageEvent event;
      provider_->WatchCoverageEvent([this, &event](CoverageEvent response) {
        event = std::move(response);
        sync_.Signal();
      });
      sync_.WaitFor("the next coverage event");
      sync_.Reset();
      if (closing_) {
        break;
      }
      on_event(std::move(event));
    }
    provider_.Unbind();
  });
}

void CoverageProviderClient::Close() {
  if (closing_.exchange(true)) {
    return;
  }
  sync_.Signal();
  if (loop_.joinable()) {
    loop_.join();
  }
}

}  // namespace fuzzing
