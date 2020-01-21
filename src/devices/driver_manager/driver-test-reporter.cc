// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver-test-reporter.h"

#include <stdio.h>

namespace devmgr {

void DriverTestReporter::LogMessage(const char* msg, size_t size) {
  fprintf(stdout, "[----------][%s] %.*s\n", driver_name_.data(), static_cast<int>(size), msg);
}

void DriverTestReporter::LogTestCase(const char* name, size_t name_size,
                                     const fuchsia_driver_test_TestCaseResult* result) {
  uint64_t ran = result->passed + result->failed;
  fprintf(stdout, "[----------] %lu tests from %s.%.*s\n", ran, driver_name_.data(),
          static_cast<int>(name_size), name);
  fprintf(stdout, "[----------] %lu passed\n", result->passed);
  fprintf(stdout, "[----------] %lu failed\n", result->failed);
  fprintf(stdout, "[----------] %lu skipped\n", result->skipped);
  if (result->failed == 0) {
    fprintf(stdout, "[       OK ] %s.%.*s\n", driver_name_.data(), static_cast<int>(name_size),
            name);
  } else {
    fprintf(stdout, "[     FAIL ] %s.%.*s\n", driver_name_.data(), static_cast<int>(name_size),
            name);
  }
  total_cases_ += 1;
  total_passed_ += result->passed;
  total_failed_ += result->failed;
  total_skipped_ += result->skipped;
}

void DriverTestReporter::TestStart() {
  fprintf(stdout, "[==========] Running driver unit tests: %s.\n", driver_name_.data());
}

void DriverTestReporter::TestFinished() {
  uint64_t total_ran = total_passed_ + total_failed_;
  if (total_skipped_ == 0) {
    fprintf(stdout, "[==========] %lu test from %lu test cases ran.\n", total_ran, total_cases_);
  } else {
    fprintf(stdout, "[==========] %lu test from %lu test cases ran (%lu skipped).\n", total_ran,
            total_cases_, total_skipped_);
  }
  if (total_failed_ == 0) {
    fprintf(stdout, "[  PASSED  ] %s: %lu tests passed.\n", driver_name_.data(), total_passed_);
  } else {
    fprintf(stdout, "[  FAILED  ] %s: %lu tests failed.\n", driver_name_.data(), total_failed_);
  }
}

}  // namespace devmgr
