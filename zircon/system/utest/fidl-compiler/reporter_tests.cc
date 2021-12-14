// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "fidl/diagnostics.h"
#include "fidl/reporter.h"
#include "test_library.h"

namespace {

using fidl::Diagnostic;
using fidl::ErrorDef;
using fidl::Reporter;
using fidl::WarningDef;

constexpr ErrorDef<std::string_view, std::string_view> ErrTest(
    "This test error has one string param '{}' and another '{}'.");
constexpr WarningDef<std::string_view, std::string_view> WarnTest(
    "This test warning has one string param '{}' and another '{}'.");

TEST(ReporterTests, ReportErrorFormatParams) {
  Reporter reporter;
  std::string param1("param1");
  std::string param2("param2");
  reporter.Report(ErrTest, param1, param2);

  const auto& errors = reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_SUBSTR(errors[0]->msg.c_str(),
                "This test error has one string param 'param1' and another 'param2'.");
}

TEST(ReporterTests, MakeErrorThenReportIt) {
  Reporter reporter;
  std::string param1("param1");
  std::string param2("param2");
  std::unique_ptr<Diagnostic> reported_err = Reporter::MakeError(ErrTest, param1, param2);
  reporter.Report(std::move(reported_err));

  const auto& errors = reporter.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_SUBSTR(errors[0]->msg.c_str(),
                "This test error has one string param 'param1' and another 'param2'.");
}

TEST(ReporterTests, ReportWarningFormatParams) {
  Reporter reporter;
  std::string param1("param1");
  std::string param2("param2");
  reporter.Report(WarnTest, param1, param2);

  const auto& warnings = reporter.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_SUBSTR(warnings[0]->msg.c_str(),
                "This test warning has one string param 'param1' and another 'param2'.");
}

TEST(ReporterTests, MakeWarningThenReportIt) {
  Reporter reporter;
  std::string param1("param1");
  std::string param2("param2");
  std::unique_ptr<Diagnostic> reported_err = Reporter::MakeWarning(WarnTest, param1, param2);
  reporter.Report(std::move(reported_err));

  const auto& warnings = reporter.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_SUBSTR(warnings[0]->msg.c_str(),
                "This test warning has one string param 'param1' and another 'param2'.");
}

TEST(ReporterTests, CheckpointNoNewErrors) {
  Reporter reporter;
  reporter.Report(ErrTest, std::string("1"), std::string());
  auto checkpoint = reporter.Checkpoint();
  EXPECT_TRUE(checkpoint.NoNewErrors());
  reporter.Report(ErrTest, std::string("2"), std::string());
  EXPECT_FALSE(checkpoint.NoNewErrors());
}

TEST(ReporterTests, CheckpointNoNewWarnings) {
  Reporter reporter;
  reporter.Report(WarnTest, std::string("1"), std::string());
  auto checkpoint = reporter.Checkpoint();
  EXPECT_TRUE(checkpoint.NoNewWarnings());
  reporter.Report(WarnTest, std::string("2"), std::string());
  EXPECT_FALSE(checkpoint.NoNewWarnings());
}

}  // namespace
