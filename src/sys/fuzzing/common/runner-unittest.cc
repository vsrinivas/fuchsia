// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/runner-unittest.h"

#include <zircon/status.h>

#include "src/sys/fuzzing/common/testing/monitor.h"

namespace fuzzing {

// |Cleanse| tries to replace bytes with 0x20 or 0xff.
static constexpr size_t kNumReplacements = 2;

// Test fixtures.

void RunnerTest::SetUp() {
  AsyncTest::SetUp();
  options_ = MakeOptions();
  options_->set_seed(1);
  handler_ = [](const Input& input) { return FuzzResult::NO_ERRORS; };
}

void RunnerTest::Configure(const OptionsPtr& options) {
  options_ = options;
  if (!options_->has_seed()) {
    options_->set_seed(1);
  }
  FUZZING_EXPECT_OK(runner()->Configure(options_));
  RunUntilIdle();
}

void RunnerTest::SetCoverage(const Input& input, Coverage coverage) {
  coverage_[input.ToHex()] = std::move(coverage);
}

void RunnerTest::SetFuzzResultHandler(FuzzResultHandler handler) { handler_ = std::move(handler); }

void RunnerTest::SetLeak(bool has_leak) { has_leak_ = has_leak; }

Promise<Input> RunnerTest::RunOne() {
  return RunOne([this](const Input& input) {
    return SetFeedback(Coverage(coverage_[input.ToHex()]), handler_(input), has_leak_);
  });
}

Promise<Input> RunnerTest::RunOne(Coverage coverage) {
  return RunOne([this, coverage = std::move(coverage)](const Input& input) {
    return SetFeedback(std::move(coverage), handler_(input), has_leak_);
  });
}

Promise<Input> RunnerTest::RunOne(FuzzResult fuzz_result) {
  return RunOne([this, fuzz_result](const Input& input) {
    return SetFeedback(Coverage(coverage_[input.ToHex()]), fuzz_result, has_leak_);
  });
}

Promise<Input> RunnerTest::RunOne(bool has_leak) {
  return RunOne([this, has_leak](const Input& input) {
    return SetFeedback(Coverage(coverage_[input.ToHex()]), handler_(input), has_leak);
  });
}

Promise<Input> RunnerTest::RunOne(fit::function<ZxPromise<>(const Input&)> set_feedback) {
  Bridge<> bridge;
  auto task = GetTestInput()
                  .and_then([set_feedback = std::move(set_feedback)](Input& input) mutable {
                    return set_feedback(input).and_then([input = std::move(input)]() mutable {
                      return fpromise::ok(std::move(input));
                    });
                  })
                  .or_else([](const zx_status_t& status) {
                    // Target may close before returning test input.
                    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED) << zx_status_get_string(status);
                    return fpromise::error();
                  })
                  .then([completer = std::move(bridge.completer)](Result<Input>& result) mutable {
                    if (result.is_ok()) {
                      completer.complete_ok();
                    } else {
                      completer.complete_error();
                    }
                    return std::move(result);
                  });
  auto consumer = std::move(previous_run_);
  previous_run_ = std::move(bridge.consumer);
  return consumer ? consumer.promise().and_then(std::move(task)).box() : task.box();
}

void RunnerTest::RunUntil(Promise<> promise) {
  RunUntil(
      std::move(promise),
      [this](const Result<Input>& result) -> Promise<Input> { return RunOne(); }, Input());
}

void RunnerTest::RunUntil(Promise<> promise, RunCallback run, Input input) {
  auto task =
      fpromise::make_promise([run = std::move(run),
                              result = Result<Input>(fpromise::ok(std::move(input))),
                              running = Future<Input>(), until = Future<>(std::move(promise))](
                                 Context& context) mutable -> Result<> {
        while (result.is_ok() && !until(context)) {
          if (!running) {
            running = run(result);
          }
          if (!running(context)) {
            return fpromise::pending();
          }
          result = running.take_result();
        }
        // Ignore errors; they were checked in |RunOne|.
        return fpromise::ok();
      }).wrap_with(scope_);
  FUZZING_EXPECT_OK(std::move(task));
  RunUntilIdle();
}

// Unit tests.

void RunnerTest::ExecuteNoError() {
  Configure(MakeOptions());
  Input input({0x01});
  FUZZING_EXPECT_OK(runner()->Execute(input.Duplicate()), FuzzResult::NO_ERRORS);
  FUZZING_EXPECT_OK(RunOne(), std::move(input));
  RunUntilIdle();
}

void RunnerTest::ExecuteWithError() {
  Configure(MakeOptions());
  Input input({0x02});
  FUZZING_EXPECT_OK(runner()->Execute(input.Duplicate()), FuzzResult::BAD_MALLOC);
  FUZZING_EXPECT_OK(RunOne(FuzzResult::BAD_MALLOC), std::move(input));
  RunUntilIdle();
}

