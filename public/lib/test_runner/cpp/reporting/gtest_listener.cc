// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/reporting/gtest_listener.h"

#include <regex>

#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"

namespace test_runner {

GTestListener::GTestListener(const std::string& executable, Reporter* reporter)
    : reporter_(reporter) {
  std::regex file_prefix("^file://");
  executable_ = std::regex_replace(executable, file_prefix, "");
}

GTestListener::~GTestListener() {}

void GTestListener::OnTestEnd(const ::testing::TestInfo& info) {
  auto gtest_result = info.result();

  std::string name(info.test_case_name());
  name += ".";
  name += info.name();

  auto elapsed = gtest_result->elapsed_time();
  bool failed = gtest_result->Failed();

  std::stringstream message;
  int part_count = gtest_result->total_part_count();
  for (int i = 0; i < part_count; i++) {
    auto part_result = gtest_result->GetTestPartResult(i);
    if (part_result.failed()) {
      message << part_result.file_name() << ":" << part_result.line_number()
              << "\n"
              << part_result.message() << "\n";
    }
  }

  if (failed) {
    message << "\nTo reproduce failure:\n"
            << executable_ << " --gtest_filter=" << name << "\n";
  }

  TestResultPtr result = TestResult::New();
  result->name = name;
  result->elapsed = elapsed;
  result->failed = failed;
  result->message = message.str();

  reporter_->Report(std::move(result));
}

void GTestListener::OnTestProgramEnd(const ::testing::UnitTest& test) {}

}  // namespace test_runner
