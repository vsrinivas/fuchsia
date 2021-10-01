// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/runner-unittest.h"

#include "src/sys/fuzzing/common/testing/monitor.h"

namespace fuzzing {

// |Cleanse| tries to replace bytes with 0x20 or 0xff.
static constexpr size_t kNumReplacements = 2;

// Test fixtures.

std::shared_ptr<Options> RunnerTest::DefaultOptions(Runner* runner) {
  auto options = std::make_shared<Options>();
  runner->AddDefaults(options.get());
  return options;
}

void RunnerTest::Configure(Runner* runner, const std::shared_ptr<Options>& options) {
  options_ = options;
  options_->set_seed(1);
  runner->Configure(options_);
}

const Coverage& RunnerTest::GetCoverage(const Input& input) {
  return feedback_[input.ToHex()].coverage;
}

void RunnerTest::SetCoverage(const Input& input, const Coverage& coverage) {
  feedback_[input.ToHex()].coverage = coverage;
}

Result RunnerTest::GetResult(const Input& input) { return feedback_[input.ToHex()].result; }

void RunnerTest::SetResult(const Input& input, Result result) {
  feedback_[input.ToHex()].result = result;
}

bool RunnerTest::HasLeak(const Input& input) {
  auto retval = feedback_[input.ToHex()].leak;
  return retval;
}

void RunnerTest::SetLeak(const Input& input, bool leak) { feedback_[input.ToHex()].leak = leak; }

Input RunnerTest::RunOne() {
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), GetResult(input), HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(const Coverage& coverage) {
  auto input = GetTestInput();
  SetFeedback(coverage, GetResult(input), HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(Result result) {
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), result, HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(bool leak) {
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), GetResult(input), leak);
  return input;
}

void RunnerTest::SetStatus(zx_status_t status) {
  status_ = status;
  sync_completion_signal(&sync_);
}

zx_status_t RunnerTest::GetStatus() {
  sync_completion_wait(&sync_, ZX_TIME_INFINITE);
  return status_;
}

// Unit tests.

void RunnerTest::ExecuteNoError(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x01});
  runner->Execute(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  EXPECT_EQ(RunOne().ToHex(), input.ToHex());
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::ExecuteWithError(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x02});
  runner->Execute(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  RunOne(Result::BAD_MALLOC);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::BAD_MALLOC);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

void RunnerTest::ExecuteWithLeak(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_detect_leaks(true);
  Configure(runner, options);
  Input input({0x03});
  // Simulate a suspected leak, followed by an LSan exit. The leak detection heuristics only run
  // full leak detection when a leak is suspected based on mismatched allocations.
  SetLeak(input, true);
  runner->Execute(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  RunOne();
  RunOne(Result::LEAK);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::LEAK);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

// Simulate no error on the original input.
void RunnerTest::MinimizeNoError(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x04});
  runner->Minimize(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  RunOne();
  EXPECT_EQ(GetStatus(), ZX_ERR_INVALID_ARGS);
}

// Empty input should exit immediately.
void RunnerTest::MinimizeEmpty(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input;
  runner->Minimize(std::move(input), [&](zx_status_t status) { SetStatus(status); });
  RunOne(Result::CRASH);
  EXPECT_EQ(GetStatus(), ZX_OK);
}

// 1-byte input should exit immediately.
void RunnerTest::MinimizeOneByte(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x44});
  runner->Minimize(std::move(input), [&](zx_status_t status) { SetStatus(status); });
  RunOne(Result::CRASH);
  EXPECT_EQ(GetStatus(), ZX_OK);
}

void RunnerTest::MinimizeReduceByTwo(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  constexpr size_t kRuns = 10;
  options->set_runs(kRuns);

  Configure(runner, options);
  Input input({0x51, 0x52, 0x53, 0x54, 0x55, 0x56});
  runner->Minimize(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });

  // Simulate a crash on the original input of 6 bytes...
  auto test_input = RunOne(Result::CRASH);
  EXPECT_EQ(input.ToHex(), test_input.ToHex());

  // ...and on inputs as small as input of 4 bytes, but no smaller.
  size_t runs = 0;
  for (; test_input.size() > 4 && runs < kRuns; ++runs) {
    test_input = RunOne(Result::CRASH);
  }
  auto minimized = test_input.Duplicate();
  EXPECT_LE(minimized.size(), 4U);
  for (runs = 0; runs < kRuns; ++runs) {
    test_input = RunOne(Result::NO_ERRORS);
  }

  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), minimized.ToHex());
}

void RunnerTest::MinimizeNewError(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_run_limit(zx::msec(500).get());
  Configure(runner, options);
  Input input({0x05, 0x15, 0x25, 0x35});
  runner->Minimize(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  // Simulate a crash on the original input...
  auto minimized = RunOne(Result::CRASH);
  // ...and a timeout on a smaller input.
  auto test_input = RunOne(Result::TIMEOUT);
  EXPECT_LT(test_input.size(), input.size());
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), minimized.ToHex());
}

void RunnerTest::CleanseNoReplacement(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x07, 0x17, 0x27});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  // Simulate no error after cleansing any byte.
  for (size_t i = 0; i < input.size(); ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      RunOne(Result::NO_ERRORS);
    }
  }
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

