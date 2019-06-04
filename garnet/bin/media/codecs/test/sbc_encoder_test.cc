// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>

#include "frame_encoder.h"
#include "raw_audio.h"

constexpr char kRawAUFile[] = "/pkg/data/sfx_s16be.au";
constexpr char kGoldenEncodedFile[] = "/pkg/data/sfx_s16be.au.sbc";
constexpr size_t kGoldenEncodedFileSize = 9528;
constexpr size_t kBatchesPerPacket = 4;
constexpr double kAudioFrequency = 44100.0;
constexpr size_t kPcmSampleSize = 2;
constexpr size_t kPcmChannels = 1;
constexpr size_t kPcmFrameSize = kPcmSampleSize * kPcmChannels;
constexpr uint64_t kTimeBase = ZX_SEC(1);
// kByteDuration is the ratio of media duration to real time for a single byte.
constexpr double kByteDuration =
    ZX_SEC(1) / kAudioFrequency / double(kPcmFrameSize);
constexpr uint64_t kTimeStampTolerance = ZX_USEC(100);
constexpr char kDebugFilename[] = "/tmp/sbc_encoder_output.sbc";

constexpr size_t kSbcBlockCount = 4;
constexpr size_t kSbcSubBands = 8;
// The number of PCM frames the SBC Encoder will encode at a time. This is
// according the rules of SBC.
constexpr size_t kSbcBatchSize = kSbcSubBands * kSbcBlockCount;

// This the frame length for our particular parameters. See
// `codec_adapter_sbc_encoder.h` for how this is calculated.
constexpr size_t kSbcFrameLength = 24;

constexpr size_t kTestCases = 3;

uint64_t TimestampForByte(size_t i) { return i * kByteDuration; }

uint64_t Difference(uint64_t a, uint64_t b) {
  if (a < b) {
    return b - a;
  }
  return a - b;
}

std::vector<uint8_t> encode(RawAudio::CodecInput&& codec_input,
                            component::StartupContext* startup_context,
                            std::optional<std::vector<bool>> expect_timestamp) {
  std::vector<FrameEncoder::PayloadOffset> offsets;
  for (auto position : codec_input.payload_offsets) {
    offsets.push_back({
        .position = position,
        .timestamp_ish = !!expect_timestamp
                             ? std::make_optional(TimestampForByte(position))
                             : std::nullopt,
    });
  }

  codec_input.format.set_timebase(kTimeBase);
  auto frames = FrameEncoder::EncodeFrames(
      FrameEncoder::Payload{
          .data = codec_input.data,
          .offsets = offsets,
      },
      codec_input.format, startup_context,
      /*expect_access_units=*/true);

  std::vector<uint8_t> concat;
  uint64_t last_timestamp = 0;
  size_t sbc_frame_index = 0;
  for (auto frame : frames) {
    if (expect_timestamp && (*expect_timestamp)[sbc_frame_index]) {
      FXL_CHECK(frame.timestamp_ish)
          << "SBC frame " << sbc_frame_index << " missing timestamp.";
      FXL_CHECK(*frame.timestamp_ish >= last_timestamp)
          << "Got timestamp " << *frame.timestamp_ish
          << " but last timestamp was " << last_timestamp;
      uint64_t expected_timestamp =
          TimestampForByte(sbc_frame_index * kSbcBatchSize * kPcmFrameSize);
      FXL_CHECK(Difference(*frame.timestamp_ish, expected_timestamp) <=
                kTimeStampTolerance)
          << "At byte " << concat.size() << " of output, expected timestamp "
          << expected_timestamp << " but got " << *frame.timestamp_ish;

      last_timestamp = *frame.timestamp_ish;
    } else {
      FXL_CHECK(!frame.timestamp_ish)
          << "SBC frame " << sbc_frame_index << " should not have timestamp.";
    }
    ++sbc_frame_index;
    concat.insert(concat.end(), frame.data.begin(), frame.data.end());
  }

  return concat;
}

void write_debug_file(const fuchsia::media::SbcEncoderSettings& sbc_settings,
                      size_t batch_size, const RawAudio& raw_audio,
                      component::StartupContext* startup_context) {
  auto codec_input = raw_audio.BuildCodecInput(kBatchesPerPacket * batch_size);

  fuchsia::media::EncoderSettings encoder_settings;
  encoder_settings.set_sbc(sbc_settings);
  codec_input.format.set_encoder_settings(std::move(encoder_settings));

  auto result = encode(std::move(codec_input), startup_context,
                       /*set_timestamps=*/std::nullopt);

  std::fstream test_file(kDebugFilename, std::ios::binary | std::ios::out);
  test_file.write(reinterpret_cast<char*>(&result[0]), result.size());
  test_file.close();
}

