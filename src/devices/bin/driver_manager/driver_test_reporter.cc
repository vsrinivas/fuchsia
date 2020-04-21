// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_test_reporter.h"

#include <stdio.h>

#include "src/devices/lib/log/log.h"

void DriverTestReporter::LogMessage(const char* msg, size_t size) {
  LOGF(INFO, "[----------][%s] %.*s\n", driver_name_.data(), static_cast<int>(size), msg);
}

void DriverTestReporter::LogTestCase(const char* name, size_t name_size,
                                     const fuchsia_driver_test_TestCaseResult* result) {
  uint64_t ran = result->passed + result->failed;
  LOGF(INFO, "[----------] %lu tests from %s.%.*s\n", ran, driver_name_.data(),
       static_cast<int>(name_size), name);
  LOGF(INFO, "[----------] %lu passed\n", result->passed);
  LOGF(INFO, "[----------] %lu failed\n", result->failed);
  LOGF(INFO, "[----------] %lu skipped\n", result->skipped);
  if (result->failed == 0) {
    LOGF(INFO, "[       OK ] %s.%.*s\n", driver_name_.data(), static_cast<int>(name_size), name);
  } else {
    LOGF(INFO, "[     FAIL ] %s.%.*s\n", driver_name_.data(), static_cast<int>(name_size), name);
  }
  total_cases_ += 1;
  total_passed_ += result->passed;
  total_failed_ += result->failed;
  total_skipped_ += result->skipped;
}

void DriverTestReporter::TestStart() {
  LOGF(INFO, "[==========] Running driver unit tests: %s.\n", driver_name_.data());
}

void DriverTestReporter::TestFinished() {
  uint64_t total_ran = total_passed_ + total_failed_;
  if (total_skipped_ == 0) {
    LOGF(INFO, "[==========] %lu test from %lu test cases ran.\n", total_ran, total_cases_);
  } else {
    LOGF(INFO, "[==========] %lu test from %lu test cases ran (%lu skipped).\n", total_ran,
         total_cases_, total_skipped_);
  }
  if (total_failed_ == 0) {
    LOGF(INFO, "[  PASSED  ] %s: %lu tests passed.\n", driver_name_.data(), total_passed_);
  } else {
    LOGF(INFO, "[  FAILED  ] %s: %lu tests failed.\n", driver_name_.data(), total_failed_);
  }
}
