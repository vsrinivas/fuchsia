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
  MOCK_METHOD(void, Reset, (bool symbolizing_dart), (override));
  MOCK_METHOD(void, Module, (uint64_t id, std::string_view name, std::string_view build_id),
              (override));
  MOCK_METHOD(void, MMap,
              (uint64_t address, uint64_t size, uint64_t module_id, std::string_view flags,
               uint64_t module_offset),
              (override));
  MOCK_METHOD(void, Backtrace,
              (uint64_t frame_id, uint64_t address, AddressType type, std::string_view message),
              (override));
  MOCK_METHOD(void, DumpFile, (std::string_view type, std::string_view name), (override));
};

class LogParserTest : public ::testing::Test {
 public:
  LogParserTest() : printer_(output_), log_parser(input_, &printer_, &symbolizer_) {}

  void ProcessOneLine(const char* input) {
    input_ << input << std::endl;
    ASSERT_TRUE(log_parser.ProcessNextLine());
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
  EXPECT_CALL(symbolizer_, Reset(false)).Times(1);
  ProcessOneLine("context: {{{reset}}}");
  ASSERT_EQ(output_.str(), "");
  printer_.OutputWithContext("");
  ASSERT_EQ(output_.str(), "context: \n");
}

TEST_F(LogParserTest, Module) {
  EXPECT_CALL(symbolizer_, Module(0, "libc.so", "8ce60b"));
  ProcessOneLine("context1: {{{module:0x0:libc.so:elf:8ce60b}}}");
  EXPECT_CALL(symbolizer_, Module(5, "libc.so", "8ce60b"));
  ProcessOneLine("context2: {{{module:0x5:libc.so:elf:8ce60b:unnecessary_content}}}");
  EXPECT_CALL(symbolizer_, Module(3, "", "8ce60b"));
  ProcessOneLine("context3: {{{module:0x3::elf:8ce60b}}}");
  ASSERT_EQ(output_.str(), "");
  EXPECT_CALL(symbolizer_, Module).Times(0);
  ProcessOneLine("context4: {{{module:0x5:libc.so:not_elf:8ce60b}}}");
  ASSERT_EQ(output_.str(), "context4: {{{module:0x5:libc.so:not_elf:8ce60b}}}\n");
}

TEST_F(LogParserTest, MMap) {
  EXPECT_CALL(symbolizer_, MMap(0xbb57d35000, 0x2000, 0, "r", 0));
  ProcessOneLine("{{{mmap:0xbb57d35000:0x2000:load:0:r:0}}}");
}

TEST_F(LogParserTest, Backtrace) {
  EXPECT_CALL(symbolizer_, Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kUnknown, ""));
  ProcessOneLine("{{{bt:1:0xbb57d370b0}}}");
  EXPECT_CALL(symbolizer_,
              Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kUnknown, "sp 0x3f540e65ef0"));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:sp 0x3f540e65ef0}}}");
  EXPECT_CALL(symbolizer_,
              Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kProgramCounter, ""));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:pc}}}");
  EXPECT_CALL(symbolizer_, Backtrace(1, 0xbb57d370b0, Symbolizer::AddressType::kProgramCounter,
                                     "sp 0x3f540e65ef0"));
  ProcessOneLine("{{{bt:1:0xbb57d370b0:pc:sp 0x3f540e65ef0}}}");
  ASSERT_EQ(output_.str(), "");
}

TEST_F(LogParserTest, DumpFile) {
  EXPECT_CALL(symbolizer_, DumpFile("type", "name"));
  ProcessOneLine("{{{dumpfile:type:name}}}");
  ASSERT_EQ(output_.str(), "");
}

TEST_F(LogParserTest, Dart) {
  {
    EXPECT_CALL(symbolizer_, Reset(true));
    ProcessOneLine("*** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***");
    EXPECT_FALSE(output_.str().empty());
    output_.clear();
    ProcessOneLine("pid: 12, tid: 30221, name some.ui");
    EXPECT_FALSE(output_.str().empty());
    output_.clear();
  }
  {
    EXPECT_CALL(symbolizer_, Module(0, "some.ui", "0123456789abcdef"));
    ProcessOneLine("build_id: '0123456789abcdef'");
    EXPECT_FALSE(output_.str().empty());
    output_.clear();
  }
  {
    EXPECT_CALL(symbolizer_, MMap(0xf2e4c8000, 0x800000000, 0, "", 0));
    ProcessOneLine("isolate_dso_base: f2e4c8000, vm_dso_base: f2e4c8000");
    EXPECT_FALSE(output_.str().empty());
    output_.clear();
  }
  ProcessOneLine("isolate_instructions: f2f9f8e60, vm_instructions: f2f9f4000");
  EXPECT_FALSE(output_.str().empty());
  output_.clear();
  {
    EXPECT_CALL(symbolizer_,
                Backtrace(0, 0x0000000f2fbb51c7, Symbolizer::AddressType::kUnknown, ""));
    ProcessOneLine(
        "#00 abs 0000000f2fbb51c7 virt 00000000016ed1c7 "
        "_kDartIsolateSnapshotInstructions+0x1bc367");
    EXPECT_FALSE(output_.str().empty());
    output_.clear();
  }
}

}  // namespace

}  // namespace symbolizer
