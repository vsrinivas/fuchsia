// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"

namespace scenic {
namespace gfx {
namespace test {

void TestErrorReporter::ReportError(fxl::LogSeverity severity,
                                    std::string error_string) {
  // Typically, we don't want to log expected errors when running the tests.
  // However, it is useful to print these errors while writing the tests.
#ifndef NDEBUG
  // Allow force printing of errors via --verbose=3 as a parameter.
  if (FXL_VLOG_IS_ON(3)) {
    switch (severity) {
      case ::fxl::LOG_INFO:
        FXL_LOG(INFO) << error_string;
        break;
      case ::fxl::LOG_WARNING:
        FXL_LOG(WARNING) << error_string;
        break;
      case ::fxl::LOG_ERROR:
        FXL_LOG(ERROR) << error_string;
        break;
      case ::fxl::LOG_FATAL:
        FXL_LOG(FATAL) << error_string;
        break;
    }
  }
#endif

  reported_errors_.push_back(error_string);
}

void ErrorReportingTest::ExpectErrorAt(size_t pos,
                                       const char* expected_error_string) {
  if (expected_error_string) {
    // Ensure pos is inside the array and references the error string.
    EXPECT_LT(pos, error_reporter_.errors().size());
    EXPECT_STREQ(error_reporter_.errors()[pos].c_str(), expected_error_string);
  } else {
    // Error string was not present, so ensure pos is outside of the errors
    // array.
    EXPECT_TRUE(pos >= error_reporter_.errors().size());
  }
}

void ErrorReportingTest::ExpectLastReportedError(
    const char* expected_error_string) {
  if (error_reporter_.errors().empty()) {
    EXPECT_EQ(nullptr, expected_error_string);
  } else {
    ExpectErrorAt(error_reporter_.errors().size() - 1, expected_error_string);
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
