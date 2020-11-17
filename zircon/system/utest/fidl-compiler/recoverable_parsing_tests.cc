// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(RecoverableParsingTests, recover_at_end_of_file) {
  TestLibrary library(R"FIDL(
library example;

enum Enum {
    ONE;          // First error
};

bits Bits {
    CONSTANT = ;  // Second error
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedToken);
}

TEST(RecoverableParsingTests, recover_at_end_of_decl) {
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
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_at_end_of_member) {
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
    /// Unattached doc comment.        // Error: doc comment must be attached
};

LoginOverride {                        // Error: missing keyword
    NONE = 0;
    AUTH.PROVIDER = 2,                 // Error: '.' in identifier
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

TEST(RecoverableParsingTests, do_not_compile_after_parsing_fails) {
  TestLibrary library(R"FIDL(
library example;

const uint8 compound.identifier = 0;  // Syntax error

struct NameCollision {};
struct NameCollision {};              // This name collision error will not be
                                      // reported, because if parsing fails
                                      // compilation is skipped
  )FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_to_next_bits_member) {
  TestLibrary library(R"FIDL(
library example;

bits Bits {
    ONE 0x1;      // First error
    TWO = 0x2;
    FOUR = 0x4    // Second error
    EIGHT = 0x8;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_to_next_enum_member) {
  TestLibrary library(R"FIDL(
library example;

enum Enum {
    ONE 1;      // First error
    TWO = 2;
    THREE = 3   // Second error
    FOUR = 4;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_to_next_protocol_member) {
  TestLibrary library(R"FIDL(
library example;

protocol P {
    compose A B;                              // Error
    MethodWithoutSemicolon()                  // Error
    ValidMethod();
    -> Event(TypeWithoutParamName);           // Error
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

TEST(RecoverableParsingTests, recover_to_next_service_member) {
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
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_to_next_struct_member) {
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

TEST(RecoverableParsingTests, recover_to_next_table_member) {
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

TEST(RecoverableParsingTests, recover_to_next_union_member) {
  TestLibrary library(R"FIDL(
library example;

union Union {
    1 string missing_colon;     // First error
    3: uint8 uint_value;
    4: string missing_semicolon // Second error
    5: int16 int_value;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
}

TEST(RecoverableParsingTests, recover_to_next_parameter_in_list) {
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

TEST(RecoverableParsingTests, recover_final_member_missing_semicolon) {
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
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrExpectedDeclaration);
}

TEST(RecoverableParsingTests, recover_final_member_missing_name_and_semicolon) {
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
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrExpectedDeclaration);
}

}  // namespace
