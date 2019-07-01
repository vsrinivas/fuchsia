// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_TEST_CASE_H_
#define ZXTEST_BASE_TEST_CASE_H_

#include <cstdint>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/test-info.h>
#include <zxtest/base/types.h>

namespace zxtest {

// Represents a collection of |TestInfo| with a unique name. Here all the logic for
// unique test definition per testcase exists. Also provides the mechanisms for
// |Test::SetUpTestCase| and |Test::TearDownTestCase|.
class TestCase {
 public:
  // Alias for a Filter function. Parameter names are provided for clarity.
  // This function returns true if |test| in |test_case| should be selected.
  using FilterFn = fbl::Function<bool(const fbl::String& test_case, const fbl::String test)>;

  TestCase() = delete;
  TestCase(const fbl::String& name, internal::SetUpTestCaseFn set_up,
           internal::TearDownTestCaseFn tear_down);
  TestCase(const TestCase&) = delete;
  TestCase(TestCase&&);
  ~TestCase();

  TestCase& operator=(const TestCase&) = delete;
  TestCase& operator=(TestCase&&) = delete;

  // Returns the number of registered tests.
  size_t TestCount() const;

  // Returns the number of registered tests, matching a given filter.
  size_t MatchingTestCount() const;

  // Filters the tests from |test_infos_| that do
  void Filter(FilterFn filter);

  // Shuffles the test execution order |selected_indexes_|.
  void Shuffle(std::uint32_t random_seed);

  // Restores the test execution order to match registration order. This does
  // not remove the effects of filter.
  void UnShuffle();

  // Returns true if registration of |test_info| into this testcase was succesful.
  bool RegisterTest(const fbl::String& name, const SourceLocation& location,
                    internal::TestFactory factory);

  // Executes all registered tests with the provided |driver|.
  void Run(LifecycleObserver* lifecycle_observer, internal::TestDriver* driver);

  // Returns the name of test case.
  const fbl::String& name() const {
    return name_;
  }

  const TestInfo& GetTestInfo(size_t index) const {
    return test_infos_[index];
  }

  // Returns TestInfo of the registered test that matches the filter, at a given
  // offset. If All tests match the filter, then it is equivalent to |TestCase::GetTestInfo|.
  const TestInfo& GetMatchingTestInfo(size_t index) const {
    return test_infos_[selected_indexes_[index]];
  }

  // Test case will return upon encountering the first test failure.
  void SetReturnOnFailure(bool should_return_on_failure) {
    return_on_failure_ = should_return_on_failure;
  }

 private:
  // Keeps track of the tests that were selected
  fbl::Vector<unsigned long> selected_indexes_;

  // Tests in registration order.
  fbl::Vector<TestInfo> test_infos_;

  // Test case name.
  fbl::String name_;

  // Called before any test in |test_infos_| is executed.
  internal::SetUpTestCaseFn set_up_;

  // Called after all tests in |test_infos_| are executed.
  internal::TearDownTestCaseFn tear_down_;

  // Finishes the test execution upon encoutering the first failure.
  bool return_on_failure_ = false;
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_TEST_CASE_H_
