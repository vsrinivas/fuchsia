// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "tools/symbolizer/log_parser.h"
#include "tools/symbolizer/symbolizer_impl.h"

#define TO_STRING_INTERNAL(var) #var
#define TO_STRING_LITERAL(var) TO_STRING_INTERNAL(var)

namespace {

const std::filesystem::path kSymbolsDir = TO_STRING_LITERAL(TEST_SYMBOLS_DIR);
const std::filesystem::path kTestCasesDir = TO_STRING_LITERAL(TEST_CASES_DIR);

class TestCase : public testing::Test {
 public:
  explicit TestCase(const std::string& name) : name_(name) {}
  void TestBody() override {
    symbolizer::CommandLineOptions options;
    options.build_id_dirs.push_back(kSymbolsDir);

    std::stringstream output;
    symbolizer::Printer printer(output);
    symbolizer::SymbolizerImpl symbolizer(&printer, options);

    std::ifstream input(kTestCasesDir / (name_ + ".in"));
    std::ifstream expected_output(kTestCasesDir / (name_ + ".out"));
    symbolizer::LogParser parser(input, &printer, &symbolizer);

    while (parser.ProcessOneLine()) {
      std::string got;
      while (std::getline(output, got)) {
        std::string expected;
        EXPECT_TRUE(std::getline(expected_output, expected));
        EXPECT_EQ(got, expected);
      }
      // Reset the output buffer.
      output.clear();
      output.str("");
    }
  }

 private:
  std::string name_;
};

void SetupTests() {
  for (const auto& entry : std::filesystem::directory_iterator(kTestCasesDir)) {
    if (entry.path().extension() == ".in") {
      std::string name = entry.path().stem();
      ::testing::RegisterTest("E2ETest", name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
                              [name]() { return new TestCase(name); });
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  SetupTests();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