void RunnerTest::ExecuteWithLeak() {
  auto options = MakeOptions();
  options->set_detect_leaks(true);
  Configure(options);
  Input input({0x03});
  // Simulate a suspected leak, followed by an LSan exit. The leak detection heuristics only run
  // full leak detection when a leak is suspected based on mismatched allocations.
  SetLeak(true);
  FUZZING_EXPECT_OK(runner()->Execute(input.Duplicate()), FuzzResult::LEAK);
  FUZZING_EXPECT_OK(RunOne(), input.Duplicate());
  FUZZING_EXPECT_OK(RunOne(FuzzResult::LEAK), std::move(input));
  RunUntilIdle();
}

// Simulate no error on the original input.
void RunnerTest::MinimizeNoError() {
  Configure(MakeOptions());
  Input input({0x04});
  FUZZING_EXPECT_ERROR(runner()->Minimize(input.Duplicate()), ZX_ERR_INVALID_ARGS);
  FUZZING_EXPECT_OK(RunOne(), std::move(input));
  RunUntilIdle();
}

// Empty input should exit immediately.
void RunnerTest::MinimizeEmpty() {
  Configure(MakeOptions());
  Input input;
  FUZZING_EXPECT_OK(runner()->Minimize(input.Duplicate()), input.Duplicate());
  FUZZING_EXPECT_OK(RunOne(FuzzResult::CRASH), std::move(input));
  RunUntilIdle();
}

// 1-byte input should exit immediately.
void RunnerTest::MinimizeOneByte() {
  Configure(MakeOptions());
  Input input({0x44});
  FUZZING_EXPECT_OK(runner()->Minimize(input.Duplicate()), input.Duplicate());
  FUZZING_EXPECT_OK(RunOne(FuzzResult::CRASH), std::move(input));
  RunUntilIdle();
}

void RunnerTest::MinimizeReduceByTwo() {
  auto options = MakeOptions();
  constexpr size_t kRuns = 0x40;
  options->set_runs(kRuns);
  Configure(options);
  Input input({0x51, 0x52, 0x53, 0x54, 0x55, 0x56});
  Input minimized;
  Barrier barrier;
  auto task = runner()
                  ->Minimize(input.Duplicate())
                  .and_then([&minimized](Input& result) {
                    minimized = std::move(result);
                    return fpromise::ok();
                  })
                  .wrap_with(barrier);
  FUZZING_EXPECT_OK(std::move(task));

  // Crash until inputs are smaller than 4 bytes.
  RunUntil(
      barrier.sync(),
      [this](const Result<Input>& result) {
        return RunOne(result.value().size() > 3 ? FuzzResult::CRASH : FuzzResult::NO_ERRORS);
      },
      std::move(input));

  EXPECT_LE(minimized.size(), 3U);
}

void RunnerTest::MinimizeNewError() {
  auto options = MakeOptions();
  options->set_run_limit(zx::msec(500).get());
  Configure(options);
  Input input({0x05, 0x15, 0x25, 0x35});
  Input minimized;

  // Simulate a crash on the original input, and a timeout on any smaller input.
  SetFuzzResultHandler([](const Input& input) {
    return input.size() > 3 ? FuzzResult::CRASH : FuzzResult::TIMEOUT;
  });
  Barrier barrier;
  auto task = runner()
                  ->Minimize(input.Duplicate())
                  .and_then([&minimized](Input& result) {
                    minimized = std::move(result);
                    return fpromise::ok();
                  })
                  .wrap_with(barrier);
  FUZZING_EXPECT_OK(std::move(task));
  RunUntil(barrier.sync());
  EXPECT_EQ(minimized, input);
}

void RunnerTest::CleanseNoReplacement() {
  Configure(MakeOptions());
  Input input({0x07, 0x17, 0x27});
  Input cleansed;
  FUZZING_EXPECT_OK(runner()->Cleanse(input.Duplicate()), &cleansed);

  // Simulate no error after cleansing any byte.
  for (size_t i = 0; i < input.size(); ++i) {
    for (size_t j = 0; j < kNumReplacements; ++j) {
      FUZZING_EXPECT_OK(RunOne(FuzzResult::NO_ERRORS));
    }
  }

  RunUntilIdle();
  EXPECT_EQ(cleansed, input);
}

void RunnerTest::CleanseAlreadyClean() {
  Configure(MakeOptions());
  Input input({' ', 0xff});
  Input cleansed;
  FUZZING_EXPECT_OK(runner()->Cleanse(input.Duplicate()), &cleansed);

  // All bytes match replacements, so this should be done.
  RunUntilIdle();
  EXPECT_EQ(cleansed, input);
}

