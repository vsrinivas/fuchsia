// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>

#include <cmath>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"
#include "src/developer/forensics/testing/stubs/logger.h"
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

class EncoderStub : public Encoder {
 public:
  EncoderStub() {}
  virtual ~EncoderStub() {}
  virtual EncodingVersion GetEncodingVersion() const { return EncodingVersion::kForTesting; }
  virtual std::string Encode(const std::string& msg) {
    input_.back() += msg;
    return msg;
  }
  virtual void Reset() { input_.push_back(""); }
  std::vector<std::string> GetInput() { return input_; }

 private:
  std::vector<std::string> input_ = {""};
};

class Decoder2x : public Decoder {
 public:
  Decoder2x() {}
  virtual ~Decoder2x() {}
  virtual EncodingVersion GetEncodingVersion() const { return EncodingVersion::kForTesting; }
  virtual std::string Decode(const std::string& msg) { return msg + msg; }
  virtual void Reset() {}
};

std::unique_ptr<Encoder> MakeIdentityEncoder() {
  return std::unique_ptr<Encoder>(new IdentityEncoder());
}

std::string MakeLogFilePath(files::ScopedTempDir& temp_dir, const size_t file_num) {
  return files::JoinPath(temp_dir.path(), std::to_string(file_num));
}

TEST(WriterTest, VerifyFileOrdering) {
  // Set up the writer such that each file can fit 1 log message. When consuming a message the
  // end of block signal will be sent and a new empty file will be produced from file rotation.
  // From this behavior although we use 4 files, we only expect to retrieve the last 3 messages.
  files::ScopedTempDir temp_dir;

  const size_t kBlockSize = kMaxLogLineSize;
  const size_t kBufferSize = kMaxLogLineSize;

  LogMessageStore store(kBlockSize, kBufferSize, MakeIdentityEncoder());
  SystemLogWriter writer(temp_dir.path(), 4u, &store);

  // Written to file 0
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  writer.Write();

  // Written to file 1
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  // Written to file 2
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  writer.Write();

  // Written to file 3
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  // Written to file 4
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 5")));
  writer.Write();

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");
  IdentityDecoder decoder;

  std::string content;
  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(temp_dir, 2u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 3
)");

  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(temp_dir, 3u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 4
)");

  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(temp_dir, 4u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 5
)");

  float compression_ratio;
  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

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

  const size_t kBlockSize = kMaxLogLineSize * 2;
  const size_t kBufferSize = kMaxLogLineSize * 2;

  auto encoder = std::unique_ptr<EncoderStub>(new EncoderStub());
  auto encoder_ptr = encoder.get();
  LogMessageStore store(kBlockSize, kBufferSize, std::move(encoder));
  SystemLogWriter writer(temp_dir.path(), 2u, &store);

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

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize * 2, MakeIdentityEncoder());
  SystemLogWriter writer(temp_dir.path(), 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");
  IdentityDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 1 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, VerifyCompressionRatio) {
  // Generate 2x data when decoding. The decoder data output is not useful, just its size.
  files::ScopedTempDir temp_dir;

  LogMessageStore store(kMaxLogLineSize * 4, kMaxLogLineSize * 4, MakeIdentityEncoder());
  SystemLogWriter writer(temp_dir.path(), 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");
  Decoder2x decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_EQ(compression_ratio, 2.0);
}

TEST(WriterTest, VerifyProductionEcoding) {
  files::ScopedTempDir temp_dir;

  // Set up the writer such that one file contains 5 log messages.
  auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
  LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, std::move(encoder));
  SystemLogWriter writer(temp_dir.path(), 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");
  ProductionDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_FALSE(std::isnan(compression_ratio));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, FilesAlreadyPresent) {
  files::ScopedTempDir temp_dir;

  {
    // Set up the writer such that one file contains at most 5 log messages.
    auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
    LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, std::move(encoder));

    SystemLogWriter writer(temp_dir.path(), 2u, &store);

    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
    writer.Write();
  }
  {
    // Set up the writer such that one file contains at most 5 log messages.
    auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
    LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, std::move(encoder));

    SystemLogWriter writer(temp_dir.path(), 2u, &store);

    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
    writer.Write();
  }

  files::ScopedTempDir output_dir;
  const std::string output_path = files::JoinPath(output_dir.path(), "output.txt");
  ProductionDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(std::vector<const std::string>(), temp_dir.path(), &decoder, output_path,
                          &compression_ratio));
  EXPECT_FALSE(std::isnan(compression_ratio));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
)");
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
