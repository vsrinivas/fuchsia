// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/runner-unittest.h"

#include "src/sys/fuzzing/common/testing/monitor.h"

namespace fuzzing {

// |Cleanse| tries to replace bytes with 0x20 or 0xff.
static constexpr size_t kNumReplacements = 2;

// Test fixtures.

void RunnerTest::Configure(Runner* runner, const std::shared_ptr<Options>& options) {
  options_ = options;
  options_->set_seed(1);
  runner->Configure(options_);
}

void RunnerTest::SetResult(zx_status_t result) {
  result_ = result;
  sync_completion_signal(&sync_);
}

zx_status_t RunnerTest::GetResult() {
  sync_completion_wait(&sync_, ZX_TIME_INFINITE);
  return result_;
}

// Unit tests.

void RunnerTest::ExecuteNoError(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x01});
  runner->Execute(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  auto test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(input.ToHex(), test_input.ToHex());
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::ExecuteWithError(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x02});
  runner->Execute(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate a large allocation.
  RunOne(Result::BAD_MALLOC);
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::BAD_MALLOC);
}

void RunnerTest::ExecuteWithLeak(Runner* runner) {
  auto options = DefaultOptions();
  options->set_detect_leaks(true);
  Configure(runner, options);
  Input input({0x03});
  runner->Execute(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate a suspected leak, followed by an LSan exit. The leak detection heuristics only run
  // full leak detection when a leak is suspected based on mismatched allocations.
  set_leak_suspected(true);
  RunOne(Result::NO_ERRORS);
  RunOne(Result::LEAK);
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::LEAK);
}

void RunnerTest::MinimizeNoError(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x04});
  runner->Minimize(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate no error on the original input.
  RunOne(Result::NO_ERRORS);
  EXPECT_EQ(GetResult(), ZX_ERR_INVALID_ARGS);
}

void RunnerTest::MinimizeOneByte(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input;
  runner->Minimize(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Empty input should exit immediately.
  RunOne(Result::CRASH);
  EXPECT_EQ(GetResult(), ZX_OK);
}

void RunnerTest::MinimizeReduceByTwo(Runner* runner) {
  auto options = DefaultOptions();
  options->set_runs(4);
  Configure(runner, options);
  Input input({0x51, 0x52, 0x53, 0x54});
  runner->Minimize(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });

  // Simulate a crash on the original input of 4 bytes...
  auto test_input = RunOne(Result::CRASH);
  EXPECT_EQ(input.ToHex(), test_input.ToHex());

  // ...and on a smaller input of 3 bytes...
  size_t max_size = input.size() - 1;
  test_input = RunOne(Result::CRASH);
  EXPECT_LE(test_input.size(), max_size);

  // ...and on an even smaller input of 2 bytes...
  max_size = test_input.size() - 1;
  test_input = RunOne(Result::CRASH);
  EXPECT_LE(test_input.size(), max_size);
  auto minimized = test_input.Duplicate();

  // ...but no smaller than that.
  max_size = test_input.size() - 1;
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_LE(test_input.size(), max_size);

  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), minimized.ToHex());
}

void RunnerTest::MinimizeNewError(Runner* runner) {
  auto options = DefaultOptions();
  options->set_run_limit(zx::msec(500).get());
  Configure(runner, options);
  Input input({0x05, 0x15, 0x25, 0x35});
  runner->Minimize(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate a crash on the original input...
  auto minimized = RunOne(Result::CRASH);
  // ...and a timeout on a smaller input.
  auto test_input = RunOne(Result::TIMEOUT);
  EXPECT_LT(test_input.size(), input.size());
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), minimized.ToHex());
}

void RunnerTest::CleanseNoError(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x06});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate no error on original input.
  auto test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(input.ToHex(), test_input.ToHex());
  EXPECT_EQ(GetResult(), ZX_ERR_INVALID_ARGS);
}

void RunnerTest::CleanseNoReplacement(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x07, 0x17, 0x27});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate death on original input.
  RunOne(Result::DEATH);
  // Simulate no error after cleansing any byte.
  for (size_t i = 0; i < input.size(); ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      RunOne(Result::NO_ERRORS);
    }
  }
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

void RunnerTest::CleanseAlreadyClean(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({' ', 0xff});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate death on original input.
  RunOne(Result::DEATH);
  // All bytes match replacements, so this should be done.
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

void RunnerTest::CleanseTwoBytes(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input({0x08, 0x18, 0x28});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t result) { SetResult(result); });
  // Simulate death on original input.
  RunOne(Result::DEATH);
  // Simulate still triggering the error for the first byte cleansed on the first attempt...
  RunOne(Result::DEATH);  // Use first replacement, 0x20
  for (size_t i = 1; i < input.size(); ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      RunOne(Result::NO_ERRORS);
    }
  }
  // ...and for the last byte cleansed on the second attempt...
  for (size_t i = 1; i < input.size() - 1; ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      RunOne(Result::NO_ERRORS);
    }
  }
  RunOne(Result::NO_ERRORS);
  RunOne(Result::DEATH);  // Use second replacement, 0xff
  // ...and a different error on the third attempt.
  for (size_t i = 1; i < input.size() - 1; ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      RunOne(Result::CRASH);
    }
  }
  EXPECT_EQ(GetResult(), ZX_OK);
  auto last_input = runner->result_input();
  ASSERT_EQ(last_input.size(), input.size());
  auto* original = input.data();
  auto* cleansed = last_input.data();
  EXPECT_EQ(cleansed[0], 0x20U);
  for (size_t i = 1; i < input.size() - 1; ++i) {
    EXPECT_EQ(cleansed[i], original[i]);
  }
  EXPECT_EQ(cleansed[input.size() - 1], 0xFFU);
}

