// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <fstream>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "goldens.h"
#include "test_library.h"

namespace fs = std::filesystem;

namespace {

// We repeat each test in a loop in order to catch situations where memory layout
// determines what JSON is produced (this is often manifested due to using a std::map<Foo*,...>
// in compiler source code).
constexpr int kRepeatTestCount = 100;

// These line lengths reflect the max length of the two columns in the test output.
// kGoldenColumnLength is the max size of the left column and kResultColumnLength is the max size
// of the right column in the example test output:
//
// checking golden for foo             ...success
// checking golden for barbazquz       ...success
// checking golden for bar       ...compile error
constexpr int kGoldenColumnLength = 70;
constexpr int kResultColumnLength = 30;

void trim(std::string& s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch) && ch != '\n'; }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch) && ch != '\n'; })
              .base(),
          s.end());
}

enum class CheckResult {
  kSuccess,
  kCompileError,
  kJsonMismatch,
};

CheckResult checkJSONGenerator(const std::vector<std::pair<std::string, std::string>>& fidl_files,
                               const std::string& expected_json) {
  SharedAmongstLibraries shared;
  TestLibrary prev_library;
  for (uint32_t i = 0; i < fidl_files.size(); i++) {
    const auto& [filename, file_contents] = fidl_files[i];
    TestLibrary lib(filename, file_contents, &shared);
    // all fidl files (other than the first) depends on the fidl file that
    // comes directly before it
    if (i != 0) {
      lib.AddDependentLibrary(std::move(prev_library));
    }
    if (!lib.Compile()) {
      return CheckResult::kCompileError;
    }
    prev_library = std::move(lib);
  }

  auto actual = prev_library.GenerateJSON();
  trim(actual);

  if (actual == expected_json) {
    return CheckResult::kSuccess;
  }

  // On error, we output both the actual and expected to allow simple
  // diffing to debug the test.

  std::ofstream output_actual("json_generator_tests_actual.txt");
  output_actual << actual;
  output_actual.close();

  std::ofstream output_expected("json_generator_tests_expected.txt");
  output_expected << expected_json;
  output_expected.close();

  return CheckResult::kJsonMismatch;
}

bool check_goldens() {
  BEGIN_TEST;

  uint32_t num_goldens = 0;
  bool test_failed = false;
  std::cout << std::endl;
  for (const auto& element : Goldens::json()) {
    std::string testname = element.first;
    std::string json_golden = element.second;
    auto dep_order = Goldens::getDepOrder(testname);
    std::vector<std::pair<std::string, std::string>> fidl_files;
    fidl_files.reserve(dep_order.size());
    for (const auto& filename : dep_order) {
      fidl_files.emplace_back(fs::path(filename).filename(), Goldens::getFileContents(filename));
    }

    std::cout << std::left
              << std::setw(kGoldenColumnLength)
              // need to concat these strings before printing so that column
              // length is correct
              << ("checking golden for: " + testname);
    trim(json_golden);
    num_goldens++;
    CheckResult result;
    for (int i = 0; i < kRepeatTestCount; i++) {
      result = checkJSONGenerator(fidl_files, json_golden);
      if (result != CheckResult::kSuccess) {
        break;
      }
    }

    std::cout << std::right << std::setw(kResultColumnLength);
    switch (result) {
      case CheckResult::kSuccess:
        std::cout << "...success" << std::endl;
        break;
      case CheckResult::kCompileError:
        test_failed = true;
        std::cout << "...failed to compile" << std::endl;
        break;
      case CheckResult::kJsonMismatch:
        test_failed = true;
        std::cout << "...JSON does not match goldens" << std::endl;
        break;
    }
  }

  // Add a sanity check that we have checked at least some number of goldens
  // so that the test doesn't silently pass if the goldens have moved and this
  // test doesn't find/test them
  ASSERT_GE(num_goldens, 10);
  ASSERT_FALSE(test_failed);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(json_generator_tests)
RUN_TEST(check_goldens)
END_TEST_CASE(json_generator_tests)
