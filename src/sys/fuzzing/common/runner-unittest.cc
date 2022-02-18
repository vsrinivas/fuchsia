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

FuzzResult RunnerTest::GetResult(const Input& input) { return feedback_[input.ToHex()].result; }

void RunnerTest::SetResult(const Input& input, FuzzResult result) {
  feedback_[input.ToHex()].result = result;
}

bool RunnerTest::HasLeak(const Input& input) {
  auto retval = feedback_[input.ToHex()].leak;
  return retval;
}

void RunnerTest::SetLeak(const Input& input, bool leak) { feedback_[input.ToHex()].leak = leak; }

Input RunnerTest::RunOne() {
  EXPECT_TRUE(HasTestInput());
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), GetResult(input), HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(const Coverage& coverage) {
  EXPECT_TRUE(HasTestInput());
  auto input = GetTestInput();
  SetFeedback(coverage, GetResult(input), HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(FuzzResult result) {
  EXPECT_TRUE(HasTestInput());
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), result, HasLeak(input));
  return input;
}

Input RunnerTest::RunOne(bool has_leak) {
  EXPECT_TRUE(HasTestInput());
  auto input = GetTestInput();
  SetFeedback(GetCoverage(input), GetResult(input), has_leak);
  return input;
}

void RunnerTest::RunUntilIdle() {
  while (HasTestInput()) {
    auto input = GetTestInput();
    SetFeedback(GetCoverage(input), GetResult(input), HasLeak(input));
  }
}

bool RunnerTest::HasTestInput() {
  // Periodically check if the engine has produced a final status; and therefore will not produce
  // additional test inputs.
  Waiter waiter = [this](zx::time deadline) {
    return (HasTestInput(deadline) || HasStatus()) ? ZX_OK : ZX_ERR_TIMED_OUT;
  };
  auto status = PollFor("engine to produce test input or status", &waiter, zx::msec(100));
  EXPECT_EQ(status, ZX_OK);
  started_sync_.Signal();
  return !HasStatus();
}

void RunnerTest::AwaitStarted() { started_sync_.WaitFor("runner to send test input"); }

bool RunnerTest::HasStatus() const { return status_sync_.is_signaled(); }

zx_status_t RunnerTest::GetStatus() {
  status_sync_.WaitFor("runner to complete");
  return status_;
}

void RunnerTest::SetStatus(zx_status_t status) {
  status_ = status;
  status_sync_.Signal();
}

// Unit tests.

void RunnerTest::ExecuteNoError(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x01});
  runner->Execute(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  EXPECT_EQ(RunOne().ToHex(), input.ToHex());
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::NO_ERRORS);
}

void RunnerTest::ExecuteWithError(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x02});
  runner->Execute(input.Duplicate(), [&](zx_status_t status) { SetStatus(status); });
  RunOne(FuzzResult::BAD_MALLOC);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::BAD_MALLOC);
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
  RunOne(FuzzResult::LEAK);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::LEAK);
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
  RunOne(FuzzResult::CRASH);
  EXPECT_EQ(GetStatus(), ZX_OK);
}

// 1-byte input should exit immediately.
void RunnerTest::MinimizeOneByte(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  Input input({0x44});
  runner->Minimize(std::move(input), [&](zx_status_t status) { SetStatus(status); });
  RunOne(FuzzResult::CRASH);
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
  auto test_input = RunOne(FuzzResult::CRASH);
  EXPECT_EQ(input.ToHex(), test_input.ToHex());

  // ...and on inputs as small as input of 4 bytes, but no smaller.
  size_t runs = 0;
  for (; test_input.size() > 4 && runs < kRuns; ++runs) {
    test_input = RunOne(FuzzResult::CRASH);
  }
  auto minimized = test_input.Duplicate();
  EXPECT_LE(minimized.size(), 4U);
  for (runs = 0; runs < kRuns; ++runs) {
    test_input = RunOne(FuzzResult::NO_ERRORS);
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
  auto minimized = RunOne(FuzzResult::CRASH);
  // ...and a timeout on a smaller input.
  auto test_input = RunOne(FuzzResult::TIMEOUT);
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
      RunOne(FuzzResult::NO_ERRORS);
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
  SetResult(input0, FuzzResult::DEATH);

  Input input1({0x08, 0x18, 0xff});
  SetResult(input1, FuzzResult::DEATH);

  Input input2({0x20, 0x18, 0xff});
  SetResult(input2, FuzzResult::DEATH);

  runner->Cleanse(input0.Duplicate(), [&](zx_status_t status) { SetStatus(status); });

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
  RunOne(FuzzResult::EXIT);
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::EXIT);
}

