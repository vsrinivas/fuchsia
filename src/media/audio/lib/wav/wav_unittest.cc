// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/media/audio/lib/wav/wav_reader.h"
#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::audio {
namespace {

struct __PACKED RiffChunkHeader {
  uint32_t four_cc;
  uint32_t length = 0;
};

constexpr char kFileName[] = "/tmp/test.wav";

TEST(WavWriterTest, EmptyFileRiffChunkSize) {
  WavWriter wav_writer;
  files::DeletePath(kFileName, false);
  wav_writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 1, 1);
  wav_writer.Close();

  // Read WAV header
  std::vector<uint8_t> data;
  ASSERT_TRUE(files::ReadFileToVector(kFileName, &data));
  ASSERT_GE(data.size(), static_cast<size_t>(0));
  int byte_offset = 0;
  auto riff_header = reinterpret_cast<RiffChunkHeader*>(&data.data()[byte_offset]);
  EXPECT_EQ(36u, riff_header->length) << "Riff chunk size is wrong";
}

TEST(WavWriterTest, NonEmptyFileRiffChunkSize) {
  WavWriter wav_writer;
  files::DeletePath(kFileName, false);
  wav_writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 1, 1);
  char buf[10];
  wav_writer.Write(buf, 10);
  wav_writer.Close();

  // Read WAV header
  std::vector<uint8_t> data;
  ASSERT_TRUE(files::ReadFileToVector(kFileName, &data));
  ASSERT_GE(data.size(), static_cast<size_t>(0));
  int byte_offset = 0;
  auto riff_header = reinterpret_cast<RiffChunkHeader*>(&data.data()[byte_offset]);
  EXPECT_EQ(46u, riff_header->length) << "Riff chunk size is wrong";
}

TEST(WavReaderTest, CanReadWrittenFile) {
  const char kWant[] = "abcdefghij";

  WavWriter writer;
  files::DeletePath(kFileName, false);
  writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2, 12, 8);
  writer.Write((void*)kWant, strlen(kWant));
  writer.Close();

  // Read WAV header
  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto reader = std::move(open_result.value());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::UNSIGNED_8, reader->sample_format());
  EXPECT_EQ(2u, reader->channel_count());
  EXPECT_EQ(12u, reader->frame_rate());
  EXPECT_EQ(8u, reader->bits_per_sample());

  char buf[128];
  auto read_bytes = reader->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(static_cast<size_t>(strlen(kWant)), read_bytes.value());

  std::string got(buf, read_bytes.value());
  EXPECT_STREQ(kWant, got.c_str());
}

TEST(WavReaderTest, CanResetAndRereadWrittenFile) {
  const char kWant[] = "abcdefghijkl";
  char buf[128];

  // Create the test file
  WavWriter writer;
  files::DeletePath(kFileName, false);
  writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 32, 8);
  writer.Write((void*)kWant, strlen(kWant));
  writer.Close();

  // Read WAV header and the entire contents
  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto reader = std::move(open_result.value());
  auto read_bytes = reader->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  ASSERT_EQ(static_cast<size_t>(strlen(kWant)), read_bytes.value());
  std::string got(buf, read_bytes.value());
  ASSERT_STREQ(kWant, got.c_str());

  // Ensure that once we reach the end of the file, Read returns 0 and no error.
  auto end_of_file = reader->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(end_of_file.is_ok()) << end_of_file.error();
  EXPECT_EQ(0u, end_of_file.value());

  // Reset should not fail.
  auto status = reader->Reset();
  EXPECT_EQ(status, 0);

  // Reset should seek the file read position to right after the header (same as first time).
  read_bytes = reader->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(static_cast<size_t>(strlen(kWant)), read_bytes.value());

  std::string got2(buf, read_bytes.value());
  EXPECT_STREQ(kWant, got2.c_str());
}

}  // namespace
}  // namespace media::audio
