// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>
#include <perftest/runner.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>

#include <fbl/algorithm.h>

#include <utility>

// This is a helper for creating a FILE* that we can redirect output to, in
// order to make the tests below less noisy.  We don't look at the output
// that is sent to the stream.
class DummyOutputStream {
 public:
  DummyOutputStream() {
    fp_ = fmemopen(buf_, sizeof(buf_), "w+");
    ZX_ASSERT(fp_);
  }
  ~DummyOutputStream() { ZX_ASSERT(fclose(fp_) == 0); }

  FILE* fp() { return fp_; }

 private:
  FILE* fp_;
  // Non-zero-size dummy buffer that fmemopen() will accept.
  char buf_[1];
};

// Example of a valid test that passes.
static bool NoOpTest(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
  }
  return true;
}

// Example of a test that fails by returning false.
static bool FailingTest(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
  }
  return false;
}

// Sanity-check time values.
static bool check_times(perftest::TestCaseResults* test_case) {
  for (auto time_taken : test_case->values) {
    EXPECT_GE(time_taken, 0);
    // Check for unreasonably large values, which suggest that we
    // subtracted timestamps incorrectly.
    EXPECT_LT(time_taken, static_cast<double>(1ULL << 60));
  }
  return true;
}

// Test that a successful run of a perf test produces sensible results.
static bool TestResults() {
  BEGIN_TEST;

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"no_op_example_test", NoOpTest};
  test_list.push_back(std::move(test));

  const uint32_t kRunCount = 7;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_TRUE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));

  auto* test_cases = results.results();
  ASSERT_EQ(test_cases->size(), 1);
  // The output should have time values for the number of runs we requested.
  auto* test_case = &(*test_cases)[0];
  EXPECT_EQ(test_case->values.size(), kRunCount);
  EXPECT_STR_EQ(test_case->label.c_str(), "no_op_example_test");
  EXPECT_TRUE(check_times(test_case));
  EXPECT_EQ(test_case->bytes_processed_per_run, 0);

  END_TEST;
}

// Test that if a perf test fails by returning "false", the failure gets
// propagated correctly.
static bool TestFailingTest() {
  BEGIN_TEST;

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"example_test", FailingTest};
  test_list.push_back(std::move(test));

  const uint32_t kRunCount = 7;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_FALSE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));
  EXPECT_EQ(results.results()->size(), 0);

  END_TEST;
}

// Test that we report a test as failed if it calls KeepRunning() too many
// or too few times.  Make sure that we don't overrun the array of
// timestamps or report uninitialized data from that array.
static bool TestBadKeepRunningCalls() {
  BEGIN_TEST;

  for (int actual_runs = 0; actual_runs < 10; ++actual_runs) {
    // Example test function which might call KeepRunning() the wrong
    // number of times.
    auto test_func = [=](perftest::RepeatState* state) {
      for (int i = 0; i < actual_runs + 1; ++i)
        state->KeepRunning();
      return true;
    };

    perftest::internal::TestList test_list;
    perftest::internal::NamedTest test{"example_bad_test", test_func};
    test_list.push_back(std::move(test));

    const uint32_t kRunCount = 5;
    perftest::ResultsSet results;
    DummyOutputStream out;
    bool success =
        perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results);
    EXPECT_EQ(success, kRunCount == actual_runs);
    EXPECT_EQ(results.results()->size(), (size_t)(kRunCount == actual_runs ? 1 : 0));
  }

  END_TEST;
}

static bool MultistepTest(perftest::RepeatState* state) {
  state->DeclareStep("step1");
  state->DeclareStep("step2");
  state->DeclareStep("step3");
  while (state->KeepRunning()) {
    // Step 1 would go here.
    state->NextStep();
    // Step 2 would go here.
    state->NextStep();
    // Step 3 would go here.
  }
  return true;
}

