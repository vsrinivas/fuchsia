// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(RecoverableParsingTests, BadRecoverAtEndOfFileOld) {
  TestLibrary library(R"FIDL(
library example;

enum Enum {
    ONE;          // First error
};

bits Bits {
    CONSTANT = ;  // Second error
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedToken);
}

TEST(RecoverableParsingTests, BadRecoverAtEndOfFile) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Enum = enum {
    ONE;          // First error
};

type Bits = bits {
    CONSTANT = ;  // Second error
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedToken);
}

TEST(RecoverableParsingTests, BadRecoverAtEndOfDeclOld) {
  TestLibrary library(R"FIDL(
library example;

enum Enum {
    VARIANT = 0;
    MISSING_EQUALS 5;
};

union Union {
    1: string string_value;
    2 uint16 missing_colon;
};

struct Struct {
    string value;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverAtEndOfDecl) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Enum = enum {
    VARIANT = 0;
    MISSING_EQUALS 5;
};

type Union = union {
    1: string_value string;
    2 missing_colon uint16;
};

type Struct = struct {
    value string;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverAtEndOfMemberOld) {
  TestLibrary library(R"FIDL(
library example;

enum SettingType {
    UNKNOWN = 0;
    TIME_ZONE = 1;
    CONNECTIVITY 2;                    // Error: missing equals
};

union SettingData {
    1: string string_value;
    2 ConnectedState time_zone_value;  // Error: missing colon
    /// Unattached doc comment.        // erroneous doc comment is skipped during recovery
};

LoginOverride {                        // Error: missing keyword
    NONE = 0;
    AUTH.PROVIDER = 2,                 // Errors: '.' in identifier (2)
};

table AccountSettings {
    1: LoginOverride mo.de;            // Error: '.' in identifier
    3: OtherSetting setting;
};

struct TimeZoneInfo {
    TimeZone? current;
    vector<<TimeZone> available;       // Error: extra <
};

struct TimeZone {
    string id;
    string name;
    vector<string> region;
};
  )FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 7);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrMissingOrdinalBeforeType);
  ASSERT_ERR(errors[3], fidl::ErrExpectedDeclaration);
  ASSERT_ERR(errors[4], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[5], fidl::ErrExpectedOrdinalOrCloseBrace);
  ASSERT_ERR(errors[6], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverAtEndOfMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type SettingType = enum {
    UNKNOWN = 0;
    TIME_ZONE = 1;
    CONNECTIVITY 2;                    // Error: missing equals
};

type SettingData = union {
    1: string_value string;
    2 time_zone_value ConnectedState;  // Error: missing colon
    /// Unattached doc comment.        // erroneous doc comment is skipped during recovery
};

type LoginOverride = {                 // Error: missing keyword
    NONE = 0;
    AUTH.PROVIDER = 2,                 // Error: '.' in identifier
};

type AccountSettings = table {
    1: mo.de LoginOverride;            // Error: '.' in identifier
    3: setting OtherSetting;
};

type TimeZoneInfo = struct {
    current TimeZone:optional;
    available vector<<TimeZone>;       // Error: extra <
};

type TimeZone = struct {
    id string;
    name string;
    region vector<string>;
};
  )FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 6);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrMissingOrdinalBeforeType);
  // NOTE(fxbug.dev/72924): In the new syntax this is a parse error instead of
  // ErrExpectedDeclaration, which no longer applies in the new syntax.
  ASSERT_ERR(errors[3], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[4], fidl::ErrUnexpectedTokenOfKind);
  // NOTE(fxbug.dev/72924): The more specific ErrExpectedOrdinalOrCloseBrace
  // isn't returned in the new syntax. It doesn't seem all that useful anyway,
  // since we also get an ErrUnexpectedTokenOfKind
  ASSERT_ERR(errors[5], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadDoNotCompileAfterParsingFailsOld) {
  TestLibrary library(R"FIDL(
library example;

const uint8 compound.identifier = 0;  // Syntax error

struct NameCollision {};
struct NameCollision {};              // This name collision error will not be
                                      // reported, because if parsing fails
                                      // compilation is skipped
  )FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadDoNotCompileAfterParsingFails) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const compound.identifier uint8 = 0;  // Syntax error

