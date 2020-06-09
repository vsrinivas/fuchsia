// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/system_log_recorder/system_log_recorder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/testing/stubs/logger.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/log_format.h"
#include "src/developer/feedback/feedback_data/system_log_recorder/reader.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace feedback {
namespace {

using stubs::BuildLogMessage;

const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line XX")).size();
const size_t kDroppedFormatStrSize = std::string("!!! DROPPED XX MESSAGES !!!\n").size();
const size_t kRepeatedFormatStrSize = std::string("!!! MESSAGE REPEATED XX MORE TIMES !!!\n").size();

TEST(LogMessageStoreTest, AddAndConsume) {
  // Set up the store to hold 2 log lines.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
)");
}

TEST(LogMessageStoreTest, DropsCorrectly) {
  // Set up the store to hold 2 log lines to test that the subsequent 3 are dropped.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 3 MESSAGES !!!
)");
}

TEST(LogMessageStoreTest, DropsSubsequentShorterMessages) {
  // Even though the store could hold 2 log lines, all the lines after the first one will be
  // dropped because the second log message is very long.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(
      FX_LOG_INFO, "This is a very big message that will not fit so it should not be displayed!")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 4 MESSAGES !!!
)");
}

TEST(LogMessageStoreTest, VerifyRepetitionMessage_AtConsume) {
  // Set up the store to hold 2 log line. With three repeated messages, the last two messages
  // should get reduced to a single repeated message.
  LogMessageStore store(kMaxLogLineSize);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");
}

TEST(LogMessageStoreTest, VerifyRepetitionMessage_WhenMessageChanges) {
  // Set up the store to hold 3 log line. Verify that a repetition message appears after input
  // repetition and before the input change.
  LogMessageStore store(kMaxLogLineSize * 2 + kRepeatedFormatStrSize);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
[15604.000][07559][07687][] INFO: line 1
)");
}

TEST(LogMessageStoreTest, VerifyDroppedRepeatedMessage_OnBufferFull) {
  // Set up the store to hold 1 log line. Verify that repeated messages that occur after the
  // buffer is full get dropped.
  LogMessageStore store(kMaxLogLineSize * 1);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 2 MESSAGES !!!
)");
}

TEST(LogMessageStoreTest, VerifyNoRepeatMessage_AfterFirstConsume) {
  // Set up the store to hold 1 log line. Verify that there is no repeat message right after
  // dropping messages.
  LogMessageStore store(kMaxLogLineSize * 1);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 1 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 1
)");
}

TEST(LogMessageStoreTest, VerifyRepeatMessage_AfterFirstConsume) {
  // Set up the store to hold 3 log lines. Verify that there can be a repeat message after
  // consume, when no messages were dropped.
  LogMessageStore store(kMaxLogLineSize * 3);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
[15604.000][07559][07687][] INFO: line 1
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(), R"(!!! MESSAGE REPEATED 1 MORE TIME !!!
)");
}

TEST(LogMessageStoreTest, VerifyRepeatedAndDropped) {
  // Set up the store to hold 2 log lines. Verify that we can have the repeated message, and then
  // the dropped message.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! DROPPED 1 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 1
)");
}

TEST(LogMessageStoreTest, VerifyNoRepeatMessage_TimeOrdering) {
  // Set up the store to hold 2 log line. Verify time ordering: a message cannot be counted as
  // repeated if it's in between messages, even if those messages get dropped.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1 overflow msg")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1 overflow msg")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
!!! DROPPED 5 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
)");
}

using ListenerTest = UnitTestFixture;

TEST_F(ListenerTest, AddsMessages) {
  const std::vector<std::vector<fuchsia::logger::LogMessage>> dumps({
      {
          BuildLogMessage(FX_LOG_INFO, "line 0"),
          BuildLogMessage(FX_LOG_INFO, "line 1"),
          BuildLogMessage(FX_LOG_INFO, "line 2"),
          BuildLogMessage(FX_LOG_INFO, "line 3"),

      },
      {
          BuildLogMessage(FX_LOG_INFO, "line 4"),
          BuildLogMessage(FX_LOG_INFO, "line 5"),
          BuildLogMessage(FX_LOG_INFO, "line 6"),
          BuildLogMessage(FX_LOG_INFO, "line 7"),
      },

  });

  const std::vector<fuchsia::logger::LogMessage> messages({
      BuildLogMessage(FX_LOG_INFO, "line 8"),
      BuildLogMessage(FX_LOG_INFO, "line 9"),
  });

  stubs::LoggerDelayedResponses logger(dispatcher(), dumps, messages, /*delay=*/zx::msec(5));
  InjectServiceProvider(&logger);

  // Set up the store to hold all of the added messages.
  LogMessageStore store(FileSize::Kilobytes(1).to_bytes());

  SystemLogListener listener(services(), &store);
  listener.StartListening();

  // Run the loop for as much time needed to ensure at the stub calls LogMany() and Log() as
  // specified in the constructor.
  RunLoopFor(logger.TotalDelayBetweenDumps() + logger.TotalDelayBetweenMessages());

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
[15604.000][07559][07687][] INFO: line 5
[15604.000][07559][07687][] INFO: line 6
[15604.000][07559][07687][] INFO: line 7
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");
}

