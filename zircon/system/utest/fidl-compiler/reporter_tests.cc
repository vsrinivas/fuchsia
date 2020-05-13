// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "fidl/diagnostics.h"
#include "fidl/reporter.h"
#include "test_library.h"

namespace {

using fidl::Diagnostic;
using fidl::ErrorDef;
using fidl::Reporter;

constexpr ErrorDef<std::string, std::string> ErrTest(
    "This test error has one string param '{}' and another '{}'.");

bool ReportErrorFormatParams() {
  BEGIN_TEST;
  Reporter reporter;
  std::string param1("param1");
  std::string param2("param2");
  reporter.ReportError(ErrTest, param1, param2);

  const auto& errors = reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0]->msg.c_str(),
                 "This test error has one string param 'param1' and another 'param2'.");
  END_TEST;
}

bool MakeErrorThenReportIt() {
  BEGIN_TEST;
  std::string param1("param1");
  std::string param2("param2");
  std::unique_ptr<Diagnostic> reported_err = Reporter::MakeError(ErrTest, param1, param2);
  Reporter reporter;
  reporter.ReportError(std::move(reported_err));

  const auto& errors = reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0]->msg.c_str(),
                 "This test error has one string param 'param1' and another 'param2'.");
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(reporter_tests)
RUN_TEST(ReportErrorFormatParams)
RUN_TEST(MakeErrorThenReportIt)
END_TEST_CASE(reporter_tests)
