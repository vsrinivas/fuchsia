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
#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

::fpromise::result<fuchsia::logger::LogMessage, std::string> BuildLogMessage(
    const int32_t severity, const std::string& text,
    const zx::duration timestamp_offset = zx::duration(0),
    const std::vector<std::string>& tags = {}) {
  return ::fpromise::ok(testing::BuildLogMessage(severity, text, timestamp_offset, tags));
}

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const auto kMaxLogLineSize =
    StorageSize::Bytes(Format(BuildLogMessage(FX_LOG_INFO, "line X").value()).size());

constexpr auto kRootDirectory = "/root";
constexpr auto kWriteDirectory = "/root/write";
constexpr auto kReadDirectory = "/read";
constexpr auto kOutputFile = "/read/output.txt";

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

std::unique_ptr<RedactorBase> MakeIdentityRedactor() {
  return std::unique_ptr<RedactorBase>(new IdentityRedactor(inspect::BoolProperty()));
}

std::string MakeLogFilePath(const size_t file_num) {
  return files::JoinPath(kWriteDirectory, std::to_string(file_num));
}

TEST(WriterTest, VerifyFileOrdering) {
  // Set up the writer such that each file can fit 1 log message. When consuming a message the
  // end of block signal will be sent and a new empty file will be produced from file rotation.
  // From this behavior although we use 4 files, we only expect to retrieve the last 3 messages.
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  const StorageSize kBlockSize = kMaxLogLineSize;
  const StorageSize kBufferSize = kMaxLogLineSize;

  LogMessageStore store(kBlockSize, kBufferSize, MakeIdentityRedactor(), MakeIdentityEncoder());
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 4u, &store);

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

  memfs_manager.Create(kReadDirectory);
  IdentityDecoder decoder;

  std::string content;
  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(2u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 3
)");

  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(3u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 4
)");

  ASSERT_TRUE(files::ReadFileToString(MakeLogFilePath(4u), &content));
  EXPECT_EQ(content, R"([15604.000][07559][07687][] INFO: line 5
)");

  float compression_ratio;
  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
[15604.000][07559][07687][] INFO: line 5
)");
}

TEST(WriterTest, VerifyEncoderInput) {
  // Set up the writer such that each file can fit 2 log messages. We will then write 4 messages
  // and expect that the encoder receives 2 reset signals and encodes 2 log messages in each block.
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  const StorageSize kBlockSize = kMaxLogLineSize * 2;
  const StorageSize kBufferSize = kMaxLogLineSize * 2;

  auto encoder = std::unique_ptr<EncoderStub>(new EncoderStub());
  auto encoder_ptr = encoder.get();
  LogMessageStore store(kBlockSize, kBufferSize, MakeIdentityRedactor(), std::move(encoder));
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

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
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize * 2, MakeIdentityRedactor(),
                        MakeIdentityEncoder());
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  memfs_manager.Create(kReadDirectory);
  IdentityDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
!!! DROPPED 1 MESSAGES !!!
)");

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, VerifyCompressionRatio) {
  // Generate 2x data when decoding. The decoder data output is not useful, just its size.
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  LogMessageStore store(kMaxLogLineSize * 4, kMaxLogLineSize * 4, MakeIdentityRedactor(),
                        MakeIdentityEncoder());
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  memfs_manager.Create(kReadDirectory);
  Decoder2x decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 2.0);
}

TEST(WriterTest, VerifyProductionEcoding) {
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  // Set up the writer such that one file contains 5 log messages.
  auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
  LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, MakeIdentityRedactor(),
                        std::move(encoder));
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  memfs_manager.Create(kReadDirectory);
  ProductionDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_FALSE(std::isnan(compression_ratio));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, FilesAlreadyPresent) {
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  {
    // Set up the writer such that one file contains at most 5 log messages.
    auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
    LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, MakeIdentityRedactor(),
                          std::move(encoder));
    store.TurnOnRateLimiting();

    SystemLogWriter writer(kWriteDirectory, 2u, &store);

    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
    writer.Write();
  }
  {
    // Set up the writer such that one file contains at most 5 log messages.
    auto encoder = std::unique_ptr<Encoder>(new ProductionEncoder());
    LogMessageStore store(kMaxLogLineSize * 5, kMaxLogLineSize * 5, MakeIdentityRedactor(),
                          std::move(encoder));
    store.TurnOnRateLimiting();

    SystemLogWriter writer(kWriteDirectory, 2u, &store);

    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
    EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
    writer.Write();
  }

  memfs_manager.Create(kReadDirectory);
  ProductionDecoder decoder;

  float compression_ratio;
  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_FALSE(std::isnan(compression_ratio));

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
)");
}

TEST(WriterTest, FailCreateDirectory) {
  // Don't set up kRootDirectory
  testing::ScopedMemFsManager memfs_manager;

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize * 2, MakeIdentityRedactor(),
                        MakeIdentityEncoder());
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

  // Create the kRootDirectory so kWriteDirectory can be made by |writer| after the next set of
  // writes.
  memfs_manager.Create(kRootDirectory);

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  memfs_manager.Create(kReadDirectory);
  IdentityDecoder decoder;

  float compression_ratio;
  EXPECT_FALSE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));

  std::string contents;
  EXPECT_FALSE(files::ReadFileToString(kOutputFile, &contents));

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

TEST(WriterTest, DirectoryDisappears) {
  testing::ScopedMemFsManager memfs_manager;
  memfs_manager.Create(kRootDirectory);

  // Set up the writer such that each file can fit 2 log messages and the "!!! DROPPED..."
  // string.
  LogMessageStore store(kMaxLogLineSize * 2, kMaxLogLineSize * 2, MakeIdentityRedactor(),
                        MakeIdentityEncoder());
  store.TurnOnRateLimiting();
  SystemLogWriter writer(kWriteDirectory, 2u, &store);

  // Destroy kWriteDirectory so the next set of writes fail and the directory is recreated.
  ASSERT_TRUE(files::DeletePath(kWriteDirectory, /*recursive=*/true));

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 0")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 1")));
  EXPECT_FALSE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 2")));
  writer.Write();

  memfs_manager.Create(kReadDirectory);
  IdentityDecoder decoder;

  float compression_ratio;
  EXPECT_FALSE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));

  std::string contents;
  EXPECT_FALSE(files::ReadFileToString(kOutputFile, &contents));

  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 3")));
  EXPECT_TRUE(store.Add(BuildLogMessage(FX_LOG_INFO, "line 4")));
  writer.Write();

  ASSERT_TRUE(Concatenate(kWriteDirectory, &decoder, kOutputFile, &compression_ratio));
  EXPECT_EQ(compression_ratio, 1.0);

  ASSERT_TRUE(files::ReadFileToString(kOutputFile, &contents));
  EXPECT_EQ(contents, R"([15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
)");
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
