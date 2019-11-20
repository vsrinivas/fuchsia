// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <array>

#include "tools/kazoo/test.h"

namespace testing {
Test* g_current_test;
}  // namespace testing

struct RegisteredTest {
  testing::Test* (*factory)();
  const char* name;
  bool should_run;
};

// This can't be a vector because tests call RegisterTest from static
// initializers and the order static initializers run it isn't specified. So a
// vector constructor isn't guaranteed to run before all of the RegisterTest()
// calls.
static std::array<RegisteredTest, 10000> tests;
static int ntests;

void RegisterTest(testing::Test* (*factory)(), const char* name) {
  tests[ntests].factory = factory;
  tests[ntests++].name = name;
}

namespace {

bool PatternMatchesString(const char* pattern, const char* str) {
  switch (*pattern) {
    case '\0':
    case '-':
      return *str == '\0';
    case '*':
      return (*str != '\0' && PatternMatchesString(pattern, str + 1)) ||
             PatternMatchesString(pattern + 1, str);
    default:
      return *pattern == *str && PatternMatchesString(pattern + 1, str + 1);
  }
}

bool TestMatchesFilter(const char* test, const char* filter) {
  // Split --gtest_filter at '-' into positive and negative filters.
  const char* const dash = strchr(filter, '-');
  const char* pos =
      dash == filter ? "*" : filter;  // Treat '-test1' as '*-test1'
  const char* neg = dash ? dash + 1 : "";
  return PatternMatchesString(pos, test) && !PatternMatchesString(neg, test);
}

}  // namespace

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  int tests_started = 0;
  bool break_on_failure = false;

  const char* test_filter = "*";
  for (int i = 1; i < argc; ++i) {
    const char kTestFilterPrefix[] = "--gtest_filter=";
    if (strncmp(argv[i], kTestFilterPrefix, strlen(kTestFilterPrefix)) == 0) {
      test_filter = &argv[i][strlen(kTestFilterPrefix)];
    }

    const char kTestBreakOnFailure[] = "--gtest_break_on_failure";
    if (strcmp(argv[i], kTestBreakOnFailure) == 0) {
      break_on_failure = true;
    }
  }

  int num_active_tests = 0;
  for (int i = 0; i < ntests; i++) {
    tests[i].should_run = TestMatchesFilter(tests[i].name, test_filter);
    if (tests[i].should_run) {
      ++num_active_tests;
    }
  }

  const char* prefix = "";
  const char* suffix = "\n";
  if (isatty(1)) {
    prefix = "\r";
    suffix = "\x1B[K";
  }
  bool passed = true;
  for (int i = 0; i < ntests; i++) {
    if (!tests[i].should_run)
      continue;

    ++tests_started;
    testing::Test* test = tests[i].factory();
    printf("%s[%d/%d] %s%s", prefix, tests_started, num_active_tests,
           tests[i].name, suffix);
    test->SetUp();
    test->Run();
    test->TearDown();
    if (test->Failed()) {
      passed = false;
      if (break_on_failure) {
        __builtin_trap();
      }
    }
    delete test;
  }

  printf("\n%s\n", passed ? "PASSED" : "FAILED");
  fflush(stdout);
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