void RunnerTest::CleanseTwoBytes() {
  Configure(MakeOptions());

  Input input({0x08, 0x18, 0x28});
  Input inputs[] = {
      {0x20, 0x18, 0x28},  // 1st try.
      {0xff, 0x18, 0x28},  //
      {0x08, 0x20, 0x28},  //
      {0x08, 0xff, 0x28},  //
      {0x08, 0x18, 0x20},  //
      {0x08, 0x18, 0xff},  // Error on 2nd replacement of 3rd byte.
      {0x20, 0x18, 0xff},  // 2nd try. Error on 1st replacement of 1st byte.
      {0x20, 0x20, 0xff},  //
      {0x20, 0xff, 0xff},  //
      {0x20, 0x20, 0xff},  // 3rd try. No errors.
      {0x20, 0xff, 0xff},  //
  };
  SetFuzzResultHandler([](const Input& input) {
    auto hex = input.ToHex();
    return (hex == "081828" || hex == "0818ff" || hex == "2018ff") ? FuzzResult::DEATH
                                                                   : FuzzResult::NO_ERRORS;
  });

  Input cleansed;
  FUZZING_EXPECT_OK(runner()->Cleanse(std::move(input)), &cleansed);
  for (auto& input : inputs) {
    FUZZING_EXPECT_OK(RunOne(), std::move(input));
  }

  RunUntilIdle();
  EXPECT_EQ(cleansed, Input({0x20, 0x18, 0xff}));
}

void RunnerTest::FuzzUntilError() {
  auto runner = this->runner();
  auto options = MakeOptions();
  options->set_detect_exits(true);
  options->set_mutation_depth(1);
  Configure(options);

  Artifact artifact;
  FUZZING_EXPECT_OK(runner->Fuzz(), &artifact);

  // Add some corpus elements.
  std::vector<Input> inputs;
  inputs.emplace_back("foo");
  inputs.emplace_back("bar");
  inputs.emplace_back("baz");
  inputs.emplace_back("qux");
  auto last = CorpusType::LIVE;
  auto next = CorpusType::SEED;
  for (const auto& input : inputs) {
    EXPECT_EQ(runner->AddToCorpus(next, input.Duplicate()), ZX_OK);
    std::swap(next, last);
  }

  // Set some coverage for the inputs above. Some runners (e.g. libFuzzer) won't mutate an input
  // that lacks any coverage. According to the AFL bucketing scheme used by libFuzzer and others,
  // the counter must be at least 2 to map to a coverage "feature".
  for (size_t i = 0; i < inputs.size(); ++i) {
    SetCoverage(inputs[i], {{i + 1, i + 1}});
  }

  std::vector<Input> actual;
  for (size_t i = 0; i < 100; ++i) {
    FUZZING_EXPECT_OK(RunOne().then([&](Result<Input>& result) {
      if (result.is_ok()) {
        actual.push_back(result.take_value());
      }
    }));
  }

  FUZZING_EXPECT_OK(RunOne(FuzzResult::EXIT));
  RunUntilIdle();

  // Helper lambda to check if the sequence of bytes given by |needle| appears in order, but not
  // necessarily contiguously, in the sequence of bytes given by |haystack|.
  auto contains = [](const Input& haystack, const Input& needle) -> bool {
    const auto* needle_data = needle.data();
    const auto* haystack_data = haystack.data();
    size_t i = 0;
    for (size_t j = 0; i < needle.size() && j < haystack.size(); ++j) {
      if (needle_data[i] == haystack_data[j]) {
        ++i;
      }
    }
    return i == needle.size();
  };

  // Verify that each corpus element is 1) used as-is, and 2) used as the basis for mutations.
  for (auto& orig : inputs) {
    bool exact_match_found = false;
    bool near_match_found = false;
    for (auto& input : actual) {
      if (orig == input) {
        exact_match_found = true;
      } else if (contains(input, orig)) {
        near_match_found = true;
      }
      if (exact_match_found && near_match_found) {
        break;
      }
    }
    EXPECT_TRUE(exact_match_found) << "input: " << orig.ToHex();
    EXPECT_TRUE(near_match_found) << "input: " << orig.ToHex();
  }

  EXPECT_EQ(artifact.fuzz_result(), FuzzResult::EXIT);
}

