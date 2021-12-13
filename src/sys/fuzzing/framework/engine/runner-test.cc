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
  process_proxy_handler_ = runner_impl->GetProcessProxyHandler();

  auto target_adapter_client = std::make_unique<TargetAdapterClient>(target_adapter_.GetHandler());
  runner_impl->SetTargetAdapter(std::move(target_adapter_client));
}

bool RunnerImplTest::HasTestInput(const zx::duration& timeout) {
  zx_signals_t observed;
  if (target_adapter_.AwaitSignal(timeout, &observed) != ZX_OK) {
    return false;
  }
  EXPECT_EQ(observed, kStart);
  return true;
}

Input RunnerImplTest::GetTestInput() {
  if (stopped_) {
    process_proxy_handler_(process_.NewRequest());
    process_.Connect();
    process_.AddFeedback();
  }
  return target_adapter_.test_input();
}

void RunnerImplTest::SetFeedback(const Coverage& coverage, Result result, bool leak) {
  // Fake some activity within the process.
  process_.SetCoverage(coverage);
  process_.SetLeak(leak);
  // In most cases, the fake process stops, and unless the error is recoverable the target adapter
  // should, too.
  stopped_ = true;
  bool fatal = true;
  switch (result) {
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
}

}  // namespace fuzzing
