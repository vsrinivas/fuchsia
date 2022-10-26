// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/testing/coverage.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeCoverage::FakeCoverage(ExecutorPtr executor)
    : collector_(this), provider_(this), executor_(executor), receiver_(&sender_) {}

fidl::InterfaceRequestHandler<Publisher> FakeCoverage::GetPublisherHandler() {
  return [this](fidl::InterfaceRequest<Publisher> request) {
    auto handler = GetCollectorHandler();
    // See target/instrumented-process.cc.
    // This fakes the protocol recasting performed by test_manager's `fuzz_coverage` component.
    handler(fidl::InterfaceRequest<CoverageDataCollector>(request.TakeChannel()));
  };
}

fidl::InterfaceRequestHandler<CoverageDataCollector> FakeCoverage::GetCollectorHandler() {
  return [this](fidl::InterfaceRequest<CoverageDataCollector> request) {
    if (auto status = collector_.Bind(request.TakeChannel(), executor_->dispatcher());
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to bind fuchsia.fuzzer.CoverageDataCollector request: "
                     << zx_status_get_string(status);
    }
  };
}

fidl::InterfaceRequestHandler<CoverageDataProvider> FakeCoverage::GetProviderHandler() {
  return [this](fidl::InterfaceRequest<CoverageDataProvider> request) {
    if (auto status = provider_.Bind(std::move(request), executor_->dispatcher());
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to bind fuchsia.fuzzer.CoverageDataProvider request: "
                     << zx_status_get_string(status);
    }
  };
}

void FakeCoverage::Initialize(InstrumentedProcess instrumented, InitializeCallback callback) {
  auto coverage_data = CoverageData::WithInstrumented(std::move(instrumented));
  if (auto status = sender_.Send(std::move(coverage_data)); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to send instrumented process to provider: "
                   << zx_status_get_string(status);
    return;
  }
  callback(CopyOptions(options_));
}

void FakeCoverage::AddLlvmModule(zx::vmo inline_8bit_counters, AddLlvmModuleCallback callback) {
  auto coverage_data = CoverageData::WithInline8bitCounters(std::move(inline_8bit_counters));
  if (auto status = sender_.Send(std::move(coverage_data)); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to send inline 8-bit counters to provider: "
                   << zx_status_get_string(status);
    return;
  }
  callback();
}

void FakeCoverage::SetOptions(Options options) { options_ = std::move(options); }

void FakeCoverage::GetCoverageData(GetCoverageDataCallback callback) {
  auto task = receiver_.Receive()
                  .and_then([callback = std::move(callback)](
                                CoverageData& coverage_data) mutable -> Result<> {
                    callback(std::move(coverage_data));
                    return fpromise::ok();
                  })
                  .wrap_with(scope_);
  executor_->schedule_task(std::move(task));
}

}  // namespace fuzzing
