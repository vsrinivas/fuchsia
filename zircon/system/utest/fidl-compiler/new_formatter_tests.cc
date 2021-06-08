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

// Ensure that already properly formatted const declarations are not modified by another run
// through the formatter.
TEST(NewFormatterTests, ConstFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
const MY_TRUE_ABCDEFGHIJKLM bool = true;
const MY_FALSE_ABCDEFGHIJK bool = false;
const MY_UINT64_AB uint64 = 12345678900;
const MY_FLOAT64_ABCDEF float64 = 12.34;
const MY_STRING_ABCDEFGH string = "foo";
const MY_OR_A uint64 = 1 | MY_UINT64_AB;
const MY_ORS_ABCDEFG uint64 = 1 | 2 | 3;
const MY_REF_ABCD uint64 = MY_UINT64_AB;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
const MY_TRUE_ABCDEFGHIJKLM bool = true;
const MY_FALSE_ABCDEFGHIJK bool = false;
const MY_UINT64_AB uint64 = 12345678900;
const MY_FLOAT64_ABCDEF float64 = 12.34;
const MY_STRING_ABCDEFGH string = "foo";
const MY_OR_A uint64 = 1 | MY_UINT64_AB;
const MY_ORS_ABCDEFG uint64 = 1 | 2 | 3;
const MY_REF_ABCD uint64 = MY_UINT64_AB;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// The const declaration has two levels of subspanning: the first is split at the equal sign, while
// the second is split at the type declaration.  This test cases tests for "partial" overflows where
// the first level of subspanning is invoked: the whole line is too long, but the `const NAME TYPE`
// portion still fits on the first line.
TEST(NewFormatterTests, ConstPartialOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
const MY_TRUE_ABCDEFGHIJKLMN bool = true;
const MY_FALSE_ABCDEFGHIJKL bool = false;
const MY_UINT64_ABC uint64 = 12345678900;
const MY_FLOAT64_ABCDEFG float64 = 12.34;
const MY_STRING_ABCDEFGHI string = "foo";
const MY_REF_ABCD uint64 = MY_UINT64_ABC;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
const MY_TRUE_ABCDEFGHIJKLMN bool
        = true;
const MY_FALSE_ABCDEFGHIJKL bool
        = false;
const MY_UINT64_ABC uint64
        = 12345678900;
const MY_FLOAT64_ABCDEFG float64
        = 12.34;
const MY_STRING_ABCDEFGHI string
        = "foo";
const MY_REF_ABCD uint64
        = MY_UINT64_ABC;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Tests cases where even the nested subspan to the left of the equal sign is longer than the
// overflow window.  Note that this test case looks a bit unusual because the name is very long, but
// the type is very short.  In reality, both would probably have to be quite long to cause this kind
// of overflow, so the output will look less "lopsided."
TEST(NewFormatterTests, ConstTotalOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
const MY_WAY_TOO_LONG_TRUE_ABCDEFGHIJKLMN bool = true;
const MY_WAY_TOO_LONG_FALSE_ABCDEFGHIJKLM bool = false;
const MY_WAY_TOO_LONG_UINT64_ABCDEFGHIJKL uint64 = 12345678900;
const MY_WAY_TOO_LONG_FLOAT64_ABCDEFGHIJK float64 = 12.34;
const MY_WAY_TOO_LONG_STRING_ABCDEFGHIJKL string = "foo";
const MY_WAY_TOO_LONG_REF_ABCDEFGHIJKLMNO uint64 = MY_WAY_TOO_LONG_UINT64_ABCDEFGHIJKL;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
const MY_WAY_TOO_LONG_TRUE_ABCDEFGHIJKLMN
        bool
        = true;
const MY_WAY_TOO_LONG_FALSE_ABCDEFGHIJKLM
        bool
        = false;
const MY_WAY_TOO_LONG_UINT64_ABCDEFGHIJKL
        uint64
        = 12345678900;
const MY_WAY_TOO_LONG_FLOAT64_ABCDEFGHIJK
        float64
        = 12.34;
const MY_WAY_TOO_LONG_STRING_ABCDEFGHIJKL
        string
        = "foo";
const MY_WAY_TOO_LONG_REF_ABCDEFGHIJKLMNO
        uint64
        = MY_WAY_TOO_LONG_UINT64_ABCDEFGHIJKL;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test const declarations where every token is placed on a newline.
TEST(NewFormatterTests, ConstMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;
const
MY_TRUE_ABCDEFGHIJKLM
bool
=
true
;
const
MY_FALSE_ABCDEFGHIJK
bool
=
false
;
const
MY_UINT64_AB
uint64
=
12345678900
;
const
MY_FLOAT64_ABCDEF
float64
=
12.34
;
const
MY_STRING_ABCDEFGH
string
=
"foo"
;
const
MY_OR_A
uint64
=
1
|
MY_UINT64_AB
;
const
MY_ORS_ABCDEFG
uint64
=
1
|
2
|
3
;
const
MY_REF_ABCD
uint64
=
MY_UINT64_AB
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
const MY_TRUE_ABCDEFGHIJKLM bool = true;
const MY_FALSE_ABCDEFGHIJK bool = false;
const MY_UINT64_AB uint64 = 12345678900;
const MY_FLOAT64_ABCDEF float64 = 12.34;
const MY_STRING_ABCDEFGH string = "foo";
const MY_OR_A uint64 = 1 | MY_UINT64_AB;
const MY_ORS_ABCDEFG uint64 = 1 | 2 | 3;
const MY_REF_ABCD uint64 = MY_UINT64_AB;
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
