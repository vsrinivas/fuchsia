// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/log_parser.h"

#include <sstream>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace symbolizer {

namespace {

class MockSymbolizer : public Symbolizer {
 public:
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(void, Module, (uint64_t id, std::string_view name, std::string_view build_id),
              (override));
  MOCK_METHOD(void, MMap,
              (uint64_t address, uint64_t size, uint64_t module_id, uint64_t module_offset),
              (override));
  MOCK_METHOD(void, Backtrace,
              (int frame_id, uint64_t address, AddressType type, std::string_view message),
              (override));
};

class LogParserTest : public ::testing::Test {
 public:
  LogParserTest() : printer_(output_), log_parser(input_, &printer_, &symbolizer_) {}

  void ProcessOneLine(const char* input) {
    input_ << input << std::endl;
    ASSERT_TRUE(log_parser.ProcessOneLine());
  }

 protected:
  std::stringstream input_;
  std::stringstream output_;
  Printer printer_;
  MockSymbolizer symbolizer_;
  LogParser log_parser;
};

TEST_F(LogParserTest, NoMarkup) {
  ProcessOneLine("normal content");
  ASSERT_EQ(output_.str(), "normal content\n");
  ProcessOneLine("{{{invalid_tag}}}");
  ASSERT_EQ(output_.str(), "normal content\n{{{invalid_tag}}}\n");
}

TEST_F(LogParserTest, ResetWithContext) {
  EXPECT_CALL(symbolizer_, Reset()).Times(1);
  ProcessOneLine("context: {{{reset}}}");
  ASSERT_EQ(output_.str(), "");
  printer_.OutputWithContext("");
  ASSERT_EQ(output_.str(), "context: \n");
}

TEST_F(LogParserTest, Module) {
  EXPECT_CALL(symbolizer_, Module(0, std::string_view("libc.so"), std::string_view("8ce60b")));
  ProcessOneLine("context1: {{{module:0x0:libc.so:elf:8ce60b}}}");
  EXPECT_CALL(symbolizer_, Module(5, std::string_view("libc.so"), std::string_view("8ce60b")));
  ProcessOneLine("context2: {{{module:0x5:libc.so:elf:8ce60b:unnecessary_content}}}");
  EXPECT_CALL(symbolizer_, Module(3, std::string_view(""), std::string_view("8ce60b")));
  ProcessOneLine("context3: {{{module:0x3::elf:8ce60b}}}");
  ASSERT_EQ(output_.str(), "");
  EXPECT_CALL(symbolizer_, Module).Times(0);
  ProcessOneLine("context4: {{{module:0x5:libc.so:not_elf:8ce60b}}}");
  ASSERT_EQ(output_.str(), "context4: {{{module:0x5:libc.so:not_elf:8ce60b}}}\n");
}

TEST_F(LogParserTest, MMap) {
  EXPECT_CALL(symbolizer_, MMap(0xbb57d35000, 0x2000, 0, 0));
  ProcessOneLine("{{{mmap:0xbb57d35000:0x2000:load:0:r:0}}}");
}

TEST_F(LogParserTest, Backtrace) {
  EXPECT_CALL(symbolizer_,
              Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kUnknown, std::string_view("")));
  ProcessOneLine("{{{bt:1:0xbb57d370b0}}}");
  EXPECT_CALL(symbolizer_, Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kUnknown,
                                     std::string_view("sp 0x3f540e65ef0")));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:sp 0x3f540e65ef0}}}");
  EXPECT_CALL(symbolizer_, Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kProgramCounter,
                                     std::string_view("")));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:pc}}}");
  EXPECT_CALL(symbolizer_, Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kProgramCounter,
                                     std::string_view("sp 0x3f540e65ef0")));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:pc:sp 0x3f540e65ef0}}}");
  ASSERT_EQ(output_.str(), "");
}

}  // namespace

}  // namespace symbolizer
