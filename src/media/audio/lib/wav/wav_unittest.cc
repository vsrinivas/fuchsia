// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

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
  files::DeletePath(kFileName, false);

  WavWriter wav_writer;
  wav_writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::SIGNED_16,
                        8,       // channels
                        192000,  // frame_rate
                        16       // bits_per_sample
  );
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
  wav_writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::SIGNED_16,
                        5,      // channels
                        96000,  // frame_rate
                        16      // bits_per_sample
  );
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

class WavReaderTest : public testing::Test {
 protected:
  // To test WavReader separately from WavWriter, this raw byte stream defines a PCMWAVEFORMAT file
  // with 24 bytes of audio (3 bytes/sample). Set24BitFileAsPadded() alters the header info so that
  // a file from these bytes contains identical data but is interpreted as 4 bytes/sample.
  uint8_t k24BitFile[68]{
      0x52, 0x49, 0x46, 0x46,  // ---- 'RIFF' chunk
      0x3c, 0x0,  0x0,  0x0,   // 60 more bytes in this chunk (including RIFF type, 'fmt ', 'data')
      0x57, 0x41, 0x56, 0x45,  // 'WAVE' type of RIFF
      0x66, 0x6d, 0x74, 0x20,  // ---- 'fmt ' subchunk
      0x10, 0x0,  0x0,  0x0,   // 16 more bytes in this subchunk
      0x1,  0x0,  0x1,  0x0,   // format_tag 1  |  num_channels 1
      0x1,  0x0,  0x0,  0x0,   // frame rate 1
      0x3,  0x0,  0x0,  0x0,   // avg bytes/sec 3
      0x3,  0x0,  0x18, 0x0,   // block_align 3 | bits_per_sample 24
      0x64, 0x61, 0x74, 0x61,  // ---- 'data' subchunk
      0x18, 0x0,  0x0,  0x0,   // 24 more bytes in this chunk (24 bytes of audio data)
      0x01, 0x02, 0x03, 0x04,  // -- (first bytes of audio data) --
      0x05, 0x06, 0x07, 0x08,  // RIFF files are little-endian (regardless of host endian-ness).
      0x09, 0x0a, 0x0b, 0x0c,  // Interpreted as 'packed-24', these (expanded) values span from
      0x0d, 0x0e, 0x0f, 0x10,  // an initial value of 0x03020100
      0x11, 0x12, 0x13, 0x14,  // to a final value of 0x18171600
      0x15, 0x16, 0x17, 0x18,  // -- (final bytes of audio data) --
  };

  // Transform the above byte stream of 8 samples of packed-24, into 6 samples of padded-24-in-32.
  void Set24BitFileAsPadded() {
    k24BitFile[28] = 4;   // avg_bytes_per_sec 3 => 4
    k24BitFile[32] = 4;   // block_align (bytes per frame) 3 => 4
    k24BitFile[34] = 32;  // bits_per_sample 24 => 32
  }
};

TEST_F(WavReaderTest, CanReadWrittenFile) {
  const char kWant[] = "abcdefghij";

  WavWriter writer;
  files::DeletePath(kFileName, false);
  writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::UNSIGNED_8,
                    2,      // channels
                    12000,  // frame_rate
                    8       // bits_per_sample
  );
  writer.Write((void*)kWant, strlen(kWant));
  writer.Close();

  // Read WAV header
  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto reader = std::move(open_result.value());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::UNSIGNED_8, reader->sample_format());
  EXPECT_EQ(reader->channel_count(), 2u);
  EXPECT_EQ(reader->frame_rate(), 12000u);
  EXPECT_EQ(reader->bits_per_sample(), 8u);

  char buf[128];
  auto read_bytes = reader->Read(static_cast<void*>(buf), sizeof(buf));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(static_cast<size_t>(strlen(kWant)), read_bytes.value());

  std::string got(buf, read_bytes.value());
  EXPECT_STREQ(kWant, got.c_str());
}

TEST_F(WavReaderTest, CanResetAndRereadWrittenFile) {
  const char kWant[] = "abcdefghijkl";
  char buf[128];

  // Create the test file
  WavWriter writer;
  files::DeletePath(kFileName, false);
  writer.Initialize(kFileName, fuchsia::media::AudioSampleFormat::UNSIGNED_8,
                    1,      // channels
                    32000,  // frame_rate
                    8       // bits_per_sample
  );
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

TEST_F(WavReaderTest, CanReadPacked24File) {
  WavWriter writer;
  files::DeletePath(kFileName, false);
  files::WriteFile(kFileName, reinterpret_cast<const char*>(k24BitFile), sizeof(k24BitFile));

  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto reader = std::move(open_result.value());

  const std::array<int32_t, 8> kExpect{0x03020100, 0x06050400, 0x09080700, 0x0c0b0a00,
                                       0x0f0e0d00, 0x12111000, 0x15141300, 0x18171600};
  int32_t data_read[64];
  auto read_bytes = reader->Read(static_cast<void*>(data_read), sizeof(data_read));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(read_bytes.value(), kExpect.size() * sizeof(kExpect[0]));

  for (auto idx = 0u; idx < kExpect.size(); ++idx) {
    EXPECT_EQ(data_read[idx], kExpect[idx])
        << "[" << idx << "] got " << std::hex << data_read[idx] << ", wanted " << kExpect[idx];
  }
}

TEST_F(WavReaderTest, CanReadPadded24File) {
  Set24BitFileAsPadded();

  WavWriter writer;
  files::DeletePath(kFileName, false);
  files::WriteFile(kFileName, reinterpret_cast<const char*>(k24BitFile), sizeof(k24BitFile));

  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok());
  auto reader = std::move(open_result.value());

  const std::array<int32_t, 6> kExpect{0x04030201, 0x08070605, 0x0c0b0a09,
                                       0x100f0e0d, 0x14131211, 0x18171615};
  int32_t data_read[64];
  auto read_bytes = reader->Read(static_cast<void*>(data_read), sizeof(data_read));
  ASSERT_TRUE(read_bytes.is_ok()) << read_bytes.error();
  EXPECT_EQ(read_bytes.value(), kExpect.size() * sizeof(kExpect[0]));

  for (auto idx = 0u; idx < kExpect.size(); ++idx) {
    EXPECT_EQ(data_read[idx], kExpect[idx])
        << "[" << idx << "] got " << std::hex << data_read[idx] << ", wanted " << kExpect[idx];
  }
}

