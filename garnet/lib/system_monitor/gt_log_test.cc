// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/gt_log.h"

#include "gtest/gtest.h"

namespace gt {

namespace {

class GtLogTest : public ::testing::Test {
 public:
  void SetUp() { SetUpLogging(/*argc=*/0, /*argv=*/nullptr); }
};

TEST_F(GtLogTest, Levels) {
  // This series of Logger instances add to the |test_stream|. These are equal
  // to unwrapping the GT_LOG(), except that GT_LOG() outputs to std::cout
  // rather than this test stream.
  GuiToolsLogLevel min_level = DEBUG;
  std::ostringstream test_stream;
  {
    Logger logger(&test_stream, DEBUG, min_level, "apple/banana.h", 55);
    logger.out() << "carrot";
    logger.out() << " dog";
  }
  EXPECT_EQ("[DEBUG]banana.h:55: carrot dog\n", test_stream.str());
  {
    Logger logger(&test_stream, INFO, min_level, "zebra/cow.h", 2134132412);
    logger.out() << "number is " << 5432;
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: carrot dog\n"
      "[INFO]cow.h:2134132412: number is 5432\n",
      test_stream.str());
  {
    Logger logger(&test_stream, WARNING, min_level, "x.h", 0);
    logger.out() << 5432 << " was the number";
  }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: carrot dog\n"
      "[INFO]cow.h:2134132412: number is 5432\n"
      "[WARNING]x.h:0: 5432 was the number\n",
      test_stream.str());
  { Logger logger(&test_stream, ERROR, min_level, "e.cc", 3); }
  EXPECT_EQ(
      "[DEBUG]banana.h:55: carrot dog\n"
      "[INFO]cow.h:2134132412: number is 5432\n"
      "[WARNING]x.h:0: 5432 was the number\n"
      "[ERROR]e.cc:3: \n",
      test_stream.str());
}

TEST_F(GtLogTest, BadInput) {
  // Try to trip up the logger with some bogus values.
  // Note: a nullptr for the file path will cause a segfault.
  GuiToolsLogLevel min_level = DEBUG;
  std::ostringstream test_stream;
  {
    Logger logger(&test_stream, (GuiToolsLogLevel)3000, min_level, "", -1);
    logger.out() << "carrot\n";
    logger.out() << " dog";
  }
  EXPECT_EQ("[UNKNOWN]:-1: carrot\n dog\n", test_stream.str());
  {
    // The -4 log level is below DEBUG, so this line will not be logged.
    Logger logger(&test_stream, (GuiToolsLogLevel)-4, min_level, "hidden", -3);
  }
  EXPECT_EQ("[UNKNOWN]:-1: carrot\n dog\n", test_stream.str());
}

}  // namespace

}  // namespace gt
