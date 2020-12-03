// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys-unittests.h"

#include <stdio.h>

#include <ktl/span.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "phys-unittests";

// This isn't more straightforwardly table-driven because even as a
// function-local variable the compiler will try to turn the table
// into a const global with relocations.
#define TEST_SUITES(SUITE) \
  SUITE(stack_tests)       \
  SUITE(relocation_tests)  \
  SUITE(popcount_tests)    \
  SUITE(printf_tests)      \
  SUITE(string_view_tests) \
  SUITE(unittest_tests)    \
  SUITE(zbitl_tests)

#define COUNT(suite) +1
constexpr int kNumSuites = TEST_SUITES(COUNT);
#undef COUNT

int TestMain(void*, arch::EarlyTicks) {
  printf("\nRunning unit tests in physical memory...\n");

  const char* failed_suites[kNumSuites];
  int good = 0, bad = 0;
  auto run = [&](bool (*suite)(), const char* name) {
    if (suite()) {
      ++good;
    } else {
      failed_suites[bad++] = name;
    }
  };

#define RUN(suite) run(suite, #suite);
  TEST_SUITES(RUN)
#undef RUN

  printf("Ran %d test suites: %d succeeded, %d failed.\n", good + bad, good, bad);

  if (bad) {
    printf("*** FAILED:");
    for (const char* name : ktl::span(failed_suites, bad)) {
      printf(" %s", name);
    }
    printf(" ***\n\n");
  }

  return bad;
}