void RunnerTest::FuzzUntilRuns(Runner* runner) {
  auto options = RunnerTest::DefaultOptions(runner);
  const size_t kNumRuns = 10;
  options->set_runs(kNumRuns);
  Configure(runner, options);
  std::vector<std::string> expected({""});

  // Add some seed corpus elements.
  Input input1({0x01, 0x11});
  EXPECT_EQ(runner->AddToCorpus(CorpusType::SEED, input1.Duplicate()), ZX_OK);
  expected.push_back(input1.ToHex());

  Input input2({0x02, 0x22});
  EXPECT_EQ(runner->AddToCorpus(CorpusType::SEED, input2.Duplicate()), ZX_OK);
  expected.push_back(input2.ToHex());

  Input input3({0x03, 0x33});
  EXPECT_EQ(runner->AddToCorpus(CorpusType::LIVE, input3.Duplicate()), ZX_OK);
  expected.push_back(input3.ToHex());

  // Subscribe to status updates.
  FakeMonitor monitor;
  auto dispatcher = std::make_shared<Dispatcher>();
  runner->AddMonitor(monitor.Bind(dispatcher));

  // Fuzz for exactly |kNumRuns|.
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  std::vector<std::string> actual;
  for (size_t i = 0; i < kNumRuns; ++i) {
    actual.push_back(RunOne({{i, i}}).ToHex());
  }

  // Check that we get the expected status updates.
  UpdateReason reason;
  auto status = monitor.NextStatus(&reason);
  EXPECT_EQ(size_t(reason), UpdateReason::INIT);
  ASSERT_TRUE(status.has_running());
  EXPECT_TRUE(status.running());
  ASSERT_TRUE(status.has_runs());
  auto runs = status.runs();
  ASSERT_TRUE(status.has_elapsed());
  EXPECT_GT(status.elapsed(), 0U);
  auto elapsed = status.elapsed();
  ASSERT_TRUE(status.has_covered_pcs());
  EXPECT_GE(status.covered_pcs(), 0U);
  auto covered_pcs = status.covered_pcs();

  status = monitor.NextStatus(&reason);
  EXPECT_EQ(size_t(reason), UpdateReason::NEW);
  ASSERT_TRUE(status.has_running());
  EXPECT_TRUE(status.running());
  ASSERT_TRUE(status.has_runs());
  EXPECT_GT(status.runs(), runs);
  runs = status.runs();
  ASSERT_TRUE(status.has_elapsed());
  EXPECT_GT(status.elapsed(), elapsed);
  elapsed = status.elapsed();
  ASSERT_TRUE(status.has_covered_pcs());
  EXPECT_GT(status.covered_pcs(), covered_pcs);
  covered_pcs = status.covered_pcs();

  // Skip others up to DONE.
  while (reason != UpdateReason::DONE) {
    status = monitor.NextStatus(&reason);
  }
  EXPECT_EQ(size_t(reason), UpdateReason::DONE);
  ASSERT_TRUE(status.has_running());
  EXPECT_FALSE(status.running());
  ASSERT_TRUE(status.has_runs());
  EXPECT_GE(status.runs(), runs);
  ASSERT_TRUE(status.has_elapsed());
  EXPECT_GT(status.elapsed(), elapsed);
  ASSERT_TRUE(status.has_covered_pcs());
  EXPECT_GE(status.covered_pcs(), covered_pcs);

  // All done.
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::NO_ERRORS);

  // All corpus inputs should have been run.
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  std::vector<std::string> missing;
  std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
                      std::inserter(missing, missing.begin()));
  EXPECT_EQ(missing, std::vector<std::string>());
}

void RunnerTest::FuzzUntilTime(Runner* runner) {
  // Time is always tricky to test. As a result, this test verifies the bare minimum, namely that
  // the runner exits at least 100 ms after it started. All other verification is performed in more
  // controllable tests, such as |FuzzUntilRuns| above.
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_max_total_time(zx::msec(100).get());
  Configure(runner, options);

  auto start = zx::clock::get_monotonic();
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  RunUntilIdle();
  auto elapsed = zx::clock::get_monotonic() - start;

  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::NO_ERRORS);
  EXPECT_GE(elapsed, zx::msec(100));
}

void RunnerTest::MergeSeedError(Runner* runner, zx_status_t expected, uint64_t oom_limit) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_oom_limit(oom_limit);
  Configure(runner, options);
  runner->AddToCorpus(CorpusType::SEED, Input({0x09}));
  runner->Merge([&](zx_status_t status) { SetStatus(status); });
  RunOne(FuzzResult::OOM);
  EXPECT_EQ(GetStatus(), expected);
}

void RunnerTest::Merge(Runner* runner, bool keeps_errors, uint64_t oom_limit) {
  auto options = RunnerTest::DefaultOptions(runner);
  options->set_oom_limit(oom_limit);
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
  SetResult(input2, FuzzResult::OOM);
  runner->AddToCorpus(CorpusType::LIVE, input2.Duplicate());
  if (keeps_errors) {
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
  RunUntilIdle();
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

void RunnerTest::Stop(Runner* runner) {
  Configure(runner, RunnerTest::DefaultOptions(runner));
  runner->Fuzz([&](zx_status_t status) { SetStatus(status); });
  std::thread t([this]() { RunUntilIdle(); });
  AwaitStarted();
  // Each stage of stopping should be idempotent.
  runner->Close();
  runner->Close();
  runner->Interrupt();
  runner->Interrupt();
  runner->Join();
  runner->Join();
  EXPECT_EQ(GetStatus(), ZX_OK);
  EXPECT_EQ(runner->result(), FuzzResult::NO_ERRORS);
  t.join();
}

}  // namespace fuzzing
