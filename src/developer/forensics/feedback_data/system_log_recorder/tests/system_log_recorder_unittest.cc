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
#include "src/developer/forensics/testing/stubs/logger.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

using stubs::BuildLogMessage;

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line X")).size();
const size_t kDroppedFormatStrSize = std::string("!!! DROPPED X MESSAGES !!!\n").size();

TEST(Encoding, VerifyProductionEncoderDecoderVersion) {
  // Verify that the production decoder and encoder always have the same version.
  ProductionEncoder encoder;
  ProductionDecoder decoder;

  EXPECT_EQ(encoder.GetEncodingVersion(), decoder.GetEncodingVersion());
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
  // Note: we use the IdentityEncoder to easily control which messages are dropped.
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
                             std::unique_ptr<Encoder>(new IdentityEncoder()));
  recorder.Start();

  std::string contents;

  RunLoopFor(kWriterPeriod);

  const std::string output_path = files::JoinPath(temp_dir.path(), "output.txt");

  IdentityDecoder decoder;

  {
    float compression_ratio;
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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
    ASSERT_TRUE(Concatenate(file_paths, &decoder, output_path, &compression_ratio));
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

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
