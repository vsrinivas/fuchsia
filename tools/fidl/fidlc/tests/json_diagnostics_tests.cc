// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <string_view>
#include <utility>

#include "tools/fidl/fidlc/include/fidl/diagnostics_json.h"
#include "tools/fidl/fidlc/tests/test_library.h"
#include "tools/fidl/fidlc/tests/unittest_helpers.h"

namespace fidl {

namespace {

#define ASSERT_JSON(DIAGS, JSON) ASSERT_NO_FAILURES(ExpectJson(DIAGS, JSON))

void ExpectJson(std::vector<Diagnostic*> diagnostics, std::string_view expected_json) {
  std::string actual_json = DiagnosticsJson(std::move(diagnostics)).Produce().str();

  if (expected_json != actual_json) {
    std::ofstream output_actual("json_diagnostics_tests_actual.txt");
    output_actual << actual_json;
    output_actual.close();

    std::ofstream output_expected("json_diagnostics_tests_expected.txt");
    output_expected << expected_json;
    output_expected.close();
  }

  EXPECT_STRING_EQ(
      expected_json, actual_json,
      "To compare results, run:\n\n diff ./json_diagnostics_tests_{expected,actual}.txt\n");
}

TEST(JsonDiagnosticsTests, BadError) {
  TestLibrary library(R"FIDL(
library example;

type Table = table {
    1: nullable_string string:optional;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.Diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "error_id": "fi-0048",
    "message": "Table members cannot be optional",
    "path": "example.fidl",
    "start_line": 5,
    "start_char": 7,
    "end_line": 5,
    "end_char": 22
  }
])JSON");
}

TEST(JsonDiagnosticsTests, WarnPassed) {
  TestLibrary library(R"FIDL(
library example;

@layort("Simple")
protocol Protocol {
    Method();
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  const auto& diagnostics = library.Diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/warning",
    "error_id": "fi-0145",
    "message": "suspect attribute with name 'layort'; did you mean 'layout'?",
    "path": "example.fidl",
    "start_line": 4,
    "start_char": 0,
    "end_line": 4,
    "end_char": 17
  }
])JSON");
}

TEST(JsonDiagnosticsTests, BadMultipleErrors) {
  TestLibrary library(R"FIDL(
library example;

type Foo = enum : string { // Error: enums may only be of integral primitive type
    A = 1;
};

type Bar = table {
    2: x uint32; // Error: missing ordinal 1 (ordinals must be dense)
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.Diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "error_id": "fi-0070",
    "message": "enums may only be of integral primitive type, found string",
    "path": "example.fidl",
    "start_line": 4,
    "start_char": 5,
    "end_line": 4,
    "end_char": 8
  },
  {
    "category": "fidlc/error",
    "error_id": "fi-0100",
    "message": "missing ordinal 1 (ordinals must be dense); consider marking it reserved",
    "path": "example.fidl",
    "start_line": 9,
    "start_char": 4,
    "end_line": 9,
    "end_char": 6
  }
])JSON");
}

TEST(JsonDiagnosticsTests, BadSpanIsEOF) {
  TestLibrary library(R"FIDL(
library example;

type Table = table {
    1: foo string;
}
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.Diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "error_id": "fi-0008",
    "message": "unexpected token EndOfFile, was expecting Semicolon",
    "path": "example.fidl",
    "start_line": 7,
    "start_char": 0,
    "end_line": 7,
    "end_char": 0
  }
])JSON");
}

}  // namespace

}  // namespace fidl
