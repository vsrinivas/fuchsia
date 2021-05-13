// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"

#include <lib/syslog/logger.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

constexpr zx::duration kTimeWaitForLimitedLogs = zx::sec(60);

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const auto kMaxLogLineSize =
    StorageSize::Bytes(std::string("[15604.000][07559][07687][] INFO: line X\n").size());
const auto kDroppedFormatStrSize =
    StorageSize::Bytes(std::string("!!! DROPPED X MESSAGES !!!\n").size());

TEST(Encoding, VerifyProductionEncoderDecoderVersion) {
  // Verify that the production decoder and encoder always have the same version.
  ProductionEncoder encoder;
  ProductionDecoder decoder;

  EXPECT_EQ(encoder.GetEncodingVersion(), decoder.GetEncodingVersion());
}

std::string BuildLogMessage(const std::string& message) {
  constexpr char fmt[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 15604000000000,
      "severity": "INFO",
      "pid": 7559,
      "tid": 7687
    },
    "payload": {
      "root": {
      "message": {
        "value": "%s"
        }
      }
    }
  }
]
)JSON";

  return fxl::StringPrintf(fmt, message.c_str());
}

using SystemLogRecorderTest = UnitTestFixture;

TEST_F(SystemLogRecorderTest, SingleThreaded_SmokeTest) {
  // To simulate a real load, we set up the test with the following conditions:
  //  * The listener will get messages every 750 milliseconds.
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
  // Note: we use the IdentityEncoder to easily control which messages are dropped.
  // Note 2: we offset time by kTimeWaitForLimitedLog to wait for the buffer rate limiter.
  const zx::duration kArchivePeriod = zx::msec(750);
  const zx::duration kWriterPeriod = zx::sec(1);

  const std::vector<std::vector<std::string>> json_batches({
      {
          BuildLogMessage("line 0"),
          BuildLogMessage("line 1"),
          BuildLogMessage("line 2"),
          BuildLogMessage("line 3"),

      },
      {
          BuildLogMessage("line 4"),
          BuildLogMessage("line 5"),
          BuildLogMessage("line 6"),
          BuildLogMessage("line 7"),
      },
      {BuildLogMessage("line 8")},
      {BuildLogMessage("line 9")},
      {BuildLogMessage("line A")},
      {BuildLogMessage("line B")},
      {BuildLogMessage("line C")},
      {BuildLogMessage("line D")},
      {},
  });

  stubs::DiagnosticsArchive archive(std::make_unique<stubs::DiagnosticsBatchIteratorDelayedBatches>(
      dispatcher(), json_batches, kTimeWaitForLimitedLogs, kArchivePeriod));

  InjectServiceProvider(&archive, kArchiveAccessorName);

  files::ScopedTempDir temp_dir;

  const StorageSize kWriteSize = kMaxLogLineSize * 2 + kDroppedFormatStrSize;

  SystemLogRecorder recorder(dispatcher(), dispatcher(), services(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWriterPeriod,
                                 .max_write_size = kWriteSize,
                                 .logs_dir = temp_dir.path(),
                                 .max_num_files = 2u,
                                 .total_log_size = 2u * kWriteSize,
                             },
                             std::unique_ptr<Encoder>(new IdentityEncoder()));
  recorder.Start();

  RunLoopFor(kTimeWaitForLimitedLogs);

  std::string contents;

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");

  IdentityDecoder decoder;

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
)");

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
)");

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
)");

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
[15604.000][07559][07687][] INFO: line C
)");

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line A
[15604.000][07559][07687][] INFO: line B
[15604.000][07559][07687][] INFO: line C
[15604.000][07559][07687][] INFO: line D
)");
}

