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
; // H
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
        ; // H
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
