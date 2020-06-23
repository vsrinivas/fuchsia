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

#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/identity_decoder.h"
#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/feedback/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/testing/stubs/logger.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fsl/vmo/vector.h"

namespace forensics {
namespace feedback_data {
namespace {

using stubs::BuildLogMessage;

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line X")).size();
const size_t kDroppedFormatStrSize = std::string("!!! DROPPED X MESSAGES !!!\n").size();
const size_t kRepeatedFormatStrSize = std::string("!!! MESSAGE REPEATED X MORE TIMES !!!\n").size();
// We set the block size to an arbitrary large numbers for test cases where the block logic does
// not matter.
const size_t kVeryLargeBlockSize = kMaxLogLineSize * 100;

class EncoderStub : public Encoder {
 public:
  EncoderStub() {}
  virtual ~EncoderStub() {}
  virtual std::string Encode(const std::string& msg) {
    input_.back() += msg;
    return msg;
  }
  virtual void Reset() { input_.push_back(""); }
  std::vector<std::string> GetInput() { return input_; }

 private:
  std::vector<std::string> input_ = {""};
};

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
  LogMessageStore store(kVeryLargeBlockSize, /*max_buffer_capacity_bytes=*/1024,
                        MakeIdentityEncoder());

  SystemLogListener listener(services(), &store);
  listener.StartListening();

  // Run the loop for as much time needed to ensure at the stub calls LogMany() and Log() as
  // specified in the constructor.
  RunLoopFor(logger.TotalDelayBetweenDumps() + logger.TotalDelayBetweenMessages());

  bool end_of_block;
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
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
  EXPECT_FALSE(end_of_block);
}

// Returns auto-generated valid file paths
std::vector<const std::string> MakeLogFilePaths(files::ScopedTempDir& temp_dir, size_t num_files) {
  std::vector<const std::string> file_names;

  for (size_t file_idx = 0; file_idx < num_files; file_idx++) {
    file_names.push_back("file" + std::to_string(file_idx) + ".txt");
  }

  std::vector<const std::string> file_paths;

  for (const auto& file : file_names) {
    file_paths.push_back(files::JoinPath(temp_dir.path(), file));
  }

  return file_paths;
}

TEST(WriterTest, VerifyFileRotation) {
  // Set up the writer such that each file can fit 1 log message. When consuming a message the
  // end of block signal will be sent and a new empty file will be produced from file rotation.
  // From this behavior although we use 4 files, we only expect to retrieve the last 3 messages.
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> file_paths = MakeLogFilePaths(temp_dir, /*num_files=*/4);

  const size_t kBlockSize = kMaxLogLineSize;
  const size_t kBufferSize = kMaxLogLineSize;

  LogMessageStore store(kBlockSize, kBufferSize, MakeIdentityEncoder());
  SystemLogWriter writer(file_paths, &store);

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

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");
  IdentityDecoder decoder;

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
[15604.000][07559][07687][] INFO: line 5
)");
}

TEST(WriterTest, VerifyEncoderInput) {
  // Set up the writer such that each file can fit 2 log messages. We will then write 4 messages
  // and expect that the encoder receives 2 reset signals and encodes 2 log messages in each block.
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> file_paths = MakeLogFilePaths(temp_dir, /*num_files=*/2);

  const size_t kBlockSize = kMaxLogLineSize * 2;
  const size_t kBufferSize = kMaxLogLineSize * 2;

  auto encoder = std::unique_ptr<EncoderStub>(new EncoderStub());
  auto encoder_ptr = encoder.get();
  LogMessageStore store(kBlockSize, kBufferSize, std::move(encoder));
  SystemLogWriter writer(file_paths, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  std::vector<std::string> input = encoder_ptr->GetInput();
  EXPECT_EQ(input.size(), (size_t)3);

  EXPECT_EQ(input[0], R"([15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
)");

  EXPECT_EQ(input[1], R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, WritesMessages) {
  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> file_paths = MakeLogFilePaths(temp_dir, /*num_files=*/2);

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize * 2, MakeIdentityEncoder());
  SystemLogWriter writer(file_paths, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");
  IdentityDecoder decoder;

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 1 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));

  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
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
  //    0.00: line0, line1, line2, line3 -> write 1 -> file 1
  //    0.75: line4, line5, line6, line7 -> write 1 -> file 1
  //    1.50: line8  -> write 2 -> file 2
  //    2.25: line9  -> write 3 -> file 2
  //    3.00: line10 -> write 4 -> file 2
  //    3.75: line11 -> write 4 -> file 2
  //    4.50: line12 -> write 5 -> file 3
  //    5.25: line13 -> write 6 -> file 3
  //
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
      BuildLogMessage(FX_LOG_INFO, "line A"),
      BuildLogMessage(FX_LOG_INFO, "line B"),
      BuildLogMessage(FX_LOG_INFO, "line C"),
      BuildLogMessage(FX_LOG_INFO, "line D"),
  });

  stubs::LoggerDelayedResponses logger(dispatcher(), dumps, messages, kListenerPeriod);
  InjectServiceProvider(&logger);

  files::ScopedTempDir temp_dir;
  const std::vector<const std::string> file_paths = MakeLogFilePaths(temp_dir, /*num_files=*/2);

  const size_t kWriteSize = kMaxLogLineSize * 2 + kDroppedFormatStrSize;

  SystemLogRecorder recorder(dispatcher(), services(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWriterPeriod,
                                 .max_write_size_bytes = kWriteSize,
                                 .log_file_paths = file_paths,
                                 .total_log_size_bytes = file_paths.size() * kWriteSize,
                             },
                             std::unique_ptr<Encoder>(new ProductionEncoder()));
  recorder.Start();

  std::string contents;

  RunLoopFor(kWriterPeriod);

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  ProductionDecoder decoder;

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
[15604.000][07559][07687][] INFO: line C
)");

  RunLoopFor(kWriterPeriod);

  ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path));
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
[15604.000][07559][07687][] INFO: line C
[15604.000][07559][07687][] INFO: line D
)");
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