// To get the encoder output for inspection, run
//   fx run-test sbc_encoder_test -- --write_debug_file
// Which will write the sbc output to `kDebugFilename`. You can inspect its
// header information with
//   sbcdec -v <filename>
// and you can turn it into an AU file for inspection in Audacity with
//   ffmpeg -i <filename> <outfile>.au
// (Check `ffmpeg -decoders | grep sbc` to ensure your build has sbc.)
// Otherwise, the frames will be compared against a golden file (which is also
// checked in because it is helpful to inspect against visually).
int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    FXL_LOG(FATAL) << "Failed to parse log settings.";
  }

  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  auto raw_audio = RawAudio::FromAUFile(kRawAUFile);

  fuchsia::media::SbcEncoderSettings sbc = {
      .sub_bands = static_cast<fuchsia::media::SbcSubBands>(kSbcSubBands),
      .block_count = static_cast<fuchsia::media::SbcBlockCount>(kSbcBlockCount),
      .channel_mode = fuchsia::media::SbcChannelMode::MONO,
      .bit_pool = 31};

  if (command_line.HasOption("write_debug_file")) {
    write_debug_file(sbc, kSbcBatchSize, raw_audio, startup_context.get());
    return 0;
  }

  std::fstream golden_file(kGoldenEncodedFile,
                           std::ios::binary | std::ios::in | std::ios::ate);
  FXL_CHECK(golden_file.is_open())
      << "Could not open " << kGoldenEncodedFile << " for reading.";

  const size_t golden_file_size = golden_file.tellg();
  FXL_CHECK(golden_file_size == kGoldenEncodedFileSize);
  golden_file.seekg(0);

  uint8_t golden_file_content[golden_file_size];
  golden_file.read(reinterpret_cast<char*>(&golden_file_content[0]),
                   golden_file_size);

  const size_t output_packet_count =
      kGoldenEncodedFileSize / kSbcFrameLength + 1;
  struct TestCase {
    size_t frames_per_packet;
    bool set_timestamps;
  };
  auto timestamp_pattern =
      [output_packet_count](size_t frames_per_packet) -> std::vector<bool> {
    std::vector<bool> expect_timestamp(output_packet_count, false);
    const size_t bytes_per_packet = frames_per_packet * kPcmFrameSize;
    for (size_t i = 0; i < output_packet_count; ++i) {
      const size_t input_index = i * kSbcBatchSize * kPcmFrameSize;
      // An input timestamp is consumed by the first output packet whose
      // starting offset is >= the input packet's offset, so not all output
      // packets get timestamps.
      expect_timestamp[i] =
          input_index % bytes_per_packet < kSbcBatchSize * kPcmFrameSize;
    }
    return expect_timestamp;
  };
  std::vector<TestCase> test_cases = {
      {.frames_per_packet = 1, .set_timestamps = true},
      {.frames_per_packet = 3, .set_timestamps = true},
      {.frames_per_packet = kSbcBatchSize + 1, .set_timestamps = true},
      {.frames_per_packet = kSbcBatchSize, .set_timestamps = true}};
  srand(100);
  for (size_t i = 0; i < kTestCases; ++i) {
    test_cases.push_back({
        .frames_per_packet = rand() % (kSbcBatchSize * kPcmFrameSize),
        .set_timestamps = rand() % 2 == 0,
    });
  }

  // We test that the encoder produces data identical to the golden file for a
  // variety of parameters. These deltas on the sbc batch size are chosen just
  // to be funky and ensure our encoder can properly handle audio on PCM frame
  // boundaries, not just sbc batch boundaries.
  for (auto& test_case : test_cases) {
    FXL_VLOG(3) << "Testing with PCM frames per packet: "
                << test_case.frames_per_packet;
    FXL_VLOG(3) << "Timestamps enabled: " << test_case.set_timestamps;
    auto codec_input = raw_audio.BuildCodecInput(test_case.frames_per_packet);

    fuchsia::media::EncoderSettings encoder_settings;
    encoder_settings.set_sbc(sbc);
    codec_input.format.set_encoder_settings(std::move(encoder_settings));

    auto actual_file =
        encode(std::move(codec_input), startup_context.get(),
               test_case.set_timestamps
                   ? std::make_optional<std::vector<bool>>(
                         timestamp_pattern(test_case.frames_per_packet))
                   : std::nullopt);

    // The actual file should be bigger than the golden file size, because the
    // golden file does not invent padding like our encoder does. Since the
    // input data is not exactly a multiple of our pcm block size, the result
    // should be at most one frame larger.
    FXL_CHECK(golden_file_size + kSbcFrameLength >= actual_file.size())
        << "File is wrong size; expected: " << golden_file_size
        << " got: " << actual_file.size();

    for (size_t i = 0; i < golden_file_size; i++) {
      FXL_CHECK(golden_file_content[i] == actual_file[i])
          << "Bytes " << i << " differ from golden file.";
    }
  }

  // Ensure that some output packets are allowed to emit without timestamps
  // when input packets have timestamps, contain more than one PCM batch, and
  // are aligned, but no timebase is set for extrapolation.
  auto pcm_format = raw_audio.BuildCodecInput(100).format;
  fuchsia::media::EncoderSettings encoder_settings;
  encoder_settings.set_sbc(sbc);
  pcm_format.set_encoder_settings(std::move(encoder_settings));
  constexpr size_t kExpectedTimestamp = 13404;
  auto payload = FrameEncoder::Payload{
      .data = std::vector<uint8_t>(kSbcBatchSize * kPcmFrameSize * 2, 0),
      .offsets = {FrameEncoder::PayloadOffset{
          .position = 0,
          .timestamp_ish = {kExpectedTimestamp},
      }}};
  auto frames =
      FrameEncoder::EncodeFrames(payload, pcm_format, startup_context.get(),
                                 /*expect_access_units=*/true);
  FXL_CHECK(frames.size() == 2) << "Frames: " << frames.size();
  FXL_CHECK(frames[0].data.size() == kSbcFrameLength)
      << "Size: " << frames[0].data.size();
  FXL_CHECK(frames[0].timestamp_ish.has_value());
  FXL_CHECK(frames[0].timestamp_ish.value() == kExpectedTimestamp);
  FXL_CHECK(frames[1].data.size() == kSbcFrameLength)
      << "Size: " << frames[1].data.size();
  FXL_CHECK(!frames[1].timestamp_ish.has_value());

  return 0;
}
