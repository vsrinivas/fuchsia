// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "fidl/errors.h"
#include "error_test.h"
#include "test_library.h"

namespace {

using fidl::BaseError;
using fidl::ErrorDef;
using fidl::ErrorReporter;

bool recover_at_end_of_file() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum Enum {
    ONE;          // First error
    TWO = 2;
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

  END_TEST;
}

bool recover_at_end_of_decl() {
  BEGIN_TEST;

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

  END_TEST;
}

// The current state of recoverable parsing only allows for recovery to the next
// decl (not to the next member within the same decl).
// This is not the desired long-term behavior, but this test will help track
// changes to parser recovery as it is extended.
bool do_not_recover_within_decl() {
  BEGIN_TEST;

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
    /// Unattached doc comment.        // This is not reported yet as we skip to
                                       // the next decl
};

LoginOverride {                        // Error: missing keyword
    NONE = 0;
    AUTH.PROVIDER = 2,                 // This is not reported yet
};

table AccountSettings {
    1: LoginOverride mo.de;            // Error: '.' in identifier
    2: OtherSetting setting,           // This is not reported yet
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
  ASSERT_EQ(errors.size(), 5);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrExpectedDeclaration);
  ASSERT_ERR(errors[3], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[4], fidl::ErrUnexpectedTokenOfKind);

  END_TEST;
}


}  // namespace

BEGIN_TEST_CASE(recoverable_parsing_tests)
RUN_TEST(recover_at_end_of_file)
RUN_TEST(recover_at_end_of_decl)
RUN_TEST(do_not_recover_within_decl)
END_TEST_CASE(recoverable_parsing_tests)
