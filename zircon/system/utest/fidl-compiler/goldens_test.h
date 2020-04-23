// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_GOLDENS_TEST_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_GOLDENS_TEST_H_

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

enum class Generator {
  kJson,
  kTables,
};

enum class CheckResult {
  kSuccess,
  kCompileError,
  kMismatch,
};

CheckResult checkGenerator(Generator generator,
                           const std::vector<std::pair<std::string, std::string>>& fidl_files,
                           const std::string& expected_golden) {
  SharedAmongstLibraries shared;
  TestLibrary prev_library;
  for (uint32_t i = 0; i < fidl_files.size(); i++) {
    const auto& [filename, file_contents] = fidl_files[i];
    fidl::ExperimentalFlags experimental_flags;
    experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);
    TestLibrary lib(filename, file_contents, &shared, std::move(experimental_flags));
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

  std::string actual;
  switch (generator) {
    case Generator::kJson:
      actual = prev_library.GenerateJSON();
      break;
    case Generator::kTables:
      actual = prev_library.GenerateTables();
      break;
  }
  trim(actual);

  if (actual == expected_golden) {
    return CheckResult::kSuccess;
  }

  // On error, we output both the actual and expected to allow simple
  // diffing to debug the test.
  std::string actual_filename;
  std::string expected_filename;
  switch (generator) {
    case Generator::kJson:
      actual_filename.append("json");
      expected_filename.append("json");
      break;
    case Generator::kTables:
      actual_filename.append("tables");
      expected_filename.append("tables");
      break;
  }

  actual_filename.append("_generator_tests_actual.txt");
  std::ofstream output_actual(actual_filename);
  output_actual << actual;
  output_actual.close();

  expected_filename.append("_generator_tests_expected.txt");
  std::ofstream output_expected(expected_filename);
  output_expected << expected_golden;
  output_expected.close();

  return CheckResult::kMismatch;
}

struct TestResult {
  uint32_t num_goldens;
  bool failed;
};

TestResult check_goldens(Generator generator) {
  auto test_result = TestResult { .num_goldens = 0, .failed = false };
  std::cout << std::endl;

  auto goldens = [generator]() {
    switch (generator) {
      case Generator::kJson:
        return Goldens::json();
      case Generator::kTables:
        return Goldens::tables();
    }
  };

  for (const auto& element : goldens()) {
    std::string testname = element.first;
    std::string golden = element.second;
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
    trim(golden);
    test_result.num_goldens++;
    CheckResult result;
    for (int i = 0; i < kRepeatTestCount; i++) {
      result = checkGenerator(generator, fidl_files, golden);
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
        test_result.failed = true;
        std::cout << "...failed to compile" << std::endl;
        break;
      case CheckResult::kMismatch:
        test_result.failed = true;
        if (generator == Generator::kJson) {
          std::cout << "...JSON does not match goldens" << std::endl;
        } else {
          std::cout << "...tables do not match goldens" << std::endl;
        }
        break;
    }
  }

  return test_result;
}

}  // namespace

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_GOLDENS_TEST_H_