std::vector<const std::string> LogFiles(files::ScopedTempDir& temp_dir) {
  const std::vector<const std::string> kLogFileNames = {
      "file0.txt",
      "file1.txt",
      "file2.txt",
      "file3.txt",
  };

  std::vector<const std::string> log_files;

  for (const auto& file : kLogFileNames) {
    log_files.push_back(files::JoinPath(temp_dir.path(), file));
  }

  return log_files;
}

TEST(WriterTest, VerifyFileRotation) {
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> log_files = LogFiles(temp_dir);

  // Set up the writer such that each file can fit 1 log message. We will then write 7 messages
  // and only expect the last 4 to remain as there are 4 files in the rotation.
  LogMessageStore store(kMaxLogLineSize);
  SystemLogWriter writer(log_files, FileSize::Bytes(log_files.size() * kMaxLogLineSize), &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 5")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 6")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 7")));
  writer.Write();

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  ASSERT_TRUE(Concatenate(log_files, output_path));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 4
[15604.000][07559][07687][] INFO: line 5
[15604.000][07559][07687][] INFO: line 6
[15604.000][07559][07687][] INFO: line 7
)");
}

TEST(WriterTest, WritesMessages) {
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> log_files = LogFiles(temp_dir);

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2);
  SystemLogWriter writer(
      log_files, FileSize::Bytes(log_files.size() * (kMaxLogLineSize * 2 + kDroppedFormatStrSize)),
      &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line A")));
  writer.Write();

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  writer.Write();

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  store.Add(BuildLogMessage(FX_LOG_INFO, "line 5"));
  writer.Write();

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  ASSERT_TRUE(Concatenate(log_files, output_path));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
!!! DROPPED 1 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 5
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 6")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 7")));
  writer.Write();

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 8")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 9")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 10")));
  writer.Write();

  ASSERT_TRUE(Concatenate(log_files, output_path));

  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
!!! DROPPED 1 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 5
[15604.000][07559][07687][] INFO: line 6
[15604.000][07559][07687][] INFO: line 7
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
!!! DROPPED 1 MESSAGES !!!
)");
}

using SystemLogRecorderTest = UnitTestFixture;

TEST_F(SystemLogRecorderTest, SingleThreaded_SmokeTest) {
  // To simulate a real load, we set up the test with the following conditions:
  //  * The listener will listener messages every 750 milliseconds.
  //  * The writer writes messages every 1 second. Each write will contain at most 2 log
  //    lines.
  //  * Each file will contain at most 2 log lines.
  //
  //    Using the above, we'll see log lines arrive with the at the following times:
  //    0.00: line0, line1, line2, line3
  //    0.75: line4, line5, line6, line7
  //    1.50: line8
  //    2.25: line9
  //    3.00: line10
  //    3.75: line11
  //    4.50: line12
  //    5.25: line13
  //    6.00: line14
  const zx::duration kListenerPeriod = zx::msec(750);
  const zx::duration kWriterPeriod = zx::sec(1);

  const std::vector<std::vector<fuchsia::logger::LogMessage>> dumps({
      {
          BuildLogMessage(FX_LOG_INFO, "line 0"),
          BuildLogMessage(FX_LOG_INFO, "line 1"),
          BuildLogMessage(FX_LOG_INFO, "line 2"),
          BuildLogMessage(FX_LOG_INFO, "line 3"),

      },
      {
          BuildLogMessage(FX_LOG_INFO, "line 4"),
          BuildLogMessage(FX_LOG_INFO, "line 5"),
          BuildLogMessage(FX_LOG_INFO, "line 6"),
          BuildLogMessage(FX_LOG_INFO, "line 7"),
      },

  });

  const std::vector<fuchsia::logger::LogMessage> messages({
      BuildLogMessage(FX_LOG_INFO, "line 8"),
      BuildLogMessage(FX_LOG_INFO, "line 9"),
      BuildLogMessage(FX_LOG_INFO, "line 10"),
      BuildLogMessage(FX_LOG_INFO, "line 11"),
      BuildLogMessage(FX_LOG_INFO, "line 12"),
      BuildLogMessage(FX_LOG_INFO, "line 13"),
      BuildLogMessage(FX_LOG_INFO, "line 14"),
  });

  stubs::LoggerDelayedResponses logger(dispatcher(), dumps, messages, kListenerPeriod);
  InjectServiceProvider(&logger);

  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> log_files = LogFiles(temp_dir);

  const size_t kWriteSize = kMaxLogLineSize * 2 + kDroppedFormatStrSize;

  SystemLogRecorder recorder(dispatcher(), services(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWriterPeriod,
                                 .max_write_size_bytes = kWriteSize,
                                 .log_file_paths = log_files,
                                 .total_log_size = FileSize::Bytes(log_files.size() * kWriteSize),
                             });
  recorder.Start();

  std::string contents;

  RunLoopFor(kWriterPeriod);

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
[15604.000][07559][07687][] INFO: line 12
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
[15604.000][07559][07687][] INFO: line 12
[15604.000][07559][07687][] INFO: line 13
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(log_files, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
[15604.000][07559][07687][] INFO: line 12
[15604.000][07559][07687][] INFO: line 13
[15604.000][07559][07687][] INFO: line 14
)");
}

}  // namespace
}  // namespace feedback