struct Format {
  Format(){};
  Format(fuchsia::media::AudioSampleFormat format, int32_t rate, int32_t f_size, int32_t s_size)
      : sample_format(format),
        frame_rate(rate),
        file_sample_size(f_size),
        stream_sample_size(s_size){};
  fuchsia::media::AudioSampleFormat sample_format;
  int32_t frame_rate;
  int32_t file_sample_size;
  int32_t stream_sample_size;
};
class WavWriterReaderTest : public testing::TestWithParam<std::tuple<Format, int32_t>> {};
TEST_P(WavWriterReaderTest, FormatSpecifics) {
  Format format = std::get<0>(GetParam());
  int32_t num_channels = std::get<1>(GetParam());
  constexpr int64_t kDataSize = 24;
  constexpr char kFileContent[kDataSize + 1] = "abcdefghijklmnopqrstuvwx";

  WavWriter writer;

  files::DeletePath(kFileName, false);
  // Create the test file
  ASSERT_TRUE(writer.Initialize(kFileName, format.sample_format,
                                static_cast<uint16_t>(num_channels), format.frame_rate,
                                static_cast<uint16_t>(format.file_sample_size * 8)));
  for (auto chan = 1; chan <= num_channels; ++chan) {
    // write out the same amount of file content for each channel
    ASSERT_TRUE(writer.Write((void*)kFileContent, strlen(kFileContent)));
  }
  ASSERT_TRUE(writer.Close());

  // Read WAV header and the entire contents.

  // When testing 24-bit file writing and reading (both "packed" and "padded") with the
  // WavWriter/Reader, we convey data both directions as "padded" 24-in-32-bit samples.
  // Although we tell WavWriter to use 24-bit, or 32-bit samples (in the FILE it saves),
  // WavReader will always tell us that the audio is 32-bit data (in the STREAM it produces).

  // To verify WavReader, we check the byte-count (did all data get read in) and the frame-count
  // (does WavReader correctly interpret the in-file packed/padded frame size).
  auto open_result = WavReader::Open(kFileName);
  ASSERT_TRUE(open_result.is_ok()) << "sample_format " << static_cast<int>(format.sample_format)
                                   << ", bits " << format.file_sample_size * 8 << ", rate "
                                   << format.frame_rate << ", chans " << num_channels;

  auto reader = std::move(open_result.value());
  EXPECT_EQ(reader->sample_format(), format.sample_format);
  EXPECT_EQ(static_cast<int32_t>(reader->bits_per_sample()), format.stream_sample_size * 8);
  EXPECT_EQ(static_cast<int32_t>(reader->frame_rate()), format.frame_rate);

  EXPECT_EQ(static_cast<int>(reader->channel_count()), num_channels);
  EXPECT_EQ(static_cast<int64_t>(reader->length_in_bytes()), kDataSize * num_channels);
  EXPECT_EQ(static_cast<int64_t>(reader->length_in_frames()),
            kDataSize / format.stream_sample_size);

  EXPECT_TRUE(writer.Delete());
}

std::array<Format, 5> formats{
    Format(fuchsia::media::AudioSampleFormat::FLOAT, 48000, 4, 4),
    Format(fuchsia::media::AudioSampleFormat::SIGNED_16, 96000, 2, 2),
    Format(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 16000, 3, 4),
    Format(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 192000, 4, 4),
    Format(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 44100, 1, 1),
};
INSTANTIATE_TEST_SUITE_P(null, WavWriterReaderTest,
                         testing::Combine(testing::ValuesIn(formats), testing::Range(1, 9)),
                         [](const testing::TestParamInfo<WavWriterReaderTest::ParamType>& info) {
                           std::string name;
                           switch (std::get<0>(info.param).sample_format) {
                             case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
                               name = "Uint8";
                               break;
                             case fuchsia::media::AudioSampleFormat::SIGNED_16:
                               name = "Int16";
                               break;
                             case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
                               name = (std::get<0>(info.param).file_sample_size == 3) ? "Packed24"
                                                                                      : "Padded24";
                               break;
                             case fuchsia::media::AudioSampleFormat::FLOAT:
                               name = "Float32";
                               break;
                           }
                           name += "_" + std::to_string(std::get<1>(info.param)) + "chan";
                           return name;
                         });

}  // namespace
}  // namespace media::audio
