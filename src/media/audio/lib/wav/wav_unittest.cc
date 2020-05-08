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

  WavWriter w;
  files::DeletePath(kFileName, false);
  w.Initialize(kFileName, fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2, 12, 8);
  w.Write((void*)kWant, strlen(kWant));
  w.Close();

  // Read WAV header
  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto r = std::move(open_result.value());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::UNSIGNED_8, r->sample_format());
  EXPECT_EQ(2u, r->channel_count());
  EXPECT_EQ(12u, r->frame_rate());
  EXPECT_EQ(8u, r->bits_per_sample());

  char buf[128];
  auto read_bytes = r->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(static_cast<size_t>(strlen(kWant)), read_bytes.value());

  std::string got(buf, read_bytes.value());
  EXPECT_STREQ(kWant, got.c_str());
}

}  // namespace
}  // namespace media::audio
