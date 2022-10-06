// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_EXPECTATIONS_H_
#define THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_EXPECTATIONS_H_

#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "third_party/gvisor_syscall_tests/gvisor/test/util/test_util.h"

namespace netstack_syscall_test {

enum class TestOption { kSuccess, kFailure, kSkip };

class TestSelector {
 public:
  // Parses a test selector a string.
  //
  // Test selectors can be for parameterized tests, in which case they take the
  // form "A/B.C/D", or for unparameterized tests, in which case they look like
  // "A.B". Selectors can match individual tests by fully specifying all
  // components, or hierarchical groups of tests by using wildcards ('*').
  //
  // The selector "A/B.C/*" will match tests "A/B.C/D1" and "A/B.C/D2" but not
  // "A/B.C2/D1". Note that while multiple components can be wildcarded, they
  // must all appear as a contiguous suffix, so "A/*.C/*" is illegal, as is
  // "A/B.*/D". A selector cannot consist only of wildcard components.
  static absl::StatusOr<TestSelector> Parse(absl::string_view name);

  // Specifies a parameterized test with its four named components.
  static TestSelector ParameterizedTest(absl::string_view suite, absl::string_view name,
                                        absl::string_view test_case, absl::string_view parameter);

  // Specifies an unparameterized test with its two named components.
  static TestSelector Test(absl::string_view name, absl::string_view test_case);

  // Returns all selectors whose match groups are a superset of this one's.
  //
  // The returned selectors in order from largest match pool to smallest, where
  // smallest is exactly *this.
  std::vector<TestSelector> Selectors() &&;

  template <typename H>
  friend H AbslHashValue(H h, const TestSelector& self) {
    return H::combine(std::move(h), self.prefix_);
  }

  friend std::ostream& operator<<(std::ostream& out, const TestSelector& selector);
  friend bool operator==(const TestSelector& lhs, const TestSelector& rhs);

 private:
  struct Parameterized {
    explicit Parameterized(std::string prefix) : prefix(std::move(prefix)) {}
    template <typename H>
    friend H AbslHashValue(H h, const Parameterized& self) {
      return H::combine(std::move(h), self.prefix);
    }
    friend bool operator==(const Parameterized& lhs, const Parameterized& rhs) {
      return lhs.prefix == rhs.prefix;
    }

    std::string prefix;
  };

  explicit TestSelector(std::variant<Parameterized, std::string> prefix);

  const absl::variant<Parameterized, std::string> prefix_;
};

using TestMap = absl::flat_hash_map<TestSelector, TestOption>;

void AddExpectations(TestMap& map, TestSelector test_selector, TestOption expect);

inline void ExpectFailure(TestMap& map, TestSelector test_selector) {
  AddExpectations(map, std::move(test_selector), TestOption::kFailure);
}
inline void ExpectFailure(TestMap& map, absl::string_view test_selector) {
  ExpectFailure(map, TestSelector::Parse(test_selector).value());
}

// Used to skip flaky tests or tests that timeout.
inline void SkipTest(TestMap& map, TestSelector test_selector) {
  AddExpectations(map, std::move(test_selector), TestOption::kSkip);
}
inline void SkipTest(TestMap& map, absl::string_view test_selector) {
  SkipTest(map, TestSelector::Parse(test_selector).value());
}

std::tuple<std::string, TestMap::const_iterator> GetTestNameAndExpectation(
    const testing::TestInfo& info, const TestMap& expectations);

inline std::string TestOptionToString(const TestOption option) {
  switch (option) {
    case TestOption::kSuccess:
      return "success";
    case TestOption::kFailure:
      return "failure";
    case TestOption::kSkip:
      return "skip";
  }
}

// Only tests marked as `kSkip` in `tests` are not included in the filter,
// causing them to be skipped by GUnit.
// Tests are expected to pass if they are neither skipped nor expected to fail.
// These tests are not added to the `tests` map, allowing us to auto include
// newly added tests upstream.
std::optional<std::string> CreateNetstackTestFilters(const TestMap& expectations);

}  // namespace netstack_syscall_test

#endif  // THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_EXPECTATIONS_H_
