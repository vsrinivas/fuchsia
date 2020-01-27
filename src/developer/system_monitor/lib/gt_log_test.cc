// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/gt_log.h"

#include "gtest/gtest.h"

namespace gt {

namespace {

class GtLogTest : public ::testing::Test {
 public:
  void SetUp() override { SetUpLogging(/*argc=*/0, /*argv=*/nullptr); }
};

TEST_F(GtLogTest, Levels) {
  // This series of Logger instances add to the |test_stream|. These are equal
  // to unwrapping the GT_LOG(), except that GT_LOG() outputs to std::cout
  // rather than this test stream.
  GuiToolsLogLevel min_level = GuiToolsLogLevel::DEBUG3;
  std::ostringstream test_stream;
  {
    Logger logger(&test_stream, GuiToolsLogLevel::DEBUG, min_level,
                  /*file_path=*/"apple/banana.h", /*line=*/55,
                  /*function=*/"foo");
    logger.out() << "carrot";
    logger.out() << " dog";
  }
  EXPECT_EQ("[DEBUG]banana.h:55: foo: carrot dog\n", test_stream.str());
  {
    Logger logger(&test_stream, GuiToolsLogLevel::INFO, min_level,
                  /*file_path=*/"zebra/cow.h",
                  /*line=*/2134132412, /*function=*/"foo");
    logger.out() << "number is " << 5432;
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: foo: carrot dog\n"
      "[INFO]cow.h:2134132412: foo: number is 5432\n",
      test_stream.str());
  {
    Logger logger(&test_stream, GuiToolsLogLevel::WARNING, min_level,
                  /*file_path=*/"x.h",
                  /*line=*/0, /*function=*/"foo");
    logger.out() << 5432 << " was the number";
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: foo: carrot dog\n"
      "[INFO]cow.h:2134132412: foo: number is 5432\n"
      "[WARNING]x.h:0: foo: 5432 was the number\n",
      test_stream.str());
  {
    Logger logger(&test_stream, GuiToolsLogLevel::ERROR, min_level,
                  /*file_path=*/"e.cc",
                  /*line=*/3, /*function=*/"foo");
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: foo: carrot dog\n"
      "[INFO]cow.h:2134132412: foo: number is 5432\n"
      "[WARNING]x.h:0: foo: 5432 was the number\n"
      "[ERROR]e.cc:3: foo: \n",
      test_stream.str());
  {
    Logger logger(&test_stream, GuiToolsLogLevel::DEBUG3, min_level,
                  /*file_path=*/"x3.cc",
                  /*line=*/333, /*function=*/"bar");
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: foo: carrot dog\n"
      "[INFO]cow.h:2134132412: foo: number is 5432\n"
      "[WARNING]x.h:0: foo: 5432 was the number\n"
      "[ERROR]e.cc:3: foo: \n"
      "[DEBUG3]x3.cc:333: bar: \n",
      test_stream.str());
}

TEST_F(GtLogTest, BadInput) {
  // Try to trip up the logger with some bogus values.
  // Note: a nullptr for the file path will cause a segfault.
  GuiToolsLogLevel min_level = GuiToolsLogLevel::DEBUG;
  std::ostringstream test_stream;
  {
    Logger logger(&test_stream, static_cast<GuiToolsLogLevel>(3000), min_level,
                  /*file_path=*/"", /*line=*/-1, /*function=*/"foo");
    logger.out() << "carrot\n";
    logger.out() << " dog";
  }
  EXPECT_EQ("[UNKNOWN]:-1: foo: carrot\n dog\n", test_stream.str());
  {
    // The -4 log level is below DEBUG, so this line will not be logged.
    Logger logger(&test_stream, static_cast<GuiToolsLogLevel>(-4), min_level,
                  /*file_path=*/"hidden", /*line=*/-3, /*function=*/"foo");
  }
  EXPECT_EQ("[UNKNOWN]:-1: foo: carrot\n dog\n", test_stream.str());
}

TEST_F(GtLogTest, SetUpLogging) {
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
  {
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::INFO);
    const char* args[] = {"log_test", "foo", "bar"};
    EXPECT_TRUE(SetUpLogging(ARRAY_SIZE(args), args));
    // No log setting was changed.
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::INFO);
  }
  {
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::INFO);
    const char* args[] = {"log_test", "--verbose"};
    EXPECT_TRUE(SetUpLogging(ARRAY_SIZE(args), args));
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::DEBUG);
    gt::g_log_level = GuiToolsLogLevel::INFO;
  }
  {
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::INFO);
    // Values compound.
    const char* args[] = {"log_test", "--quiet", "--quiet"};
    EXPECT_TRUE(SetUpLogging(ARRAY_SIZE(args), args));
    EXPECT_EQ(gt::g_log_level, GuiToolsLogLevel::ERROR);
    gt::g_log_level = GuiToolsLogLevel::INFO;
  }
#undef ARRAY_SIZE
}

}  // namespace

}  // namespace gt
