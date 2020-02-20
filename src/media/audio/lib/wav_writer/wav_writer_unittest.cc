// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/wav_writer/wav_writer.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace media::audio {
namespace {

struct __PACKED RiffChunkHeader {
  uint32_t four_cc;
  uint32_t length = 0;
};

const char* file_name = "/tmp/test.wav";

TEST(WavWriterTest, EmptyFileRiffChunkSize) {
  WavWriter wav_writer;
  files::DeletePath(file_name, false);
  wav_writer.Initialize(file_name, fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 1, 1);
  wav_writer.Close();

  // Read WAV header
  std::vector<uint8_t> data;
  ASSERT_TRUE(files::ReadFileToVector(file_name, &data));
  ASSERT_GE(data.size(), static_cast<size_t>(0));
  int byte_offset = 0;
  auto riff_header = reinterpret_cast<RiffChunkHeader*>(&data.data()[byte_offset]);
  EXPECT_EQ(36u, riff_header->length) << "Riff chunk size is wrong";
}

TEST(WavWriterTest, NonEmptyFileRiffChunkSize) {
  WavWriter wav_writer;
  files::DeletePath(file_name, false);
  wav_writer.Initialize(file_name, fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 1, 1);
  char buf[10];
  wav_writer.Write(buf, 10);
  wav_writer.Close();

  // Read WAV header
  std::vector<uint8_t> data;
  ASSERT_TRUE(files::ReadFileToVector(file_name, &data));
  ASSERT_GE(data.size(), static_cast<size_t>(0));
  int byte_offset = 0;
  auto riff_header = reinterpret_cast<RiffChunkHeader*>(&data.data()[byte_offset]);
  EXPECT_EQ(46u, riff_header->length) << "Riff chunk size is wrong";
}

}  // namespace
}  // namespace media::audio
