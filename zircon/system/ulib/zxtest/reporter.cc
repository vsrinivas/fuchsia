// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include <utility>

#ifdef __Fuchsia__
#include <lib/zx/time.h>
#else
#include <sys/time.h>
#endif
#include <zxtest/base/reporter.h>
#include <zxtest/base/runner.h>

namespace zxtest {
namespace {

uint64_t now() {
#ifdef __Fuchsia__
  return (zx::clock::get_monotonic() - zx::time(0)).to_nsecs();
#else
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) < 0)
    return 0u;
  return tv.tv_sec * 1000000000ull + tv.tv_usec * 1000ull;
#endif
}

template <typename T>
const char* Pluralize(T value, bool capitalize = false) {
  if (value > 1) {
    return capitalize ? "S" : "s";
  }
  return "";
}

}  // namespace

namespace internal {

Timer::Timer() : start_(now()) {
}

void Timer::Reset() {
  start_ = now();
}

int64_t Timer::GetElapsedTime() const {
  return (now() - start_) / 1000000;
}

void IterationSummary::Reset() {
  failed = 0;
  passed = 0;
  skipped = 0;
  failed_tests.reset();
}
}  // namespace internal

Reporter::Reporter(std::unique_ptr<LogSink> log_sink) : log_sink_(std::move(log_sink)) {
  ZX_ASSERT_MSG(log_sink_ != nullptr, "Must provide a valid |LogSink| implementation.");
}

void Reporter::OnProgramStart(const Runner& runner) {
  timers_.program.Reset();

  log_sink_->Write("[==========] Flag Values:\n");

  // Report value of flags.
  if (!runner.options().filter.empty()) {
    log_sink_->Write("             --gtest_filter = %s\n", runner.options().filter.c_str());
  }

  if (runner.options().shuffle) {
    log_sink_->Write("             --gtest_shuffle = true\n");
  }

  if (runner.options().repeat != 1) {
    log_sink_->Write("             --gtest_repeat = %d\n", runner.options().repeat);
  }

  log_sink_->Write("             --gtest_random_seed = %d\n", runner.options().seed);

  if (runner.options().break_on_failure) {
    log_sink_->Write("             --gtest_break_on_failure = true\n");
  }
  log_sink_->Write("[==========] \n");
  log_sink_->Flush();
}

void Reporter::OnIterationStart(const Runner& runner, int iteration) {
  timers_.iteration.Reset();
  iteration_summary_.Reset();

  if (runner.summary().total_iterations > 1) {
    log_sink_->Write("\nRepeating all tests (iteration %d) . . .\n\n", iteration);
  }

  log_sink_->Write(
      "[==========] Running %zu test%s from %zu test case%s.\n", runner.summary().active_test_count,
      Pluralize(runner.summary().active_test_count), runner.summary().active_test_case_count,
      Pluralize(runner.summary().active_test_case_count));
  log_sink_->Flush();
}

void Reporter::OnEnvironmentSetUp(const Runner& runner) {
  log_sink_->Write("[----------] Global test environment set-up.\n");
  log_sink_->Flush();
}

void Reporter::OnTestCaseStart(const TestCase& test_case) {
  timers_.test_case.Reset();

  log_sink_->Write("[----------] %zu test%s from %s\n", test_case.MatchingTestCount(),
                   Pluralize(test_case.MatchingTestCount()), test_case.name().c_str());
  log_sink_->Flush();
}

void Reporter::OnTestStart(const TestCase& test_case, const TestInfo& test) {
  timers_.test.Reset();
  log_sink_->Write("[ RUN      ] %s.%s\n", test_case.name().c_str(), test.name().c_str());
  log_sink_->Flush();
}

void Reporter::OnAssertion(const Assertion& assertion) {
  log_sink_->Write("%s:%" PRIi64 ": Failure: %s\n", assertion.location().filename,
                   assertion.location().line_number, assertion.description().c_str());

  if (assertion.has_values()) {
    log_sink_->Write("    Expected: %s\n", assertion.expected().c_str());
    // When it is not a literal.
    if (assertion.expected() != assertion.expected_eval()) {
      log_sink_->Write("    Which is: %s\n", assertion.expected_eval().c_str());
    }

    log_sink_->Write("    Actual  : %s\n", assertion.actual().c_str());
    // When it is not a literal.
    if (assertion.actual() != assertion.actual_eval()) {
      log_sink_->Write("    Which is: %s\n", assertion.actual_eval().c_str());
    }
  }
  log_sink_->Flush();
}

