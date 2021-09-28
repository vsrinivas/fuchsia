// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner-test.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace fuzzing {

void RunnerImplTest::Configure(Runner* runner, const std::shared_ptr<Options>& options) {
  RunnerTest::Configure(runner, options);
  runner_ = static_cast<RunnerImpl*>(runner);
  runner_->SetTargetAdapterHandler(target_adapter_.GetHandler());
  process_proxy_handler_ = runner_->GetProcessProxyHandler(dispatcher_.get());
}

void RunnerImplTest::SetCoverage(const Coverage& coverage) {
  coverage_ = Coverage(coverage.begin(), coverage.end());
}

Input RunnerImplTest::RunOne(Result expected) {
  EXPECT_EQ(target_adapter_.AwaitSignal(), kStart);
  if (stopped_) {
    process_proxy_handler_(process_.NewRequest());
    process_.Connect();
    process_.AddFeedback();
  }
  auto test_input = target_adapter_.test_input();
  // Fake some activity within the process.
  process_.SetCoverage(coverage_);
  coverage_.clear();
  process_.SetLeak(leak_suspected());
  set_leak_suspected(false);
  // In most cases, the fake process stops, and unless the error is recoverable the target adapter
  // should, too.
  stopped_ = true;
  bool fatal = true;
  switch (expected) {
    case Result::NO_ERRORS:
      // Finish the run normally.
      target_adapter_.SignalPeer(kFinish);
      stopped_ = false;
      break;
    case Result::BAD_MALLOC:
      process_.Exit(options()->malloc_exitcode());
      break;
    case Result::CRASH:
      process_.Crash();
      break;
    case Result::DEATH:
      process_.Exit(options()->death_exitcode());
      break;
    case Result::EXIT:
      process_.Exit(1);
      fatal = options()->detect_exits();
      break;
    case Result::LEAK:
      process_.Exit(options()->leak_exitcode());
      break;
    case Result::OOM:
      process_.Exit(options()->oom_exitcode());
      break;
    case Result::TIMEOUT:
      // Don't signal from the target adapter and don't exit the fake process; just... wait.
      // Eventually, the Runner's Timer thread will time out and kill the process.
      break;
    default:
      FX_NOTREACHED();
  }
  if (stopped_ && fatal) {
    EXPECT_EQ(target_adapter_.AwaitSignal(), ZX_EVENTPAIR_PEER_CLOSED);
  }
  return test_input;
}

}  // namespace fuzzing
