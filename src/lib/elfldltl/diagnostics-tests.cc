// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>

#include <sstream>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

#ifndef ASSERT_DEATH
#define ASSERT_DEATH(...)  // Not available on host.
#endif

TEST(ElfldltlDiagnosticsTests, Trap) {
  auto diag = elfldltl::TrapDiagnostics();

  EXPECT_EQ(1, diag.errors());
  ASSERT_DEATH([diag]() mutable { diag.FormatError("errors are fatal"); });

  EXPECT_EQ(1, diag.warnings());
  ASSERT_DEATH([diag]() mutable { diag.FormatWarning("warnings are fatal"); });
}

TEST(ElfldltlDiagnosticsTests, Panic) {
  auto diag = elfldltl::PanicDiagnostics();

  EXPECT_EQ(1, diag.errors());
  ASSERT_DEATH([diag]() mutable { diag.FormatError("errors are fatal"); });

  EXPECT_EQ(1, diag.warnings());
  ASSERT_DEATH([diag]() mutable { diag.FormatWarning("warnings are fatal"); });
}

TEST(ElfldltlDiagnosticsTests, OneString) {
  std::string error = "no error";
  auto diag = elfldltl::OneStringDiagnostics(error);

  EXPECT_FALSE(diag.FormatError("first error"));
  EXPECT_STREQ(error, "first error");
  EXPECT_EQ(1, diag.errors());

  EXPECT_FALSE(diag.FormatError("second error"));
  EXPECT_STREQ(error, "second error");
  EXPECT_EQ(2, diag.errors());

  EXPECT_FALSE(diag.FormatWarning("warning"));
  EXPECT_STREQ(error, "warning");
  EXPECT_EQ(1, diag.warnings());
  EXPECT_EQ(2, diag.errors());
}

TEST(ElfldltlDiagnosticsTests, CollectStrings) {
  std::vector<std::string> errors;
  const elfldltl::DiagnosticsFlags flags = {.multiple_errors = true};
  auto diag = elfldltl::CollectStringsDiagnostics(errors, flags);

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  EXPECT_TRUE(diag.FormatError("first error"));
  EXPECT_EQ(1, errors.size());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(1, diag.errors());

  EXPECT_TRUE(diag.FormatError("second error"));
  EXPECT_EQ(2, errors.size());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(2, diag.errors());

  EXPECT_TRUE(diag.FormatWarning("warning"));
  EXPECT_EQ(3, errors.size());
  EXPECT_EQ(1, diag.warnings());
  EXPECT_EQ(2, diag.errors());

  ASSERT_GE(errors.size(), 3);
  EXPECT_STREQ(errors[0], "first error");
  EXPECT_STREQ(errors[1], "second error");
  EXPECT_STREQ(errors[2], "warning");
}

TEST(ElfldltlDiagnosticsTests, Ostream) {
  std::stringstream sstr;
  const elfldltl::DiagnosticsFlags flags = {.multiple_errors = true};
  auto diag = elfldltl::OstreamDiagnostics(sstr, flags, 'a', 1, ":");

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  EXPECT_TRUE(diag.FormatError("first error"));
  EXPECT_EQ(1, diag.errors());

  EXPECT_TRUE(diag.FormatError("second error"));
  EXPECT_EQ(2, diag.errors());

  EXPECT_TRUE(diag.FormatWarning("warning"));
  EXPECT_EQ(1, diag.warnings());
  EXPECT_EQ(2, diag.errors());

  EXPECT_STREQ(sstr.str(),
               "a1:first error\n"
               "a1:second error\n"
               "a1:warning\n");
}

}  // namespace
