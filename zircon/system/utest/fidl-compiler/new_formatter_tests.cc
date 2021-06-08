// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/new_formatter.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace {
std::string Format(const std::string& source) {
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto lib = WithLibraryZx(source, flags);

  // We use a column width of 40, rather than the "real world" 100, to make tests easier to read
  // and write.
  auto formatter = fidl::fmt::NewFormatter(40, lib.Reporter());
  auto result = formatter.Format(lib.source_file(), flags);
  if (!result.has_value()) {
    lib.PrintReports();
    return "PARSE_FAILED";
  }
  return "\n" + result.value();
}

// Ensure that an already properly formatted alias declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, AliasFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqr = bool;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqr = bool;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test that an alias declaration gets wrapped properly.
TEST(NewFormatterTests, AliasOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqrs = bool;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqrs
        = bool;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test an alias declaration in which every token is placed on a newline.
TEST(NewFormatterTests, AliasMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
alias
MyAlias_Abcdefghijklmnopqr
=
bool
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqr = bool;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Ensure that an already properly formatted library declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, LibraryFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test that the library declaration is never wrapped.
TEST(NewFormatterTests, LibraryOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library my.overlong.severely.overflowing.name;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library my.overlong.severely.overflowing.name;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test a library declaration in which every token is placed on a newline.
TEST(NewFormatterTests, LibraryMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library
foo
.
bar
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Ensure that an already properly formatted using declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, UsingFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxy;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxy;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test that a using declaration with no alias does not get wrapped.
TEST(NewFormatterTests, UsingOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxyz;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxyz;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test a using declaration in which every token is placed on a newline.
TEST(NewFormatterTests, UsingMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using
imported
.
abcdefhijklmnopqrstubwxy
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxy;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Ensure that an already properly formatted aliased using declaration is not modified by another
// run through the formatter.
TEST(NewFormatterTests, UsingWithAliasFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using baz.qux as abcdefghijklmnopqrstuv;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using baz.qux as abcdefghijklmnopqrstuv;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test that the aliased using declaration is properly wrapped
TEST(NewFormatterTests, UsingWithAliasOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using baz.qux as abcdefghijklmnopqrstuvw;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using baz.qux
        as abcdefghijklmnopqrstuvw;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test an aliased using declaration in which every token is placed on a newline.
TEST(NewFormatterTests, UsingWithAliasMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
using
baz
.
qux
as
abcdefghijklmnopqrstuv
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using baz.qux as abcdefghijklmnopqrstuv;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// What happens when we have both an inline and standalone comment surrounding each token?
TEST(NewFormatterTests, CommentsMaximal) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
// 0
library // A
// 1
foo // B
// 2
. // C
// 3
bar // D
// 4
; // E
// 5



// 6


// 7
using // F
// 8
baz // G
// 9
as // H
// 10
quz // I
; // 11
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// 0
library // A
        // 1
        foo // B
        // 2
        . // C
        // 3
        bar // D
        // 4
        ; // E
// 5



// 6


// 7
using // F
        // 8
        baz // G
        // 9
        as // H
        // 10
        quz // I
        ; // 11
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, CommentsWeird) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
   // C1
library foo.

// C2

        // C3

bar; using // C4

baz;

   // C5




)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// C1
library foo.

        // C2

        // C3

        bar;
using // C4
        baz;

// C5
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

}  // namespace
