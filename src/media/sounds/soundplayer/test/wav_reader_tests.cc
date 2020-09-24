// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/sounds/soundplayer/wav_reader.h"

namespace soundplayer {
namespace test {

bool VmoMatches(const zx::vmo& vmo, const uint8_t* data, size_t size) {
  auto buffer = std::vector<uint8_t>(size);
  zx_status_t status = vmo.read(buffer.data(), 0, size);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read " << size << " bytes from vmo";
    return false;
  }

  return memcmp(data, buffer.data(), size) == 0;
}

TEST(WavReaderTests, Successful) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x01, 0x02, 0x03, 0x04,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(4u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_16, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(
      VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size()), sound.size()));
}

TEST(WavReaderTests, OneExtraByte) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x01, 0x02, 0x03, 0x04,  // frames
      0x00,                    // extra
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(4u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_16, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size() - 1),
                         sound.size()));
}

TEST(WavReaderTests, TwoExtraBytes) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x01, 0x02, 0x03, 0x04,  // frames
      0x00, 0x01,              // extra
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(4u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_16, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size() - 2),
                         sound.size()));
}

TEST(WavReaderTests, ThreeExtraBytes) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x01, 0x02, 0x03, 0x04,  // frames
      0x00, 0x01, 0x02,        // extra
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(4u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_16, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size() - 3),
                         sound.size()));
}

TEST(WavReaderTests, LongFmtChunk) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x14, 0x00, 0x00, 0x00,  // fmt chunk size (20 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x00, 0x00, 0x00, 0x00,  // extra fmt stuff
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x01, 0x02, 0x03, 0x04,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(4u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_16, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(
      VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size()), sound.size()));
}

TEST(WavReaderTests, NoRiffHeader) {
  std::vector<uint8_t> test_data{
      0x52, 0x41, 0x46, 0x46,  // 'RAFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, NoWave) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x4f, 0x56, 0x45,  // 'WOVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, NoFmt) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x74,  // 'fmtt'
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, ShortFmt) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x0e, 0x00, 0x00, 0x00,  // fmt chunk size (14 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, BadEncoding) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x02, 0x00,              // encoding (2 = bad)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, ZeroChannels) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x00, 0x00,              // channel count (0)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, ThreeChannels) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x03, 0x00,              // channel count (3)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, NoData) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x65,  // 'date'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_error());
}

TEST(WavReaderTests, Truncated) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x01, 0x00,              // encoding (1 = pcm)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x10, 0xb1, 0x02, 0x00,  // byte rate (44100 * 4)
      0x04, 0x00,              // block alignment (4)
      0x10, 0x00,              // bits/sample (16)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x04, 0x00, 0x00, 0x00,  // data chunk size (4)
      0x00, 0x00, 0x00, 0x00,  // frames
  };

  for (size_t i = 1; i < 44; ++i) {
    WavReader under_test;
    auto result = under_test.Process(test_data.data(), test_data.size() - i);
    EXPECT_TRUE(result.is_error());
  }
}

TEST(WavReaderTests, Packed24) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,              // 'RIFF'
      0x00, 0x00, 0x00, 0x00,              // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,              // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,              // 'fmt '
      0x12, 0x00, 0x00, 0x00,              // fmt chunk size (18 bytes)
      0x01, 0x00,                          // encoding (1 = pcm)
      0x02, 0x00,                          // channel count (2)
      0x44, 0xac, 0x00, 0x00,              // frames/second (44100)
      0x98, 0x09, 0x04, 0x00,              // byte rate (44100 * 6)
      0x06, 0x00,                          // block alignment (6)
      0x18, 0x00,                          // bits/sample (24)
      0x00, 0x00,                          // extra param size (0)
      0x64, 0x61, 0x74, 0x61,              // 'data'
      0x0c, 0x00, 0x00, 0x00,              // data chunk size (12)
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // frame
      0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,  // frame
  };
  std::vector<uint8_t> expected{
      0x00, 0x01, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06,
      0x00, 0x07, 0x08, 0x09, 0x00, 0x0a, 0x0b, 0x0c,
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(16u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(VmoMatches(sound.vmo(), expected.data(), expected.size()));
}

TEST(WavReaderTests, Float) {
  std::vector<uint8_t> test_data{
      0x52, 0x49, 0x46, 0x46,  // 'RIFF'
      0x00, 0x00, 0x00, 0x00,  // RIFF chunk size (ignored)
      0x57, 0x41, 0x56, 0x45,  // 'WAVE'
      0x66, 0x6d, 0x74, 0x20,  // 'fmt '
      0x10, 0x00, 0x00, 0x00,  // fmt chunk size (16 bytes)
      0x03, 0x00,              // encoding (3 = pcm float)
      0x02, 0x00,              // channel count (2)
      0x44, 0xac, 0x00, 0x00,  // frames/second (44100)
      0x98, 0x09, 0x04, 0x00,  // byte rate (44100 * 6)
      0x06, 0x00,              // block alignment (6)
      0x20, 0x00,              // bits/sample (32)
      0x64, 0x61, 0x74, 0x61,  // 'data'
      0x08, 0x00, 0x00, 0x00,  // data chunk size (8)
      0x01, 0x02, 0x03, 0x04,  // frame
      0x05, 0x06, 0x07, 0x08,  // frame
  };

  WavReader under_test;
  auto result = under_test.Process(test_data.data(), test_data.size());
  EXPECT_TRUE(result.is_ok());
  auto& sound = result.value();
  EXPECT_EQ(8u, sound.size());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::FLOAT, sound.stream_type().sample_format);
  EXPECT_EQ(2u, sound.stream_type().channels);
  EXPECT_EQ(44100u, sound.stream_type().frames_per_second);
  EXPECT_TRUE(
      VmoMatches(sound.vmo(), test_data.data() + (test_data.size() - sound.size()), sound.size()));
}

}  // namespace test
}  // namespace soundplayer
