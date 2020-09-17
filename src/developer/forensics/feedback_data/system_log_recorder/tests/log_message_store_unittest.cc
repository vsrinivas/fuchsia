// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"
#include "src/developer/forensics/testing/stubs/logger.h"
#include "src/developer/forensics/utils/log_format.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

using stubs::BuildLogMessage;

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line X")).size();
const size_t kRepeatedFormatStrSize = std::string("!!! MESSAGE REPEATED X MORE TIMES !!!\n").size();
// We set the block size to an arbitrary large numbers for test cases where the block logic does
// not matter.
const size_t kVeryLargeBlockSize = kMaxLogLineSize * 100;

std::unique_ptr<Encoder> MakeIdentityEncoder() {
  return std::unique_ptr<Encoder>(new IdentityEncoder());
}

TEST(LogMessageStoreTest, VerifyBlock) {
  // Set the block to hold 2 log messages while the buffer holds 1 log message.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize, MakeIdentityEncoder());
  bool end_of_block;

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_TRUE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 2
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 3
)");
  EXPECT_TRUE(end_of_block);
}

TEST(LogMessageStoreTest, AddAndConsume) {
  // Set up the store to hold 2 log lines.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2, MakeIdentityEncoder());
  bool end_of_block;

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, DropsCorrectly) {
  bool end_of_block;
  // Set up the store to hold 2 log lines to test that the subsequent 3 are dropped.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 3 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, DropsSubsequentShorterMessages) {
  bool end_of_block;
  // Even though the store could hold 2 log lines, all the lines after the first one will be
  // dropped because the second log message is very long.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(
      FX_LOG_INFO, "This is a very big message that will not fit so it should not be displayed!")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 4 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepetitionMessage_AtConsume) {
  bool end_of_block;
  // Set up the store to hold 2 log line. With three repeated messages, the last two messages
  // should get reduced to a single repeated message.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepetition_DoNotResetRepeatedWarningOnConsume) {
  bool end_of_block;
  // Test that we only write repeated warning messages when repeated messages span over 2 buffers.
  // Block capacity: very large (unlimited for this example)
  // Buffer capacity: 1 log message
  //
  // __________________
  // |input   |output |
  // |________|_______| _
  // |line 0  |line 0 |  |
  // |line 0  |x2     |  |---- Consume 1
  // |line 0  |       |  |
  // |________|_______| _|
  // |line 0  |x2     |  |
  // |line 0  |       |  |---- Consume 2
  // |________|_______| _|
  //
  // Note: xN = last message repeated N times
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"(!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");

  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepetition_ResetRepeatedWarningOnConsume) {
  bool end_of_block;
  // Test that the first log of a block should not be a repeated warning message.
  // Block capacity: 1 log message
  // Buffer capacity: 1 log message
  //
  // __________________
  // |input   |output |
  // |________|_______| _
  // |line 0  |line 0 |  |
  // |line 0  |x2     |  |---- Consume 1
  // |line 0  |       |  |
  // |________|_______| _|
  // |  End of Block  |
  // |----------------| _
  // |line 0  |line 0 |  |
  // |line 0  |x1     |  |---- Consume 2
  // |________|_______| _|
  // |  End of Block  |
  // -----------------
  // Note: xN = last message repeated N times
  LogMessageStore store(kMaxLogLineSize, kMaxLogLineSize, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");

  EXPECT_TRUE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  EXPECT_TRUE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepetitionMessage_WhenMessageChanges) {
  bool end_of_block;
  // Set up the store to hold 3 log line. Verify that a repetition message appears after input
  // repetition and before the input change.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2 + kRepeatedFormatStrSize,
                        MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
[15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyDroppedRepeatedMessage_OnBufferFull) {
  bool end_of_block;
  // Set up the store to hold 1 log line. Verify that repeated messages that occur after the
  // buffer is full get dropped.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 1, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 2 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyNoRepeatMessage_AfterFirstConsume) {
  bool end_of_block;
  // Set up the store to hold 1 log line. Verify that there is no repeat message right after
  // dropping messages.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 1, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 1 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepeatMessage_AfterFirstConsume) {
  bool end_of_block;
  // Set up the store to hold 3 log lines. Verify that there can be a repeat message after
  // consume, when no messages were dropped.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 3, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
[15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(&end_of_block), R"(!!! MESSAGE REPEATED 1 MORE TIME !!!
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyRepeatedAndDropped) {
  bool end_of_block;
  // Set up the store to hold 2 log lines. Verify that we can have the repeated message, and then
  // the dropped message.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! DROPPED 1 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 1
)");
  EXPECT_FALSE(end_of_block);
}

TEST(LogMessageStoreTest, VerifyNoRepeatMessage_TimeOrdering) {
  bool end_of_block;
  // Set up the store to hold 2 log line. Verify time ordering: a message cannot be counted as
  // repeated if it's in between messages, even if those messages get dropped.
  LogMessageStore store(kVeryLargeBlockSize, kMaxLogLineSize * 2, MakeIdentityEncoder());

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1 overflow msg")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1 overflow msg")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 5 MESSAGES !!!
)");
  EXPECT_FALSE(end_of_block);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
)");
  EXPECT_FALSE(end_of_block);
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
