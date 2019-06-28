// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cinttypes>
#include <cstdio>

#include <zircon/compiler.h>
#include <zxtest/base/reporter.h>

int main(int argc, char** argv) {
  zxtest::internal::Timer test_timer, iteration_timer, test_case_timer;
  fprintf(stdout, "[==========] Running %lu tests from 1 test case.\n",
          countof(zxtest::test::kRegisteredTests));
  iteration_timer.Reset();
  fprintf(stdout, "[----------] Global test environment set-up.\n");
  test_case_timer.Reset();
  fprintf(stdout, "[----------] %lu tests from ZxTestSmokeTests\n",
          countof(zxtest::test::kRegisteredTests));
  for (auto& test : zxtest::test::kRegisteredTests) {
    test_timer.Reset();
    fprintf(stdout, "[ RUN      ] ZxTestSmokeTest.%s\n", test.name);
    test.test_fn();
    fprintf(stdout, "[       OK ] ZxTestSmokeTest.%s (%" PRIi64 " ms)\n", test.name,
            test_timer.GetElapsedTime());
  }
  fprintf(stdout, "[----------] %lu tests from ZxTestSmokeTest (%" PRIi64 " ms total)\n\n",
          countof(zxtest::test::kRegisteredTests), test_case_timer.GetElapsedTime());
  fprintf(stdout, "[----------] Global test environment tear-down.\n");
  fprintf(stdout, "[==========] %ld tests from 1 test case ran (%" PRIi64 " ms total).\n",
          countof(zxtest::test::kRegisteredTests), iteration_timer.GetElapsedTime());
  fprintf(stdout, "[  PASSED  ] %lu tests\n", countof(zxtest::test::kRegisteredTests));
  return 0;
}
