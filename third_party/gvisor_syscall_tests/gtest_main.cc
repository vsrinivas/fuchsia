// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "expectations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_split.h"
#include "expectations.h"
#include "gvisor/test/util/test_util.h"

namespace netstack_syscall_test {

// Adds the expectations for tests that should fail or be skipped. This is a
// forward declaration only; the actual definition should be provided by another
// object present at link time.
extern void AddNonPassingTests(TestMap& tests);

// Compare actual test results with `expectations`.
// Tests that are not included by `expectations` are expected to pass.
// This allows us to auto include newly added tests upstream.
// Return 0 when all test results match expectations.
// In all other cases, output all error or failure message to stderr and
// return 1.
static int ValidateTestResults(const TestMap& expectations) {
  int return_code = 0;
  const auto* instance = testing::UnitTest::GetInstance();
  absl::flat_hash_set<std::reference_wrapper<const TestMap::key_type>> used_expectations,
      tests_in_another_shard;
  for (int i = 0; i < instance->total_test_suite_count(); i++) {
    const auto* suite = instance->GetTestSuite(i);
    for (int j = 0; j < suite->total_test_count(); j++) {
      const auto* test = suite->GetTestInfo(j);
      auto [test_name, expectation_it] = GetTestNameAndExpectation(*test, expectations);

      if (!test->should_run()) {
        // Record if we have an expectation for the test but the test is in
        // another shard.
        if (expectation_it != expectations.end() && test->is_in_another_shard()) {
          tests_in_another_shard.insert(expectation_it->first);
        }
        continue;
      }

      // Tests are expected to pass if they don't exist in `expectations`.
      // This means they are neither skipped nor expected to fail.
      const auto expected_result =
          expectation_it == expectations.end() ? TestOption::kSuccess : expectation_it->second;

      const auto actual_result =
          test->result()->Failed() ? TestOption::kFailure : TestOption::kSuccess;

      if (actual_result != expected_result) {
        std::cerr << "[ SYSCALL TEST UNEXPECTED RESULT ] Test result for \"" << test_name
                  << "\" didn't meet expectation. Actual test result: "
                  << TestOptionToString(actual_result)
                  << ", expect: " << TestOptionToString(expected_result) << std::endl;
        return_code = 1;
      }
      if (expectation_it != expectations.end()) {
        // Store all used expectations in a set to find unused ones at the end.
        used_expectations.insert(expectation_it->first);
      }
    }
  }

  for (const auto& [test_name, expected_result] : expectations) {
    // The only way we allow a test being unexpectedly skipped is if it is in
    // another test shard.
    if (used_expectations.find(test_name) == used_expectations.end() &&
        tests_in_another_shard.find(test_name) == tests_in_another_shard.end() &&
        expected_result != TestOption::kSkip) {
      std::cerr << "[ SYSCALL TEST UNEXPECTED RESULT ] Test result for \"" << test_name
                << "\" didn't meet expectation. Actual test result: "
                << TestOptionToString(TestOption::kSkip)
                << ", expect: " << TestOptionToString(expected_result) << std::endl;
      return_code = 1;
    }
  }
  return return_code;
}

}  // namespace netstack_syscall_test

template <typename T>
void SetEnv(const char* name, const T& value) {
  std::stringstream ss;
  ss << value;
  auto val_str = ss.str();
  if (setenv(name, val_str.c_str(), 1) != 0) {
    std::cerr << "Failed to set envrionement variable " << name << " to " << val_str << std::endl;
    exit(1);
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  absl::ParseCommandLine(argc, argv);

  SetEnv(gvisor::testing::kTestOnGvisor, gvisor::testing::Platform::kFuchsia);

  auto filter = GTEST_FLAG_GET(filter);
  if (filter != "*") {
    // We have an explicit filter flag; run the tests normally.
    return RUN_ALL_TESTS();
  }

  // The "nonPassingTests" map includes all tests to be skipped or are expected
  // to fail. All tests added to this map should be marked with a comment
  // reasoning why. If the test should eventually pass, please file a bug and
  // reference the bug in the comment.
  netstack_syscall_test::TestMap nonPassingTests;
  netstack_syscall_test::AddNonPassingTests(nonPassingTests);
  std::optional test_filter = CreateNetstackTestFilters(nonPassingTests);
  if (!test_filter) {
    return 2;
  }
  GTEST_FLAG_SET(filter, *test_filter);
  // Discarding return value from RUN_ALL_TESTS because we expect some tests to
  // fail. ValidateTestResults below will compare test results with
  // expectations.
  std::cerr << RUN_ALL_TESTS() << std::endl;

  return ValidateTestResults(std::move(nonPassingTests));
}
