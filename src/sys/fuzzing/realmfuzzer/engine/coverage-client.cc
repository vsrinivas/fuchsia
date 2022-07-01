// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/coverage-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"

namespace fuzzing {

using fuchsia::fuzzer::CoverageEventPtr;
using fuchsia::fuzzer::CoverageProvider;
using fuchsia::fuzzer::CoverageProviderPtr;

CoverageProviderClient::CoverageProviderClient(ExecutorPtr executor)
    : executor_(std::move(executor)) {}

void CoverageProviderClient::SetOptions(const OptionsPtr& options) {
  Connect();
  provider_->SetOptions(CopyOptions(*options));
}

Promise<CoverageEvent> CoverageProviderClient::WatchCoverageEvent() {
  Connect();
  Bridge<CoverageEvent> bridge;
  provider_->WatchCoverageEvent(bridge.completer.bind());
  return bridge.consumer.promise_or(fpromise::error());
}

void CoverageProviderClient::Connect() {
  if (provider_) {
    return;
  }
  FX_DCHECK(handler_);
  handler_(provider_.NewRequest(executor_->dispatcher()));
  provider_.set_error_handler([this](zx_status_t status) { provider_ = nullptr; });
}

}  // namespace fuzzing
