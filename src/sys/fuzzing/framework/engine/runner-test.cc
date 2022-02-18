// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner-test.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace fuzzing {

void RunnerImplTest::SetAdapterParameters(const std::vector<std::string>& parameters) {
  target_adapter_.SetParameters(parameters);
}

void RunnerImplTest::Configure(Runner* runner, const std::shared_ptr<Options>& options) {
  RunnerTest::Configure(runner, options);
  auto* runner_impl = static_cast<RunnerImpl*>(runner);
  stopped_ = true;

  auto coverage_provider = std::make_unique<CoverageProviderClient>();
  auto handler = coverage_forwarder_.GetCoverageProviderHandler();
  handler(coverage_provider->TakeRequest());
  runner_impl->SetCoverageProvider(std::move(coverage_provider));

  auto target_adapter_client = std::make_unique<TargetAdapterClient>(target_adapter_.GetHandler());
  runner_impl->SetTargetAdapter(std::move(target_adapter_client));
}

bool RunnerImplTest::HasTestInput(zx::time deadline) {
  zx_signals_t observed;
  return target_adapter_.AwaitSignal(deadline, &observed) == ZX_OK && observed == kStart;
}

Input RunnerImplTest::GetTestInput() {
  if (stopped_) {
    auto instrumentation_handler = coverage_forwarder_.GetInstrumentationHandler();
    instrumentation_handler(process_.NewRequest());
    process_.AddProcess();
    process_.AddLlvmModule();
  }
  return target_adapter_.test_input();
}

void RunnerImplTest::SetFeedback(const Coverage& coverage, FuzzResult result, bool leak) {
  // Fake some activity within the process.
  process_.SetCoverage(coverage);
  process_.SetLeak(leak);
  // In most cases, the fake process stops, and unless the error is recoverable the target adapter
  // should, too.
  stopped_ = true;
  bool fatal = true;
  switch (result) {
    case FuzzResult::NO_ERRORS:
      // Finish the run normally.
      target_adapter_.SignalPeer(kFinish);
      stopped_ = false;
      break;
    case FuzzResult::BAD_MALLOC:
      process_.Exit(options()->malloc_exitcode());
      break;
    case FuzzResult::CRASH:
      process_.Crash();
      break;
    case FuzzResult::DEATH:
      process_.Exit(options()->death_exitcode());
      break;
    case FuzzResult::EXIT:
      process_.Exit(1);
      fatal = options()->detect_exits();
      break;
    case FuzzResult::LEAK:
      process_.Exit(options()->leak_exitcode());
      break;
    case FuzzResult::OOM:
      process_.Exit(options()->oom_exitcode());
      break;
    case FuzzResult::TIMEOUT:
      // Don't signal from the target adapter and don't exit the fake process; just... wait.
      // Eventually, the Runner's Timer thread will time out and kill the process.
      break;
    default:
      FX_NOTREACHED();
  }
  if (stopped_ && fatal) {
    EXPECT_EQ(target_adapter_.AwaitSignal(), ZX_EVENTPAIR_PEER_CLOSED);
  }
}

}  // namespace fuzzing