TEST_F(SystemLogRecorderTest, SingleThreaded_StopAndDeleteLogs) {
  // To simulate a real load, we set up the test with the following conditions:
  //  * The listener will get messages every 750 milliseconds.
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
  // Note: we use the IdentityEncoder to easily control which messages are dropped.
  // Note 2: we offset time by kTimeWaitForLimitedLog to wait for the buffer rate limiter.
  const zx::duration kArchivePeriod = zx::msec(750);
  const zx::duration kWriterPeriod = zx::sec(1);

  const std::vector<std::vector<std::string>> json_batches({
      {
          BuildLogMessage("line 0"),
          BuildLogMessage("line 1"),
          BuildLogMessage("line 2"),
          BuildLogMessage("line 3"),

      },
      {
          BuildLogMessage("line 4"),
          BuildLogMessage("line 5"),
          BuildLogMessage("line 6"),
          BuildLogMessage("line 7"),
      },
      {BuildLogMessage("line 8")},
      {BuildLogMessage("line 9")},
      {BuildLogMessage("line A")},
      {BuildLogMessage("line B")},
      {BuildLogMessage("line C")},
      {BuildLogMessage("line D")},
      {},
  });

  stubs::DiagnosticsArchive archive(std::make_unique<stubs::DiagnosticsBatchIteratorDelayedBatches>(
      dispatcher(), json_batches, kTimeWaitForLimitedLogs, kArchivePeriod, /*strict=*/false));

  InjectServiceProvider(&archive, kArchiveAccessorName);

  files::ScopedTempDir temp_dir;

  const StorageSize kWriteSize = kMaxLogLineSize * 2 + kDroppedFormatStrSize;

  SystemLogRecorder recorder(dispatcher(), dispatcher(), services(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWriterPeriod,
                                 .max_write_size = kWriteSize,
                                 .logs_dir = temp_dir.path(),
                                 .max_num_files = 2u,
                                 .total_log_size = 2u * kWriteSize,
                             },
                             std::unique_ptr<Encoder>(new IdentityEncoder()));
  recorder.Start();

  RunLoopFor(kTimeWaitForLimitedLogs);

  std::string contents;

  RunLoopFor(kWriterPeriod);

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");

  IdentityDecoder decoder;

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
)");

  recorder.StopAndDeleteLogs();

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_FALSE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
  }

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_FALSE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
  }

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_FALSE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
  }

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_FALSE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
  }

  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_FALSE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
  }
}

TEST_F(SystemLogRecorderTest, SingleThreaded_Flush) {
  // To simulate a real load, we set up the test with the following conditions:
  //  * The listener will get messages every 750 milliseconds.
  //  * The writer writes messages every 1 second. Each write will contain at most 2 log
  //    lines.
  //  * Each file will contain at most 2 log lines.
  //
  //    Using the above, we'll see log lines arrive with the at the following times:
  //    0.00: line0, line1, line2, line3 -> write 1 -> file 1
  //    0.75: line4, line5, line6, line7 -> write 1 -> file 1
  //    0.75: FLUSH
  //    1.50: line8  -> write 2 -> file 2
  //
  // Note: we use the IdentityEncoder to easily control which messages are dropped.
  // Note 2: we offset time by kTimeWaitForLimitedLog to wait for the buffer rate limiter.
  const zx::duration kArchivePeriod = zx::msec(750);
  const zx::duration kWriterPeriod = zx::sec(1);

  const std::vector<std::vector<std::string>> json_batches({
      {
          BuildLogMessage("line 0"),
          BuildLogMessage("line 1"),
          BuildLogMessage("line 2"),
          BuildLogMessage("line 3"),

      },
      {
          BuildLogMessage("line 4"),
          BuildLogMessage("line 5"),
          BuildLogMessage("line 6"),
          BuildLogMessage("line 7"),
      },
      {BuildLogMessage("line 8")},
      {},
  });

  stubs::DiagnosticsArchive archive(std::make_unique<stubs::DiagnosticsBatchIteratorDelayedBatches>(
      dispatcher(), json_batches, kTimeWaitForLimitedLogs, kArchivePeriod, /*strict=*/true));

  InjectServiceProvider(&archive, kArchiveAccessorName);

  files::ScopedTempDir temp_dir;

  const std::string kFlushStr = "FLUSH\n";

  const StorageSize kWriteSize =
      kMaxLogLineSize * 2 + kDroppedFormatStrSize + StorageSize::Bytes(kFlushStr.size());

  SystemLogRecorder recorder(dispatcher(), dispatcher(), services(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWriterPeriod,
                                 .max_write_size = kWriteSize,
                                 .logs_dir = temp_dir.path(),
                                 .max_num_files = 2u,
                                 .total_log_size = 2u * kWriteSize,
                             },
                             std::unique_ptr<Encoder>(new IdentityEncoder()));
  recorder.Start();

  RunLoopFor(kTimeWaitForLimitedLogs);

  RunLoopFor(kArchivePeriod);
  recorder.Flush(kFlushStr);

  std::string contents;

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");

  IdentityDecoder decoder;

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
FLUSH
)");

  RunLoopFor(kWriterPeriod);
  RunLoopFor(kWriterPeriod);

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(temp_dir.path(), &decoder, output_path, &compression_ratio));
    EXPECT_EQ(compression_ratio, 1.0);
  }
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 6 MESSAGES !!!
FLUSH
[15604.000][07559][07687][] INFO: line 8
)");
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
