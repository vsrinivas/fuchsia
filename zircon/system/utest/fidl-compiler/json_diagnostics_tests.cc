// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include <fidl/diagnostics_json.h>

#include "test_library.h"
#include "unittest_helpers.h"

namespace fidl {

namespace {

using diagnostics::Diagnostic;

#define ASSERT_JSON(DIAGS, JSON) ASSERT_NO_FATAL_FAILURES(ExpectJson(DIAGS, JSON))

void ExpectJson(std::vector<Diagnostic*> diagnostics, std::string expected_json) {
  std::string actual_json = DiagnosticsJson(diagnostics).Produce().str();

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

TEST(JsonDiagnosticsTests, error) {
  TestLibrary library(R"FIDL(
library example;

table Table {
    1: string? nullable_string;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "message": "Table members cannot be nullable",
    "path": "example.fidl",
    "start_line": 5,
    "start_char": 4,
    "end_line": 5,
    "end_char": 30
  }
])JSON");
}

TEST(JsonDiagnosticsTests, warning) {
  TestLibrary library(R"FIDL(
library example;

[Layort = "Simple"]
protocol Protocol {
    Method();
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/warning",
    "message": "suspect attribute with name 'Layort'; did you mean 'Layout'?",
    "path": "example.fidl",
    "start_line": 4,
    "start_char": 1,
    "end_line": 4,
    "end_char": 18
  }
])JSON");
}

TEST(JsonDiagnosticsTests, multiple_errors) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol P {};      // Error: name collision

table Table {
    1: string? s;   // Error: nullable table member
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "message": "multiple declarations of 'P'; also declared at example.fidl:4:10",
    "path": "example.fidl",
    "start_line": 5,
    "start_char": 9,
    "end_line": 5,
    "end_char": 10
  },
  {
    "category": "fidlc/error",
    "message": "Table members cannot be nullable",
    "path": "example.fidl",
    "start_line": 8,
    "start_char": 4,
    "end_line": 8,
    "end_char": 16
  }
])JSON");
}

TEST(JsonDiagnosticsTests, span_is_eof) {
  TestLibrary library(R"FIDL(
library example;

table Table {
    1: string foo;
}
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
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