void RunnerTest::FuzzUntilRuns() {
  auto runner = this->runner();
  auto options = MakeOptions();
  const size_t kNumRuns = 10;
  options->set_runs(kNumRuns);
  Configure(options);
  std::vector<std::string> expected({""});

  // Subscribe to status updates.
  FakeMonitor monitor(executor());
  runner->AddMonitor(monitor.NewBinding());

  // Fuzz for exactly |kNumRuns|.
  Artifact artifact;
  FUZZING_EXPECT_OK(runner->Fuzz(), &artifact);

  for (size_t i = 0; i < kNumRuns; ++i) {
    FUZZING_EXPECT_OK(RunOne({{i, i}}));
  }

  // Check that we get the expected status updates.
  FUZZING_EXPECT_OK(monitor.AwaitUpdate());
  RunUntilIdle();
  EXPECT_EQ(monitor.reason(), UpdateReason::INIT);
  auto status = monitor.take_status();
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

  monitor.pop_front();
  FUZZING_EXPECT_OK(monitor.AwaitUpdate());
  RunUntilIdle();
  EXPECT_EQ(size_t(monitor.reason()), size_t(UpdateReason::NEW));
  status = monitor.take_status();
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
  while (monitor.reason() != UpdateReason::DONE) {
    monitor.pop_front();
    FUZZING_EXPECT_OK(monitor.AwaitUpdate());
    RunUntilIdle();
  }
  status = monitor.take_status();
  ASSERT_TRUE(status.has_running());
  EXPECT_FALSE(status.running());
  ASSERT_TRUE(status.has_runs());
  EXPECT_GE(status.runs(), runs);
  ASSERT_TRUE(status.has_elapsed());
  EXPECT_GT(status.elapsed(), elapsed);
  ASSERT_TRUE(status.has_covered_pcs());
  EXPECT_GE(status.covered_pcs(), covered_pcs);

  // All done.
  EXPECT_EQ(artifact.fuzz_result(), FuzzResult::NO_ERRORS);
}

void RunnerTest::FuzzUntilTime() {
  auto runner = this->runner();
  // Time is always tricky to test. As a result, this test verifies the bare minimum, namely that
  // the runner exits at least 100 ms after it started. All other verification is performed in more
  // controllable tests, such as |FuzzUntilRuns| above.
  auto options = MakeOptions();
  options->set_max_total_time(zx::msec(100).get());
  Configure(options);
  auto start = zx::clock::get_monotonic();

  Artifact artifact;
  Barrier barrier;
  auto task = runner->Fuzz()
                  .and_then([&artifact](Artifact& result) {
                    artifact = std::move(result);
                    return fpromise::ok();
                  })
                  .wrap_with(barrier);
  FUZZING_EXPECT_OK(std::move(task));
  RunUntil(barrier.sync());

  EXPECT_EQ(artifact.fuzz_result(), FuzzResult::NO_ERRORS);
  auto elapsed = zx::clock::get_monotonic() - start;
  EXPECT_GE(elapsed, zx::msec(100));
}

void RunnerTest::MergeSeedError(zx_status_t expected, uint64_t oom_limit) {
  auto runner = this->runner();
  auto options = MakeOptions();
  options->set_oom_limit(oom_limit);
  Configure(options);
  runner->AddToCorpus(CorpusType::SEED, Input({0x09}));
  if (expected == ZX_OK) {
    FUZZING_EXPECT_OK(runner->Merge());
  } else {
    FUZZING_EXPECT_ERROR(runner->Merge(), expected);
  }
  FUZZING_EXPECT_OK(RunOne(FuzzResult::OOM));
  RunUntilIdle();
}

void RunnerTest::Merge(bool keeps_errors, uint64_t oom_limit) {
  auto runner = this->runner();
  auto options = MakeOptions();
  options->set_oom_limit(oom_limit);
  Configure(options);
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
  SetFuzzResultHandler([](const Input& input) {
    return input.ToHex() == "0b" ? FuzzResult::OOM : FuzzResult::NO_ERRORS;
  });
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

  Barrier barrier;
  FUZZING_EXPECT_OK(runner->Merge().wrap_with(barrier));
  RunUntil(barrier.sync());

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

void RunnerTest::Stop() {
  auto runner = this->runner();
  Configure(MakeOptions());
  Artifact artifact;
  Barrier barrier;
  auto task = runner->Fuzz()
                  .and_then([&artifact](Artifact& result) {
                    artifact = std::move(result);
                    return fpromise::ok();
                  })
                  .wrap_with(barrier);
  FUZZING_EXPECT_OK(std::move(task));
  FUZZING_EXPECT_OK(executor()
                        ->MakeDelayedPromise(zx::msec(100))
                        .then([runner](const Result<>& result) { return runner->Stop(); })
                        .then([runner](const ZxResult<>& result) {
                          // Should be idempotent.
                          return runner->Stop();
                        }));
  RunUntil(barrier.sync());
  EXPECT_EQ(artifact.fuzz_result(), FuzzResult::NO_ERRORS);
}

}  // namespace fuzzing
