// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_ERROR_REPORTING_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_ERROR_REPORTING_TEST_H_

#include <string>
#include <vector>

#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "gtest/gtest.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic {
namespace gfx {
namespace test {

// Use of this macro allows us to remain consistent with gtest syntax, aiding
// readability.
#define EXPECT_ERROR_COUNT(n) ExpectErrorCount((n))

class TestErrorReporter : public ErrorReporter {
 public:
  const std::vector<std::string>& errors() const { return reported_errors_; }

 private:
  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  std::vector<std::string> reported_errors_;
};

class ErrorReportingTest : public ::gtest::TestLoopFixture {
 protected:
  ErrorReporter* error_reporter() { return &error_reporter_; }

  // Verify that the expected number of errors were reported.
  void ExpectErrorCount(size_t errors_expected) {
    EXPECT_EQ(errors_expected, error_reporter_.errors().size());
  }

  // Verify that the error at position |pos| in the list is as expected.  If
  // |pos| is >= the number of errors, then the verification will fail.  If no
  // error is expected, use nullptr as |expected_error_string|.
  void ExpectErrorAt(size_t pos, const char* expected_error_string);

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string);

 private:
  TestErrorReporter error_reporter_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_ERROR_REPORTING_TEST_H_