void Reporter::OnTestSkip(const TestCase& test_case, const TestInfo& test) {
  int64_t elapsed_time = timers_.test.GetElapsedTime();
  iteration_summary_.skipped++;
  log_sink_->Write("[  SKIPPED ] %s.%s  (%" PRIi64 " ms)\n", test_case.name().c_str(),
                   test.name().c_str(), elapsed_time);
  log_sink_->Flush();
}

void Reporter::OnTestFailure(const TestCase& test_case, const TestInfo& test) {
  int64_t elapsed_time = timers_.test.GetElapsedTime();
  char buffer[test_case.name().size() + test.name().size() + 2];
  sprintf(buffer, "%s.%s", test_case.name().c_str(), test.name().c_str());
  iteration_summary_.failed++;
  iteration_summary_.failed_tests.push_back(buffer);
  log_sink_->Write("[  FAILED  ] %s.%s (%" PRIi64 " ms)\n", test_case.name().c_str(),
                   test.name().c_str(), elapsed_time);
  log_sink_->Flush();
}

void Reporter::OnTestSuccess(const TestCase& test_case, const TestInfo& test) {
  int64_t elapsed_time = timers_.test.GetElapsedTime();
  iteration_summary_.passed++;
  log_sink_->Write("[       OK ] %s.%s (%" PRIi64 " ms)\n", test_case.name().c_str(),
                   test.name().c_str(), elapsed_time);
  log_sink_->Flush();
}

void Reporter::OnTestCaseEnd(const TestCase& test_case) {
  int64_t elapsed_time = timers_.test_case.GetElapsedTime();
  log_sink_->Write("[----------] %zu test%s from %s (%" PRIi64 " ms total)\n\n",
                   test_case.MatchingTestCount(), Pluralize(test_case.MatchingTestCount()),
                   test_case.name().c_str(), elapsed_time);
  log_sink_->Flush();
}

void Reporter::OnEnvironmentTearDown(const Runner& runner) {
  log_sink_->Write("[----------] Global test environment tear-down.\n");
  log_sink_->Flush();
}

void Reporter::OnIterationEnd(const Runner& runner, int iteration) {
  int64_t elapsed_time = timers_.iteration.GetElapsedTime();
  log_sink_->Write("[==========] %zd test%s from %zu test case%s ran (%" PRIi64 " ms total).\n",
                   runner.summary().active_test_count,
                   Pluralize(runner.summary().active_test_count),
                   runner.summary().active_test_case_count,
                   Pluralize(runner.summary().active_test_case_count), elapsed_time);
  if (iteration_summary_.passed > 0) {
    log_sink_->Write("[  PASSED  ] %" PRIu64 " test%s\n", iteration_summary_.passed,
                     Pluralize(iteration_summary_.passed));
  }
  if (iteration_summary_.skipped > 0) {
    log_sink_->Write("[  SKIPPED ] %" PRIu64 " test%s\n", iteration_summary_.skipped,
                     Pluralize(iteration_summary_.skipped));
  }
  if (iteration_summary_.failed > 0) {
    log_sink_->Write("[  FAILED  ] %" PRIu64 " test%s, listed below:\n", iteration_summary_.failed,
                     Pluralize(iteration_summary_.failed));
    if (iteration_summary_.failed_tests.size() > 0) {
      for (auto& failed_test : iteration_summary_.failed_tests) {
        log_sink_->Write("[  FAILED  ] %s\n", failed_test.c_str());
      }
      log_sink_->Write("%" PRIi64 " FAILED TEST%s\n", iteration_summary_.failed,
                       Pluralize(iteration_summary_.failed, /*capitalize*/ true));
    }
  }
  log_sink_->Flush();
}

void Reporter::OnProgramEnd(const Runner& runner) {
  timers_.program.Reset();
}

}  // namespace zxtest
