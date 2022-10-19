// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>

#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "tests.h"

namespace {

#ifndef ASSERT_DEATH
#define ASSERT_DEATH(...)  // Not available on host.
#endif

TEST(ElfldltlDiagnosticsTests, PrintfDiagnosticsReport) {
  std::array<char, 200> buffer{};
  auto printer = [&buffer](const char* format, auto&&... args) {
    snprintf(buffer.data(), buffer.size(), format, std::forward<decltype(args)>(args)...);
  };

  constexpr uint32_t kPrefixValue = 42;
  constexpr std::string_view kPrefixStringView = ": ";
  auto report =
      elfldltl::PrintfDiagnosticsReport(printer, "prefix", kPrefixValue, kPrefixStringView);

  constexpr std::string_view kStringViewArg = "foo";
  constexpr uint32_t kValue32 = 123;
  constexpr uint64_t kValue64 = 456;
  constexpr uint32_t kOffset32 = 0x123;
  constexpr uint64_t kOffset64 = 0x456;
  constexpr uint32_t kAddress32 = 0x1234;
  constexpr uint64_t kAddress64 = 0x4567;
  decltype(auto) retval =
      report(kStringViewArg, kValue32, "bar", kValue64, elfldltl::FileOffset{kOffset32},
             elfldltl::FileOffset{kOffset64}, elfldltl::FileAddress{kAddress32},
             elfldltl::FileAddress{kAddress64});

  static_assert(std::is_same_v<decltype(retval), bool>);
  EXPECT_TRUE(retval);

  ASSERT_EQ(buffer.back(), '\0');
  EXPECT_STREQ(
      "prefix 42: foo 123bar 456"
      " at file offset 0x123 at file offset 0x456"
      " at relative address 0x1234 at relative address 0x4567",
      buffer.data());
}

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

template <size_t... Args>
auto CreateExpect(std::index_sequence<Args...>) {
  return ExpectedSingleError{"error ", Args...};
}

template <typename Diag, size_t... Args>
auto CreateError(Diag& diag, std::index_sequence<Args...>) {
  diag.FormatError("error ", Args...);
}

TEST(ElfldltlDiagnosticsTests, FormatErrorVariadic) {
  {
    ExpectedSingleError expected("abc ", 123ull, " --- ", 45678910);
    expected.diag().FormatError("abc ", 123ull, " --- ", 45678910);
  }
  {
    auto expected = CreateExpect(std::make_index_sequence<20>{});
    CreateError(expected.diag(), std::make_index_sequence<20>{});
  }
}

TEST(ElfldltlDiagnosticsTests, ResourceError) {
  {
    ExpectedSingleError expected("error", ": cannot allocate ", 5);
    expected.diag().ResourceError("error", 5);
  }
  {
    ExpectedSingleError expected("error");
    expected.diag().ResourceError("error");
  }
}

TEST(ElfldltlDiagnosticsTests, ResourceLimit) {
  {
    ExpectedSingleError expected("error", ": maximum 501 < requested ", 723);
    expected.diag().ResourceLimit<501>("error", 723);
  }
  {
    ExpectedSingleError expected("error", ": maximum 5");
    expected.diag().ResourceLimit<5>("error");
  }
}

}  // namespace
