// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "fidl/errors.h"
#include "fidl/error_reporter.h"
#include "test_library.h"

namespace {

using fidl::BaseReportedError;
using fidl::Error;
using fidl::ErrorReporter;

constexpr Error<std::string, std::string> ErrTest (
  "This test error has one string param '{}' and another '{}'."
);

bool ReportErrorFormatParams() {
  BEGIN_TEST;
  ErrorReporter error_reporter;
  std::string param1("param1");
  std::string param2("param2");
  error_reporter.ReportError(ErrTest, param1, param2);

  auto errors = error_reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(),
                 "This test error has one string param 'param1' and another 'param2'.");
  END_TEST;
}

bool MakeReportedErrorThenReportIt() {
  BEGIN_TEST;
  std::string param1("param1");
  std::string param2("param2");
  std::unique_ptr<BaseReportedError> reported_err = ErrorReporter::MakeReportedError(
      ErrTest, param1, param2);
  ErrorReporter error_reporter;
  error_reporter.ReportError(std::move(reported_err));

  auto errors = error_reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(),
                 "This test error has one string param 'param1' and another 'param2'.");
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(report_error_tests)
RUN_TEST(ReportErrorFormatParams)
RUN_TEST(MakeReportedErrorThenReportIt)
END_TEST_CASE(report_error_tests)
