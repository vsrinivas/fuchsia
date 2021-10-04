// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner-test.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace fuzzing {

void RunnerImplTest::SetUp() {
  RunnerTest::SetUp();
  dispatcher_ = std::make_shared<Dispatcher>();
}

void RunnerImplTest::Configure(Runner* runner, const std::shared_ptr<Options>& options) {
  RunnerTest::Configure(runner, options);
  auto* runner_impl = static_cast<RunnerImpl*>(runner);
  runner_impl->SetTargetAdapterHandler(target_adapter_.GetHandler());
  process_proxy_handler_ = runner_impl->GetProcessProxyHandler(dispatcher_);
}

Input RunnerImplTest::GetTestInput() {
  EXPECT_EQ(target_adapter_.AwaitSignal(), kStart);
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

void RunnerImplTest::RunAllForCleanseTwoBytes() {
  EXPECT_EQ(RunOne().ToHex(), "201828");  // 1st attempt.
  EXPECT_EQ(RunOne().ToHex(), "ff1828");
  EXPECT_EQ(RunOne().ToHex(), "082028");
  EXPECT_EQ(RunOne().ToHex(), "08ff28");
  EXPECT_EQ(RunOne().ToHex(), "081820");
  EXPECT_EQ(RunOne().ToHex(), "0818ff");  // Error on 2nd replacement of 3rd byte.
  EXPECT_EQ(RunOne().ToHex(), "2018ff");  // 2nd attempt; error on 1st replacement of 1st byte.
  EXPECT_EQ(RunOne().ToHex(), "2020ff");
  EXPECT_EQ(RunOne().ToHex(), "20ffff");
  EXPECT_EQ(RunOne().ToHex(), "2020ff");  // Third attempt.
  EXPECT_EQ(RunOne().ToHex(), "20ffff");
}

void RunnerImplTest::RunAllForFuzzUntilTime() {
  // Seed corpus is executed first. It has one input.
  RunOne();
  RunOne({{1, 2}});
  zx::nanosleep(zx::deadline_after(zx::msec(200)));
  RunOne();
}

void RunnerImplTest::MergeSeedError(Runner* runner) {
  RunnerTest::MergeSeedError(runner);
  EXPECT_EQ(GetStatus(), ZX_ERR_INVALID_ARGS);
}

void RunnerImplTest::RunAllForMerge() {
  // Seed corpus.
  EXPECT_EQ(RunOne().ToHex(), "");
  EXPECT_EQ(RunOne().ToHex(), "0a");

  // Live corpus, first pass. Should be in same order as added to corpus.
  EXPECT_EQ(RunOne().ToHex(), "");
  EXPECT_EQ(RunOne().ToHex(), "0b");
  EXPECT_EQ(RunOne().ToHex(), "0c0c");
  EXPECT_EQ(RunOne().ToHex(), "0d0d0d");
  EXPECT_EQ(RunOne().ToHex(), "0e0e");
  EXPECT_EQ(RunOne().ToHex(), "0f");
  EXPECT_EQ(RunOne().ToHex(), "10101010");

  // Live corpus, second pass. Inputs should be ordered by size, smallest to largest, then by most
  // unique features to fewest to break ties.
  // input2 is skipped, as error-triggering inputs are always included.
  // input6 is skipped, as it is redundant with the seed corpus.
  EXPECT_EQ(RunOne().ToHex(), "0c0c");
  EXPECT_EQ(RunOne().ToHex(), "0e0e");
  EXPECT_EQ(RunOne().ToHex(), "0d0d0d");
  EXPECT_EQ(RunOne().ToHex(), "10101010");
}

}  // namespace fuzzing
