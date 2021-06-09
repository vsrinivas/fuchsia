// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/new_formatter.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace {
std::string Format(const std::string& source, bool reformat_and_compare = true) {
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto lib = WithLibraryZx(source, flags);

  // We use a column width of 40, rather than the "real world" 100, to make tests easier to read
  // and write.
  auto formatter = fidl::fmt::NewFormatter(40, lib.Reporter());
  auto result = formatter.Format(lib.source_file(), flags);
  if (!result.has_value()) {
    lib.PrintReports();
    return reformat_and_compare ? "SECOND_PASS_PARSE_FAILED" : "PARSE_FAILED";
  }

  // Running the newly formatted output through the formatted another time tests that well-formatted
  // inputs are always left unchanged by the formatter.
  if (reformat_and_compare) {
    return Format(result.value(), false);
  }

  if (source != result.value()) {
    return "FORMAT_PASSES_NOT_EQUAL";
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

// Test with comments, doc comments, and attributes added and spaced out.
TEST(NewFormatterTests, AliasWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment

  /// doc comment

   @attr

    alias MyAlias_Abcdefghijklmnopqr = bool;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment

/// doc comment
@attr
alias MyAlias_Abcdefghijklmnopqr = bool;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, AliasMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(library foo.bar;alias MyAlias_Abcdefghijklmnopqr=bool;)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
alias MyAlias_Abcdefghijklmnopqr = bool;
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

// TODO(fxbug.dev/78236): more tests need to be added here once multiple arguments are supported for
//  attributes.

// Ensure that already properly formatted attributes declarations are not modified by another run
// through the formatter.
TEST(NewFormatterTests, AttributesFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
@attr_without_args
@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_without_args
@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, AttributesSingle) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
   @attr_with_one_arg("abcd")
library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_with_one_arg("abcd")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Attributes with arguments should overflow gracefully, while attributes without them should not.
TEST(NewFormatterTests, AttributesOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
@attr_without_args_abcdefghijklmnopqrstuv
@attr_with_one_arg("abcdefghijklmnopqrs")
library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_without_args_abcdefghijklmnopqrstuv
@attr_with_one_arg(
        "abcdefghijklmnopqrs")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}
TEST(NewFormatterTests, AttributesWithComment) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
 @attr_without_args

  // comment

   @attr_with_one_arg("abcdefghijklmnopqr")
    library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_without_args

// comment

@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, AttributesWithDocComment) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
    /// doc comment 1
    /// doc comment 2

   @attr_without_args @attr_with_one_arg("abcdefghijklmnopqr")

library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
/// doc comment 1
/// doc comment 2
@attr_without_args
@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, AttributesMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted =
      R"FIDL(@attr_without_args @attr_with_one_arg("abcdefghijklmnopqr")library foo.bar;)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_without_args
@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, AttributesMaximalNewLines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
@attr_without_args
@attr_with_one_arg
(
"abcdefghijklmnopqr"
)
library
foo
.
bar
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
@attr_without_args
@attr_with_one_arg("abcdefghijklmnopqr")
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Ensure that an already properly formatted bits declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, BitsFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghijklmnopqrs = bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghijklmnopqrs = bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, BitsOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghijklmnopqrst = bits {
    value1_abcdefghijklmnopqrstuvwxy = 0;
    value2_abcdefghijklmnopqrstuv = 0x01;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghijklmnopqrst
        = bits {
    value1_abcdefghijklmnopqrstuvwxy
            = 0;
    value2_abcdefghijklmnopqrstuv
            = 0x01;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, BitsUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghij= flexible bits {
 value1_abcdefghijklmnopqrstuvwx =0;
  value2_abcdefghijklmnopqrstu= 0x01;};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghij = flexible bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, BitsWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment 1
  /// doc comment 1

   @foo

    type MyBits_Abcdefghijklmnopqrs = bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
  // comment 2

   /// doc comment 2

    @bar
     value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment 1
/// doc comment 1
@foo
type MyBits_Abcdefghijklmnopqrs = bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
    // comment 2

    /// doc comment 2
    @bar
    value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// TODO(fxbug.dev/77861): multi-token blocks of text are currently not spaced properly, so
//  `=bits{` does not get split into `= bits {` properly.  This should be fixed when proper token
//  parsing is used.
TEST(NewFormatterTests, BitsMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted =
      R"FIDL(library foo.bar;type MyBits_Abcdefghijklmnopqrstu=bits{value1_abcdefghijklmnopqrstuvwx=0;value2_abcdefghijklmnopqrstu=0x01;};)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
type MyBits_Abcdefghijklmnopqrstu =bits{
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstu = 0x01;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, BitsMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library
foo
.
bar
;

type
MyBits_Abcdefghijklmnopqrs
=
bits
{
value1_abcdefghijklmnopqrstuvwx
=
0
;
value2_abcdefghijklmnopqrstu
=
0x01
;
}
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyBits_Abcdefghijklmnopqrs = bits {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstu = 0x01;
};
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
TEST(NewFormatterTests, ConstUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

const    MY_TRUE_ABCDEFGHIJKLM bool = true;
const MY_FALSE_ABCDEFGHIJK bool =    false;
const MY_UINT64_AB uint64 = 12345678900   ;


  const MY_FLOAT64_ABCDEF float64 = 12.34;
   const MY_STRING_ABCDEFGH
    string = "foo";
const MY_OR_A uint64 = 1
|   MY_UINT64_AB;
const MY_ORS_ABCDEFG uint64=1|2|3;
 const MY_REF_ABCD uint64 = MY_UINT64_AB
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

// Test with comments, doc comments, and attributes added and spaced out.
TEST(NewFormatterTests, ConstWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment

  /// doc comment

   @attr

    const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment

/// doc comment
@attr
const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, ConstMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted =
      R"FIDL(library foo.bar;const MY_TRUE_ABCDEFGHIJKLM bool=true;const MY_FALSE_ABCDEFGHIJK bool=false;const MY_UINT64_AB uint64=12345678900;const MY_FLOAT64_ABCDEF float64=12.34;const MY_STRING_ABCDEFGH string="foo";const MY_OR_A uint64=1|MY_UINT64_AB;const MY_ORS_ABCDEFG uint64=1|2|3;const MY_REF_ABCD uint64=MY_UINT64_AB;)FIDL";

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

// Ensure that an already properly formatted enum declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, EnumFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEnum_Abcdefghij = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEnum_Abcdefghij = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, EnumOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEnum_Abcdefghijk = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwxy = 0;
    value2_abcdefghijklmnopqrstuvwx = 01;

    @unknown
    value3_abcdefghijklmnopqrstuvw = 002;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEnum_Abcdefghijk
        = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwxy
            = 0;
    value2_abcdefghijklmnopqrstuvwx
            = 01;

    @unknown
    value3_abcdefghijklmnopqrstuvw
            = 002;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, EnumUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEnum_Abc= strict enum : uint32 {
 value1_abcdefghijklmnopqrstuvwx =0;
  value2_abcdefghijklmnopqrstuvw= 01;

     @unknown
      value3_abcdefghijklmnopqrstuv = 002 ;};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEnum_Abc = strict enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, EnumWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment 1
  /// doc comment 1

   @foo

    type MyEnum_Abcdefghij = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
  // comment 2

   /// doc comment 2

    @bar
     value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment 1
/// doc comment 1
@foo
type MyEnum_Abcdefghij = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
    // comment 2

    /// doc comment 2
    @bar
    value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// TODO(fxbug.dev/77861): multi-token blocks of text are currently not spaced properly, so
//  `=enum:uint32` does not get split into `= enum : uint32 {` properly.  This should be fixed when
//  proper token parsing is used.
TEST(NewFormatterTests, EnumMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted =
      R"FIDL(library foo.bar;type MyEnum_Abcdefghij=enum:uint32{value1_abcdefghijklmnopqrstuvwx=0;value2_abcdefghijklmnopqrstuvw=01;@unknown value3_abcdefghijklmnopqrstuv=002;};)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
type MyEnum_Abcdefghij =enum:uint32{
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstuvw = 01;
    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, EnumMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library
foo
.
bar
;

type
MyEnum_Abcdefghij
=
enum
:
uint32
{
value1_abcdefghijklmnopqrstuvwx
=
0
;
value2_abcdefghijklmnopqrstuvw
=
01
;

@unknown
value3_abcdefghijklmnopqrstuv
=
002
;
}
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEnum_Abcdefghij = enum : uint32 {
    value1_abcdefghijklmnopqrstuvwx = 0;
    value2_abcdefghijklmnopqrstuvw = 01;

    @unknown
    value3_abcdefghijklmnopqrstuv = 002;
};
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

// No overflow, but incorrect leading spacing and newlines.
TEST(NewFormatterTests, LibraryUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
  library

  foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test with comments, doc comments, and attributes added and spaced out.
TEST(NewFormatterTests, LibraryWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
 // comment

  /// doc comment

   @attr

    library foo.bar;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// comment

/// doc comment
@attr
library foo.bar;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, LibraryMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(library foo.bar;)FIDL";

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

// Ensure that an already properly formatted struct declaration is not modified by another run
// through the formatter.
TEST(NewFormatterTests, StructFormatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefgh = struct {};

type MyPopulatedStruct_Abcdef = struct {
    field1_abcdefghijklmnopqrstuvw bool;
    field2_abcdefghijklmno bool = false;

    field3_abcdefghijklmnopqrst struct {
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefgh = struct {};

type MyPopulatedStruct_Abcdef = struct {
    field1_abcdefghijklmnopqrstuvw bool;
    field2_abcdefghijklmno bool = false;

    field3_abcdefghijklmnopqrst struct {
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, StructOverflow) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefghi = struct {};
type MyPopulatedStruct_Abcdefg = struct {
    field1_abcdefghijklmnopqrstuvwx bool;
    field2_abcdefghijklmnop bool = false;

    field3_abcdefghijklmnopqrstu struct {
        nested1_abcdefg vector<uint8>:16;
        nested2_abcdef string = "abcdef";
    };
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefghi
        = struct {};
type MyPopulatedStruct_Abcdefg
        = struct {
    field1_abcdefghijklmnopqrstuvwx
            bool;
    field2_abcdefghijklmnop
            bool
            = false;

    field3_abcdefghijklmnopqrstu
            struct {
        nested1_abcdefg
                vector<uint8>:16;
        nested2_abcdef
                string
                = "abcdef";
    };
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, StructUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefgh = struct {
};

type MyStruct_Abcdef= resource struct {
 field1_abcdefghijklmnopqrstuvw bool;
      field2_abcdefghijklmno bool = false;

       field3_abcdefghijklmnopqrst struct {
 nested1_abcdef vector<  uint8>:16;
   nested2_abcdef string = "abcde";};


};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

type MyEmptyStruct_Abcdefgh = struct {};

type MyStruct_Abcdef = resource struct {
    field1_abcdefghijklmnopqrstuvw bool;
    field2_abcdefghijklmno bool = false;

    field3_abcdefghijklmnopqrst struct {
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Test with comments, doc comments, and attributes added.
TEST(NewFormatterTests, StructWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment 1
  /// doc comment 1

   @foo

    type MyEmptyStruct_Abcdefgh = struct {};

type MyPopulatedStruct_Abcdef = struct {
    field1_abcdefghijklmnopqrstuvw bool;

  // comment 2

   /// doc comment 2

     @bar

      field2_abcdefghijklmno bool = false;
    field3_abcdefghijklmnopqrst struct {
      /// doc comment 3
       @baz("qux")
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment 1
/// doc comment 1
@foo
type MyEmptyStruct_Abcdefgh = struct {};

type MyPopulatedStruct_Abcdef = struct {
    field1_abcdefghijklmnopqrstuvw bool;

    // comment 2

    /// doc comment 2
    @bar
    field2_abcdefghijklmno bool = false;
    field3_abcdefghijklmnopqrst struct {
        /// doc comment 3
        @baz("qux")
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// TODO(fxbug.dev/77861): multi-token blocks of text are currently not spaced properly, so
//  `=struct{` does not get split into `= struct {` properly.  This should be fixed when proper
//  token parsing is used.
TEST(NewFormatterTests, StructMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted =
      R"FIDL(library foo.bar;type MyEmptyStruct_Abcdefgh=struct{};type MyPopulatedStruct_Abcdef=struct{field1_abcdefghijklmnopqrstuvw bool;field2_abcdefghijklmno bool=false;field3_abcdefghijklmnopqrst struct{nested1_abcdef vector<uint8>:16;nested2_abcdef string="abcde";};};)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
type MyEmptyStruct_Abcdefgh = struct{};
type MyPopulatedStruct_Abcdef =struct{
    field1_abcdefghijklmnopqrstuvw bool;
    field2_abcdefghijklmno bool = false;
    field3_abcdefghijklmnopqrst struct{
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, StructMaximalNewlines) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library
foo
.
bar
;
type
MyEmptyStruct_Abcdefgh
=
struct
{
}
;
type
MyPopulatedStruct_Abcdef
=
struct
{
field1_abcdefghijklmnopqrstuvw
bool
;
field2_abcdefghijklmno
bool
=
false
;
field3_abcdefghijklmnopqrst
struct
{
nested1_abcdef
vector
<
uint8
>
:
16
;
nested2_abcdef
string
=
"abcde"
;
}
;
}
;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
type MyEmptyStruct_Abcdefgh = struct {};
type MyPopulatedStruct_Abcdef = struct {
    field1_abcdefghijklmnopqrstuvw bool;
    field2_abcdefghijklmno bool = false;
    field3_abcdefghijklmnopqrst struct {
        nested1_abcdef vector<uint8>:16;
        nested2_abcdef string = "abcde";
    };
};
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

TEST(NewFormatterTests, UsingUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

  using imported.
 abcdefhijklmnopqrstubwxy;
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

// Test with comments, doc comments, and attributes added and spaced out.
TEST(NewFormatterTests, UsingWithAllAnnotations) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

 // comment

  /// doc comment

   @attr

    using imported.abcdefhijklmnopqrstubwxy;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment

/// doc comment
@attr
using imported.abcdefhijklmnopqrstubwxy;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, UsingMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(library foo.bar;using imported.abcdefhijklmnopqrstubwxy;)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using imported.abcdefhijklmnopqrstubwxy;
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

TEST(NewFormatterTests, UsingWithAliasUnformatted) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

  using    baz.qux as
abcdefghijklmnopqrstuv;
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

TEST(NewFormatterTests, UsingWithAliasMinimalWhitespace) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(library foo.bar;using baz.qux as abcdefghijklmnopqrstuv;)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
using baz.qux as abcdefghijklmnopqrstuv;
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
// 0.1
/// 0.2
/// 0.3
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
// 6.1


// 7
/// 7.1
/// 7.2
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
// 0.1
/// 0.2
/// 0.3
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
// 6.1


// 7
/// 7.1
/// 7.2
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

TEST(NewFormatterTests, CommentsNormal) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
// C1
library foo.bar; // C2
// C3
using baz.qux; // C4
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// C1
library foo.bar; // C2
// C3
using baz.qux; // C4
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, CommentsMultiline) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
// C1
// C2
library foo.bar; // C3

// C4
// C5
using baz.qux; // C6
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// C1
// C2
library foo.bar; // C3

// C4
// C5
using baz.qux; // C6
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// Ensure that overlong comments are not wrapped.
TEST(NewFormatterTests, CommentsOverlong) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
// C1: This is my very very long comment.
library foo.bar; // C2
// C3: This is my very very long comment.
using baz.qux; // C4
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// C1: This is my very very long comment.
library foo.bar; // C2
// C3: This is my very very long comment.
using baz.qux; // C4
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, CommentsWeird) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
   // C1
     /// D1
/// D2
         /// D3
 @foo( // C2
     "abc"
  // C3
)
library foo.

// C4

        // C5

bar; @attr using // C6

baz;
using qux // C7
;

type // C8
MyStruct = struct

// C9

{ my_field // C10
bool;

// C11


}

   // C12




)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
// C1
/// D1
/// D2
/// D3
@foo( // C2
        "abc"
        // C3
        )
library foo.

        // C4

        // C5

        bar;
@attr
using // C6
        baz;
using qux // C7
        ;

type // C8
        MyStruct = struct

        // C9

        {
    my_field // C10
            bool;

// C11


}

// C12
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, NewlinesAbsent) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(library foo.bar;
// comment
using imported.abcdefhijklmnopqrstubwxy;
/// doc comment
alias MyAlias_Abcdefghijklmnopqr = bool;
@foo
@bar
const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;
// comment
using imported.abcdefhijklmnopqrstubwxy;
/// doc comment
alias MyAlias_Abcdefghijklmnopqr = bool;
@foo
@bar
const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

// For this test and the one below, new lines are generally expected to be retained.  An exception
// is made for doc comment and attribute blocks, which must never have newlines between the
// respective attributes, or between the last attribute and the declaration the block is describing.
TEST(NewFormatterTests, NewlinesSingle) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(
library foo.bar;

// comment

using imported.abcdefhijklmnopqrstubwxy;

/// doc comment

alias MyAlias_Abcdefghijklmnopqr = bool;

@foo

@bar

const MY_TRUE_ABCDEFGHIJKLM bool = true;

)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;

// comment

using imported.abcdefhijklmnopqrstubwxy;

/// doc comment
alias MyAlias_Abcdefghijklmnopqr = bool;

@foo
@bar
const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

TEST(NewFormatterTests, NewlinesDouble) {
  // ---------------40---------------- |
  std::string unformatted = R"FIDL(

library foo.bar;


// comment


using imported.abcdefhijklmnopqrstubwxy;


/// doc comment


alias MyAlias_Abcdefghijklmnopqr = bool;


@foo


@bar


const MY_TRUE_ABCDEFGHIJKLM bool = true;


)FIDL";

  // ---------------40---------------- |
  std::string formatted = R"FIDL(
library foo.bar;


// comment


using imported.abcdefhijklmnopqrstubwxy;


/// doc comment
alias MyAlias_Abcdefghijklmnopqr = bool;


@foo
@bar
const MY_TRUE_ABCDEFGHIJKLM bool = true;
)FIDL";

  ASSERT_STR_EQ(formatted, Format(unformatted));
}

}  // namespace
