// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder/system_log_recorder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/testing/stubs/logger.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/log_format.h"
#include "src/developer/feedback/utils/rotating_file_set.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using stubs::BuildLogMessage;

const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line XX")).size();
const size_t kDroppedFormatStrSize = std::string("!!! DROPPED %lu LOG MESSAGES !!!\n").size();

TEST(LogMessageStoreTest, AddAndConsume) {
  // Set up the store to hold 2 log lines.
  LogMessageStore store(kMaxLogLineSize * 2);

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 0"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 1"))));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
)");

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 2"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 3"))));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
)");
}

TEST(LogMessageStoreTest, DropsCorrectly) {
  // Set up the store to hold 2 log lines and the "!!! DROPPED..." string.
  LogMessageStore store(kMaxLogLineSize * 2 + kDroppedFormatStrSize);

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 0"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 1"))));
  EXPECT_FALSE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 2"))));
  EXPECT_FALSE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 3"))));
  EXPECT_FALSE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 4"))));

  EXPECT_EQ(store.Consume(), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 3 LOG MESSAGES !!!
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

TEST(WriterTest, WritesMessages) {
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> log_files = LogFiles(temp_dir);

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2);
  SystemLogWriter writer(
      log_files, FileSize::Bytes(log_files.size() * (kMaxLogLineSize * 2 + kDroppedFormatStrSize)),
      &store);

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line A"))));
  writer.Write();

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 0"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 1"))));
  writer.Write();

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 2"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 3"))));
  EXPECT_FALSE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 4"))));
  writer.Write();

  store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 5")));
  writer.Write();

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");
  RotatingFileSetReader reader(log_files);

  ASSERT_TRUE(reader.Concatenate(output_path));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
!!! DROPPED 1 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 5
)");

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 6"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 7"))));
  writer.Write();

  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 8"))));
  EXPECT_TRUE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 9"))));
  EXPECT_FALSE(store.Add(Format(BuildLogMessage(FX_LOG_INFO, "line 10"))));
  writer.Write();

  ASSERT_TRUE(reader.Concatenate(output_path));

  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
!!! DROPPED 1 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 5
[15604.000][07559][07687][] INFO: line 6
[15604.000][07559][07687][] INFO: line 7
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
!!! DROPPED 1 LOG MESSAGES !!!
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

  RotatingFileSetReader reader(log_files);
  std::string contents;

  RunLoopFor(kWriterPeriod);

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
[15604.000][07559][07687][] INFO: line 12
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 LOG MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
[15604.000][07559][07687][] INFO: line 11
[15604.000][07559][07687][] INFO: line 12
[15604.000][07559][07687][] INFO: line 13
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(reader.Concatenate(output_path));
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
