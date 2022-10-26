// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data-provider-client.h"

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

CoverageDataProviderClient::CoverageDataProviderClient(ExecutorPtr executor)
    : executor_(std::move(executor)), options_(MakeOptions()), receiver_(&sender_) {}

void CoverageDataProviderClient::Configure(const OptionsPtr& options) {
  options_ = options;
  if (provider_.is_bound()) {
    provider_->SetOptions(CopyOptions(*options_));
  }
}

zx_status_t CoverageDataProviderClient::Bind(zx::channel channel) {
  FX_CHECK(!provider_.is_bound());
  if (auto status = provider_.Bind(std::move(channel), executor_->dispatcher()); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to bind fuchsia.fuzzer.CoverageDataProviderClient proxy: "
                     << zx_status_get_string(status);
    return status;
  }
  provider_->SetOptions(CopyOptions(*options_));
  // |GetCoverageData| futures may be abandoned. To prevent coverage data being dropped, a separate
  // future, scoped to this object, should handle the FIDL requests and responses.
  auto task = fpromise::make_promise([this, watch = Future<CoverageData>()](
                                         Context& context) mutable -> Result<> {
                while (true) {
                  if (!watch) {
                    Bridge<CoverageData> bridge;
                    provider_->GetCoverageData(bridge.completer.bind());
                    watch = bridge.consumer.promise_or(fpromise::error());
                  }
                  if (!watch(context)) {
                    return fpromise::pending();
                  }
                  if (watch.is_error()) {
                    FX_LOGS(WARNING) << "Failed to receive coverage data.";
                    return fpromise::error();
                  }
                  if (auto status = sender_.Send(watch.take_value()); status != ZX_OK) {
                    FX_LOGS(WARNING) << "Failed to forward received coverage data: "
                                     << zx_status_get_string(status);
                  }
                }
              }).wrap_with(scope_);
  executor_->schedule_task(std::move(task));
  return ZX_OK;
}

Promise<CoverageData> CoverageDataProviderClient::GetCoverageData() { return receiver_.Receive(); }

}  // namespace fuzzing
