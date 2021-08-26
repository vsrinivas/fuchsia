// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include <fidl/diagnostics_json.h>

#include "error_test.h"
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

TEST(JsonDiagnosticsTests, BadError) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Table = table {
    1: nullable_string string:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_FALSE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/error",
    "message": "Table members cannot be nullable",
    "path": "example.fidl",
    "start_line": 5,
    "start_char": 7,
    "end_line": 5,
    "end_char": 22
  }
])JSON");
}

TEST(JsonDiagnosticsTests, WarnPassed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

@layort("Simple")
protocol Protocol {
    Method();
};
)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());
  const auto& diagnostics = library.diagnostics();

  ASSERT_JSON(diagnostics, R"JSON([
  {
    "category": "fidlc/warning",
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol P {};         // Error: name collision

type Table = table {
    1: s string;
};

type NewType = Table;  // Error: new type not allowed
)FIDL",
                      experimental_flags);
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
    "message": "newtypes not allowed: type declaration NewType defines a new type of the existing Table type, which is not yet supported",
    "path": "example.fidl",
    "start_line": 11,
    "start_char": 0,
    "end_line": 11,
    "end_char": 20
  }
])JSON");
}

TEST(JsonDiagnosticsTests, BadSpanIsEOF) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Table = table {
    1: foo string;
}
)FIDL",
                      experimental_flags);
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
