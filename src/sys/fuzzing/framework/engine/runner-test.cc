// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner-test.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace fuzzing {

void RunnerImplTest::SetUp() {
  RunnerTest::SetUp();
  runner_ = RunnerImpl::MakePtr(executor());
  auto runner_impl = std::static_pointer_cast<RunnerImpl>(runner_);

  target_adapter_ = std::make_unique<FakeTargetAdapter>(executor());
  runner_impl->set_target_adapter_handler(target_adapter_->GetHandler());

  coverage_forwarder_ = std::make_unique<CoverageForwarder>(executor());
  auto handler = coverage_forwarder_->GetCoverageProviderHandler();
  runner_impl->set_coverage_provider_handler(std::move(handler));

  process_ = std::make_unique<FakeProcess>(executor());
  process_->set_handler(coverage_forwarder_->GetInstrumentationHandler());
}

void RunnerImplTest::SetAdapterParameters(const std::vector<std::string>& parameters) {
  target_adapter_->SetParameters(parameters);
}

ZxPromise<Input> RunnerImplTest::GetTestInput() {
  return target_adapter_->AwaitStart()
      .and_then([launch = ZxFuture<>(process_->Launch())](Context& context,
                                                          Input& input) mutable -> ZxResult<Input> {
        if (!launch(context)) {
          return fpromise::pending();
        }
        if (launch.is_error()) {
          return fpromise::error(launch.error());
        }
        return fpromise::ok(std::move(input));
      })
      .wrap_with(scope_);
}

ZxPromise<> RunnerImplTest::SetFeedback(Coverage coverage, FuzzResult fuzz_result, bool leak) {
  return fpromise::make_promise(
             [this, coverage = std::move(coverage), leak, fuzz_result]() mutable -> ZxResult<> {
               if (fuzz_result != FuzzResult::NO_ERRORS) {
                 return fpromise::ok();
               }
               // Fake some activity within the process.
               process_->SetCoverage(coverage);
               process_->SetLeak(leak);
               return AsZxResult(target_adapter_->Finish());
             })
      .and_then([this, fuzz_result, target = ZxFuture<>(),
                 disconnect = ZxFuture<>()](Context& context) mutable -> ZxResult<> {
        if (!target) {
          switch (fuzz_result) {
            case FuzzResult::NO_ERRORS:
              target = process_->AwaitFinish();
              break;
            case FuzzResult::BAD_MALLOC:
              target = process_->ExitAsync(options()->malloc_exitcode());
              break;
            case FuzzResult::CRASH:
              target = process_->CrashAsync();
              break;
            case FuzzResult::DEATH:
              target = process_->ExitAsync(options()->death_exitcode());
              break;
            case FuzzResult::EXIT:
              target = process_->ExitAsync(1);
              break;
            case FuzzResult::LEAK:
              target = process_->ExitAsync(options()->leak_exitcode());
              break;
            case FuzzResult::OOM:
              target = process_->ExitAsync(options()->oom_exitcode());
              break;
            case FuzzResult::TIMEOUT:
              // Don't signal from the target adapter and don't exit the fake process; just wait.
              // Eventually, the runner's will time out and kill the process.
              target = executor()->MakePromiseForTime(zx::time::infinite()).or_else([] {
                return fpromise::error(ZX_ERR_TIMED_OUT);
              });
              break;
          }
        }
        if (!target(context)) {
          return fpromise::pending();
        }
        if (target.is_error()) {
          return fpromise::error(target.error());
        }
        if (process_->is_running()) {
          return fpromise::ok();
        }
        if (fuzz_result == FuzzResult::EXIT && !options()->detect_exits()) {
          return fpromise::ok();
        }
        // In most cases, the fake process stops, and unless the error is recoverable the target
        // adapter should, too.
        if (!disconnect) {
          disconnect = target_adapter_->AwaitDisconnect();
        }
        if (!disconnect(context)) {
          return fpromise::pending();
        }
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

}  // namespace fuzzing