void RunnerTest::CleanseAlreadyClean(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({' ', 0xff});
  runner->Cleanse(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  // All bytes match replacements, so this should be done.
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), input.ToHex());
}

void RunnerTest::CleanseTwoBytes(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));

  Input input0({0x08, 0x18, 0x28});
  SetResult(input0, Result::DEATH);

  Input input1({0x08, 0x18, 0xff});
  SetResult(input1, Result::DEATH);

  Input input2({0x20, 0x18, 0xff});
  SetResult(input2, Result::DEATH);

  runner->Cleanse(input0.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  RunAllForCleanseTwoBytes();
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result_input().ToHex(), "2018ff");
}

void RunnerTest::FuzzUntilError(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_detect_exits(true);
  Configure(runner, options);
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  RunOne();
  RunOne();
  RunOne();
  RunOne(Result::EXIT);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::EXIT);
}

void RunnerTest::FuzzUntilRuns(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  // bool detects_exits = options->has_detect_exits() && options->detect_exits();
  options->set_runs(3);
  Configure(runner, options);
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  for (size_t i = 0; i < 3; ++i) {
    RunOne(Result::NO_ERRORS);
  }
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::FuzzUntilTime(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_max_total_time(zx::msec(100).get());
  Configure(runner, options);

  FakeMonitor monitor;
  runner->AddMonitor(monitor.Bind());
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  RunAllForFuzzUntilTime();
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(size_t(monitor.NextReason()), UpdateReason::INIT);
  EXPECT_EQ(size_t(monitor.NextReason()), UpdateReason::NEW);
  EXPECT_EQ(size_t(monitor.NextReason()), UpdateReason::DONE);
  EXPECT_EQ(runner->result(), Result::NO_ERRORS);
}

void RunnerTest::MergeSeedError(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_oom_limit(1ULL << 25);  // 32 Mb
  Configure(runner, options);
  runner->AddToCorpus(CorpusType::SEED, Input({0x09}));
  runner->Merge([&](zx_status_t status) { SetStatus(status); });
  RunOne(Result::OOM);
  // Derived classes should call and check |GetResult|.
}

void RunnerTest::Merge(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_oom_limit(1ULL << 25);  // 32 Mb
  Configure(runner, options);
  std::vector<std::string> expected_seed;
  std::vector<std::string> expected_live;

  // Empty input, implicitly included in all corpora.
  Input input0;
  expected_seed.push_back(input0.ToHex());
  expected_live.push_back(input0.ToHex());

  // Seed input => kept.
  Input input1({0x0a});
  SetCoverage(input1, {{0, 1}, {1, 2}, {2, 3}});
  runner->AddToCorpus(CorpusType::SEED, input1.Duplicate());
  expected_seed.push_back(input1.ToHex());

  // Triggers error => maybe kept.
  Input input2({0x0b});
  SetResult(input2, Result::OOM);
  runner->AddToCorpus(CorpusType::LIVE, input2.Duplicate());
  if (MergePreservesErrors()) {
    expected_live.push_back(input2.ToHex());
  }

  // Second-smallest and 2 non-seed features => kept.
  Input input5({0x0c, 0x0c});
  SetCoverage(input5, {{0, 2}, {2, 2}});
  runner->AddToCorpus(CorpusType::LIVE, input5.Duplicate());
  expected_live.push_back(input5.ToHex());

  // Larger and 1 feature not in any smaller inputs => kept.
  Input input4({0x0d, 0x0d, 0x0d});
  SetCoverage(input4, {{0, 2}, {1, 1}});
  runner->AddToCorpus(CorpusType::LIVE, input4.Duplicate());
  expected_live.push_back(input4.ToHex());

  // Second-smallest but only 1 non-seed feature above => skipped.
  Input input3({0x0e, 0x0e});
  SetCoverage(input3, {{0, 2}, {2, 3}});
  runner->AddToCorpus(CorpusType::LIVE, input3.Duplicate());

  // Smallest but features are subset of seed corpus => skipped.
  Input input6({0x0f});
  SetCoverage(input6, {{0, 1}, {2, 3}});
  runner->AddToCorpus(CorpusType::LIVE, input6.Duplicate());

  // Largest with all 3 of the new features => skipped.
  Input input7({0x10, 0x10, 0x10, 0x10});
  SetCoverage(input7, {{0, 2}, {1, 1}, {2, 2}});
  runner->AddToCorpus(CorpusType::LIVE, input7.Duplicate());

  runner->Merge([&](zx_status_t status) { SetStatus(status); });
  RunAllForMerge();
  EXPECT_EQ(GetStatus(), ZX_OK);

  std::vector<std::string> actual_seed;
  for (size_t i = 0; i < expected_seed.size(); ++i) {
    actual_seed.push_back(runner->ReadFromCorpus(CorpusType::SEED, i).ToHex());
  }
  std::sort(expected_seed.begin(), expected_seed.end());
  std::sort(actual_seed.begin(), actual_seed.end());
  EXPECT_EQ(expected_seed, actual_seed);

  std::vector<std::string> actual_live;
  for (size_t i = 0; i < expected_live.size(); ++i) {
    actual_live.push_back(runner->ReadFromCorpus(CorpusType::LIVE, i).ToHex());
  }
  std::sort(actual_live.begin(), actual_live.end());
  EXPECT_EQ(expected_live, actual_live);
}

}  // namespace fuzzing