// Test the results for a simple multi-step test.
static bool TestMultistepTest() {
  BEGIN_TEST;

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"example_test", MultistepTest};
  test_list.push_back(std::move(test));

  const uint32_t kRunCount = 7;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_TRUE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));
  ASSERT_EQ(results.results()->size(), 3);
  EXPECT_STR_EQ((*results.results())[0].label.c_str(), "example_test.step1");
  EXPECT_STR_EQ((*results.results())[1].label.c_str(), "example_test.step2");
  EXPECT_STR_EQ((*results.results())[2].label.c_str(), "example_test.step3");
  for (auto& test_case : *results.results()) {
    EXPECT_EQ(test_case.values.size(), kRunCount);
    EXPECT_TRUE(check_times(&test_case));
  }

  END_TEST;
}

static bool MultistepTestWithDuplicateNames(perftest::RepeatState* state) {
  // These duplicate names should be caught as an error.
  state->DeclareStep("step1");
  state->DeclareStep("step1");
  while (state->KeepRunning()) {
    state->NextStep();
  }
  return true;
}

// Test that we report a test as failed if it declares duplicate step names
// via DeclareStep().
static bool TestDuplicateStepNamesAreRejected() {
  BEGIN_TEST;

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"example_test", MultistepTestWithDuplicateNames};
  test_list.push_back(std::move(test));
  const uint32_t kRunCount = 7;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_FALSE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));

  END_TEST;
}

// Test that we report a test as failed if it calls NextStep() before
// KeepRunning(), which is invalid.
static bool TestNextStepCalledBeforeKeepRunning() {
  BEGIN_TEST;

  bool keeprunning_retval = true;
  // Invalid test function that calls NextStep() at the wrong time,
  // before calling KeepRunning().
  auto test_func = [&](perftest::RepeatState* state) {
    state->NextStep();
    keeprunning_retval = state->KeepRunning();
    return true;
  };

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"example_bad_test", test_func};
  test_list.push_back(std::move(test));
  const uint32_t kRunCount = 5;
  perftest::ResultsSet results;
  DummyOutputStream out;
  bool success =
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results);
  EXPECT_FALSE(success);
  EXPECT_FALSE(keeprunning_retval);

  END_TEST;
}

// Test that we report a test as failed if it calls NextStep() too many or
// too few times.
static bool TestBadNextStepCalls() {
  BEGIN_TEST;

  for (int actual_calls = 0; actual_calls < 10; ++actual_calls) {
    // Example test function which might call NextStep() the wrong
    // number of times.
    auto test_func = [=](perftest::RepeatState* state) {
      state->DeclareStep("step1");
      state->DeclareStep("step2");
      state->DeclareStep("step3");
      while (state->KeepRunning()) {
        for (int i = 0; i < actual_calls; ++i) {
          state->NextStep();
        }
      }
      return true;
    };

    perftest::internal::TestList test_list;
    perftest::internal::NamedTest test{"example_bad_test", test_func};
    test_list.push_back(std::move(test));

    const uint32_t kRunCount = 5;
    perftest::ResultsSet results;
    DummyOutputStream out;
    bool success =
        perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results);
    const int kCorrectNumberOfCalls = 2;
    EXPECT_EQ(success, actual_calls == kCorrectNumberOfCalls);
    EXPECT_EQ(results.results()->size(),
              static_cast<size_t>(actual_calls == kCorrectNumberOfCalls ? 3 : 0));
  }

  END_TEST;
}

// Check that the bytes_processed_per_run parameter is propagated through.
static bool TestBytesProcessedParameter() {
  BEGIN_TEST;

  auto test_func = [&](perftest::RepeatState* state) {
    state->SetBytesProcessedPerRun(1234);
    while (state->KeepRunning()) {
    }
    return true;
  };
  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"throughput_test", test_func};
  test_list.push_back(std::move(test));

  const uint32_t kRunCount = 5;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_TRUE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));
  auto* test_cases = results.results();
  ASSERT_EQ(test_cases->size(), 1);
  EXPECT_EQ((*test_cases)[0].bytes_processed_per_run, 1234);

  END_TEST;
}

