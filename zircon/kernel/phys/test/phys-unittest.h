// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTEST_H_
#define ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTEST_H_

#include <stdio.h>

#include <ktl/array.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

#include "test-main.h"

// Boilerplate code for creating binaries that will execute a sequence of tests.

// Usage example:
//  my_test_main.cc:
//
//    TEST_SUITES("my_test_main", test_1, ...., test_n);
//

// Helper that generates a unique string of all input names joined by comma.
#define TEST_NAME_GEN(Args...) #Args

#define TEST_SUITES(program_name, ...)                           \
  int TestMain(void*, arch::EarlyTicks) {                        \
    printf("\nRunning unit tests in physical memory...\n");      \
    return Run(TEST_NAME_GEN(__VA_ARGS__), __VA_ARGS__) ? 0 : 1; \
  }                                                              \
  const char Symbolize::kProgramName_[] = program_name

// For simplification |test_names| is the joining of all __VA_ARGS__ with a special token that
// cannot appear in a function name. Then we look for the i-th entry of the test to figure out the
// name.
template <typename... TestFuncs>
bool Run(const char* test_names, TestFuncs... funcs) {
  static_assert(sizeof...(funcs) > 0);
  ZX_ASSERT(test_names != nullptr);

  ktl::array<ktl::string_view, sizeof...(funcs)> failed_tests;

  // Given a list of the concatenated strings with ',' as the joining symbol.

  // Returns the first element of the list.
  auto get_head = [](ktl::string_view name_list) {
    size_t name_end = name_list.find(',');
    return name_list.substr(0, name_end);
  };
  // Returns the |name_list| without its head.
  auto get_tail = [](ktl::string_view name_list) {
    size_t name_end = name_list.find(',');
    if (name_end == ktl::string_view::npos) {
      return ktl::string_view();
    }
    return name_list.substr(name_end + 1);
  };

  size_t bad_count = 0;
  ktl::string_view names(test_names);

  auto process_test = [&](auto func) {
    if (!func()) {
      failed_tests[bad_count] = get_head(names);
      bad_count++;
    }
    names = get_tail(names);
    return true;
  };

  // Order matters here, since each call to |process_test| assumes its looking at test in order,
  // so it can figure out which is the right name for the test being executed.
  (process_test(funcs), ...);

  constexpr size_t arg_count = sizeof...(funcs);
  printf("Ran %zu test suites: %zu succeeded, %zu failed.\n", arg_count, arg_count - bad_count,
         bad_count);

  ktl::span<ktl::string_view> failed_view(failed_tests.data(), bad_count);
  if (failed_view.empty()) {
    return true;
  }

  printf("*** FAILED:\n");
  for (auto failed : failed_view) {
    printf(" %.*s\n", static_cast<int>(failed.length()), failed.data());
  }
  printf(" ***\n\n");
  return false;
}

#endif  // ZIRCON_KERNEL_PHYS_TEST_PHYS_UNITTEST_H_