void RunnerTest::FuzzUntilError(Runner* runner) {
  auto options = DefaultOptions();
  options->set_detect_exits(true);
  Configure(runner, options);
  runner->Fuzz([&](zx_status_t result) { SetResult(result); });

  RunOne(Result::NO_ERRORS);
  RunOne(Result::NO_ERRORS);
  RunOne(Result::NO_ERRORS);
  RunOne(Result::EXIT);

  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::EXIT);
}

void RunnerTest::FuzzUntilRuns(Runner* runner) {
  auto options = DefaultOptions();
  options->set_runs(3);
  Configure(runner, options);
  runner->Fuzz([&](zx_status_t result) { SetResult(result); });
  for (size_t i = 0; i < 3; ++i) {
    RunOne(Result::EXIT);
  }
  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::FuzzUntilTime(Runner* runner) {
  auto options = DefaultOptions();
  options->set_max_total_time(zx::msec(200).get());
  Configure(runner, options);

  FakeMonitor monitor;
  runner->AddMonitor(monitor.Bind());
  runner->Fuzz([&](zx_status_t result) { SetResult(result); });
  EXPECT_EQ(size_t(monitor.NextReason()), size_t(UpdateReason::INIT));
  RunOne(Result::NO_ERRORS);
  SetCoverage({{0, 1}});
  RunOne(Result::NO_ERRORS);
  EXPECT_EQ(size_t(monitor.NextReason()), size_t(UpdateReason::NEW));
  RunOne(Result::NO_ERRORS);
  // All of this should be finished within 200 ms. Now, the runner should be stuck trying to start
  // a new run. It won't exit until that run is complete.
  zx::nanosleep(zx::deadline_after(zx::msec(300)));
  RunOne(Result::NO_ERRORS);
  EXPECT_EQ(size_t(monitor.NextReason()), size_t(UpdateReason::DONE));

  EXPECT_EQ(GetResult(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::MergeSeedError(Runner* runner) {
  Configure(runner, DefaultOptions());
  runner->AddToCorpus(CorpusType::SEED, Input({0x09}));
  runner->Merge([&](zx_status_t result) { SetResult(result); });
  RunOne(Result::OOM);
  EXPECT_EQ(GetResult(), ZX_ERR_INVALID_ARGS);
}

void RunnerTest::Merge(Runner* runner) {
  Configure(runner, DefaultOptions());
  Input input0;  // Empty input, implicitly included in all corpora.

  Input input1({0x0a});  // Seed input => kept.
  Coverage coverage1 = {{0, 1}, {1, 2}, {2, 3}};

  Input input2({0x0b});  // Triggers error => kept.

  Input input3({0x0e, 0x0e});  // Second-smallest but only 1 non-seed feature above => skipped.
  Coverage coverage3 = {{0, 2}, {2, 3}};

  Input input4({0x0f, 0x0f, 0x0f});  // Larger and 1 feature not in any smaller inputs => kept.
  Coverage coverage4 = {{0, 2}, {1, 1}};

  Input input5({0x0d, 0x0d});  // Second-smallest and 2 non-seed features => kept.
  Coverage coverage5 = {{0, 2}, {2, 2}};

  Input input6({0x0c});  // Smallest but features are subset of seed corpus => skipped.
  Coverage coverage6 = {{0, 1}, {2, 3}};

  Input input7({0x10, 0x10, 0x10, 0x10});  // Largest with all 3 of the new features => skipped.
  Coverage coverage7 = {{0, 2}, {1, 1}, {2, 2}};

  runner->AddToCorpus(CorpusType::SEED, input1.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input2.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input3.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input4.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input5.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input6.Duplicate());
  runner->AddToCorpus(CorpusType::LIVE, input7.Duplicate());
  runner->Merge([&](zx_status_t result) { SetResult(result); });

  // Seed corpus.
  auto test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input0.ToHex());

  SetCoverage(coverage1);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input1.ToHex());

  // Live corpus, first pass. Should be in same order as added to corpus.
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input0.ToHex());

  test_input = RunOne(Result::OOM);
  EXPECT_EQ(test_input.ToHex(), input2.ToHex());

  SetCoverage(coverage3);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input3.ToHex());

  SetCoverage(coverage4);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input4.ToHex());

  SetCoverage(coverage5);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input5.ToHex());

  SetCoverage(coverage6);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input6.ToHex());

  SetCoverage(coverage7);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input7.ToHex());

  // Live corpus, second pass. Inputs should be ordered by size, smallest to largest, then by most
  // unique features to fewest to break ties.
  // input2 is skipped, as error-triggering inputs are always included.
  // input6 is skipped, as it is redundant with the seed corpus.

  SetCoverage(coverage5);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input5.ToHex());

  SetCoverage(coverage3);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input3.ToHex());

  SetCoverage(coverage4);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input4.ToHex());

  SetCoverage(coverage7);
  test_input = RunOne(Result::NO_ERRORS);
  EXPECT_EQ(test_input.ToHex(), input7.ToHex());

  EXPECT_EQ(GetResult(), ZX_OK);

  // Check the contents of the seed corpus.
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::SEED, 0).ToHex(), input0.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::SEED, 1).ToHex(), input1.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::SEED, 2).ToHex(), input0.ToHex());

  // Check the contents of the live corpus (error-causing inputs should be appended).
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::LIVE, 0).ToHex(), input0.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::LIVE, 1).ToHex(), input5.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::LIVE, 2).ToHex(), input4.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::LIVE, 3).ToHex(), input2.ToHex());
  EXPECT_EQ(runner->ReadFromCorpus(CorpusType::LIVE, 4).ToHex(), input0.ToHex());
}

}  // namespace fuzzing