type NameCollision = struct {};
type NameCollision = struct {};       // This name collision error will not be
                                      // reported, because if parsing fails
                                      // compilation is skipped
  )FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextBitsMemberOld) {
  TestLibrary library(R"FIDL(
library example;

bits Bits {
    ONE 0x1;      // First error
    TWO = 0x2;
    FOUR = 0x4    // Second error
    EIGHT = 0x8;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextBitsMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Bits = bits {
    ONE 0x1;      // First error
    TWO = 0x2;
    FOUR = 0x4    // Second error
    EIGHT = 0x8;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextEnumMemberOld) {
  TestLibrary library(R"FIDL(
library example;

enum Enum {
    ONE 1;      // First error
    TWO = 2;
    THREE = 3   // Second error
    FOUR = 4;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextEnumMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Enum = enum {
    ONE 1;      // First error
    TWO = 2;
    THREE = 3   // Second error
    FOUR = 4;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextProtocolMemberOld) {
  TestLibrary library(R"FIDL(
library example;

protocol P {
    compose A B;                              // 2 Errors (on 'B', ';')
    MethodWithoutSemicolon()
    ValidMethod();                            // Error (expecting ';')
    -> Event(TypeWithoutParamName);           // 2 Errors (on ')', ';')
    MissingParen request<Protocol> protocol); // Error
    -> Event(Type missing_paren;              // Error
    ValidMethod();
    Method() -> (uint16 num) error;           // Error
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 8);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnrecognizedProtocolMember);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[3], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[4], fidl::ErrUnexpectedToken);
  ASSERT_ERR(errors[5], fidl::ErrUnrecognizedProtocolMember);
  ASSERT_ERR(errors[6], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[7], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextProtocolMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {
    compose A B;                                 // 2 Errors (on 'B', ';')
    MethodWithoutSemicolon()
    ValidMethod();                               // Error (expecting ';')
    -> Event(struct { TypeWithoutParamName; });  // Error
    MissingParen server_end:Protocol protocol);  // Error
    -> Event(struct { missing_paren T };         // 2 Errors (on '}', ';')
    ValidMethod();
    Method() -> (struct { num uint16; }) error;  // Error
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  // NOTE(fxbug.dev/72924): the difference in errors is due to the change in
  // test input (for the TypeWithoutParams and MissingParen cases) rather than
  // any real behavior change
  ASSERT_EQ(errors.size(), 8);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnrecognizedProtocolMember);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[3], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[4], fidl::ErrUnrecognizedProtocolMember);
  ASSERT_ERR(errors[5], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[6], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[7], fidl::ErrUnexpectedTokenOfKind);
}

TEST(ParsingTests, BadRecoverableParamListParsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("example.fidl", R"FIDL(
library example;

protocol Example {
  Method(/// Doc comment
      { b bool; }) -> (/// Doc comment
      struct  { b bool; });
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrDocCommentOnParameters,
                                      fidl::ErrDocCommentOnParameters);
}

TEST(RecoverableParsingTests, BadRecoverToNextServiceMemberOld) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol Q {};
protocol R {};

service Service {
  P p extra_token; // First error
  Q q              // Second error
  R r;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextServiceMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol Q {};
protocol R {};

service Service {
  p P extra_token; // First error
  q Q              // Second error
  r R;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextStructMemberOld) {
  TestLibrary library(R"FIDL(
library example;

struct Struct {
    string string_value extra_token; // Error
    uint8 uint_value;
    vector<handle> vector_value      // Error
    int32 int_value;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextStructMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {
    string_value string extra_token; // Error
    uint_value uint8;
    vector_value vector<handle>      // Error
    int_value int32;
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextTableMemberOld) {
  TestLibrary library(R"FIDL(
library example;

table Table {
    1: string string_value              // Error
    2: uint8 uint_value;
    3: vector<handle> value_with space; // Error
    4: int32 int_value;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrExpectedOrdinalOrCloseBrace);
}

TEST(RecoverableParsingTests, BadRecoverToNextTableMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Table = table {
    1: string_value string              // Error
    2: uint_value uint8;
    3: value_with space vector<handle>; // Error
    4: int_value int32;
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  // NOTE(fxbug.dev/72924): the difference here is just due to the type/member
  // reordering, not a behavior change
  ASSERT_ERR(errors[2], fidl::ErrMissingOrdinalBeforeType);
}

TEST(RecoverableParsingTests, BadRecoverToNextUnionMemberOld) {
  TestLibrary library(R"FIDL(
library example;

union Union {
    1 string missing_colon;     // First error
    3: uint8 uint_value;
    4: string missing_semicolon // Second error
    5: int16 int_value;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, BadRecoverToNextUnionMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Union = union {
    1 missing_colon string;     // First error
    3: uint_value uint8;
    4: missing_semicolon string // Second error
    5: int_value int16;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

// TODO(fxbug.dev/70247): This only applies to the old syntax, since the new
// syntax uses ParseTypeConstructor for request/response types
TEST(RecoverableParsingTests, BadRecoverToNextParameterInList) {
  TestLibrary library(R"FIDL(
library example;

protocol Protocol {
    Method(uint8, uint16 n);
    Method(, string s);
    -> Event(Type, uint8 num, string compound.identifier);
    Method(uint8 num, uint16 num) -> (uint16 value, string value_with space);
    Method(Type param, request<<LocationLookup> frame) - (uint16 port);
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 8);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[3], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[4], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[5], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[6], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[7], fidl::ErrExpectedProtocolMember);
}

TEST(RecoverableParsingTests, BadRecoverFinalMemberMissingSemicolonOld) {
  TestLibrary library(R"FIDL(
library example;

struct Struct {
    uint8 uint_value;
    string foo // First error
};

// Recovered back to top-level parsing.
struct Good {};

extra_token // Second error
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

TEST(RecoverableParsingTests, BadRecoverFinalMemberMissingSemicolon) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {
    uint_value uint8;
    foo string // First error
};

// Recovered back to top-level parsing.
type Good = struct {};

extra_token // Second error
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

TEST(RecoverableParsingTests, BadRecoverFinalMemberMissingNameAndSemicolonOld) {
  TestLibrary library(R"FIDL(
library example;

struct Struct {
    uint8 uint_value;
    string }; // First error

// Does not recover back to top-level parsing. End the struct.
};

// Back to top-level parsing.
struct Good {};

extra_token // Second error
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

TEST(RecoverableParsingTests, BadRecoverFinalMemberMissingNameAndSemicolon) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {
    uint_value uint8;
    string_value }; // First error

// Does not recover back to top-level parsing. End the struct.
};

// Back to top-level parsing.
type Good = struct {};

extra_token // Second error
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

}  // namespace