// If we have a multi-step test that specifies a bytes_processed_per_run
// parameter, we should get a result reported for the overall times with a
// bytes_processed_per_run value.  The results for the individual steps
// should not report bytes_processed_per_run.
static bool TestBytesProcessedParameterMultistep() {
  BEGIN_TEST;

  auto test_func = [&](perftest::RepeatState* state) {
    state->SetBytesProcessedPerRun(1234);
    state->DeclareStep("step1");
    state->DeclareStep("step2");
    while (state->KeepRunning()) {
      state->NextStep();
    }
    return true;
  };
  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test{"throughput_test", test_func};
  test_list.push_back(std::move(test));

  const uint32_t kRunCount = 5;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_TRUE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));
  auto* test_cases = results.results();
  ASSERT_EQ(test_cases->size(), 3);
  EXPECT_STR_EQ((*test_cases)[0].label.c_str(), "throughput_test");
  EXPECT_STR_EQ((*test_cases)[1].label.c_str(), "throughput_test.step1");
  EXPECT_STR_EQ((*test_cases)[2].label.c_str(), "throughput_test.step2");
  EXPECT_EQ((*test_cases)[0].bytes_processed_per_run, 1234);
  EXPECT_EQ((*test_cases)[1].bytes_processed_per_run, 0);
  EXPECT_EQ((*test_cases)[2].bytes_processed_per_run, 0);

  END_TEST;
}

// Check that test cases are run in sorted order, sorted by name.
static bool TestRunningInSortedOrder() {
  BEGIN_TEST;

  perftest::internal::TestList test_list;
  perftest::internal::NamedTest test1{"test1", NoOpTest};
  perftest::internal::NamedTest test2{"test2", NoOpTest};
  perftest::internal::NamedTest test3{"test3", NoOpTest};
  // Add tests in non-sorted order.
  test_list.push_back(std::move(test3));
  test_list.push_back(std::move(test1));
  test_list.push_back(std::move(test2));

  const uint32_t kRunCount = 5;
  perftest::ResultsSet results;
  DummyOutputStream out;
  EXPECT_TRUE(
      perftest::internal::RunTests("test-suite", &test_list, kRunCount, "", out.fp(), &results));
  auto* test_cases = results.results();
  ASSERT_EQ(test_cases->size(), 3);
  // Check that the tests are reported as being run in sorted order.
  EXPECT_STR_EQ((*test_cases)[0].label.c_str(), "test1");
  EXPECT_STR_EQ((*test_cases)[1].label.c_str(), "test2");
  EXPECT_STR_EQ((*test_cases)[2].label.c_str(), "test3");

  END_TEST;
}

static bool TestParsingCommandArgs() {
  BEGIN_TEST;

  const char* argv[] = {"unused_argv0",
                        "--runs",
                        "123",
                        "--out",
                        "dest_file",
                        "--filter",
                        "some_regex",
                        "--quiet",
                        "--enable-tracing",
                        "--startup-delay=456"};
  perftest::internal::CommandArgs args;
  perftest::internal::ParseCommandArgs(fbl::count_of(argv), const_cast<char**>(argv), &args);
  EXPECT_EQ(args.run_count, 123);
  EXPECT_STR_EQ(args.output_filename, "dest_file");
  EXPECT_STR_EQ(args.filter_regex, "some_regex");
  EXPECT_TRUE(args.quiet);
  EXPECT_TRUE(args.enable_tracing);
  EXPECT_EQ(args.startup_delay_seconds, 456);

  END_TEST;
}

BEGIN_TEST_CASE(perftest_runner_test)
RUN_TEST(TestResults)
RUN_TEST(TestFailingTest)
RUN_TEST(TestBadKeepRunningCalls)
RUN_TEST(TestMultistepTest)
RUN_TEST(TestDuplicateStepNamesAreRejected)
RUN_TEST(TestNextStepCalledBeforeKeepRunning)
RUN_TEST(TestBadNextStepCalls)
RUN_TEST(TestBytesProcessedParameter)
RUN_TEST(TestBytesProcessedParameterMultistep)
RUN_TEST(TestRunningInSortedOrder)
RUN_TEST(TestParsingCommandArgs)
END_TEST_CASE(perftest_runner_test)

int main(int argc, char** argv) {
  return perftest::PerfTestMain(argc, argv, "fuchsia.zircon.perf_test");
}
