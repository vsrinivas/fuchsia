// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/sinc_sampler.h"

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/filter.h"

namespace media::audio::mixer {
namespace {

std::unique_ptr<Mixer> SelectSincSampler(
    uint32_t source_channels, uint32_t source_frame_rate,
    fuchsia::media::AudioSampleFormat source_format, uint32_t dest_channels,
    uint32_t dest_frame_rate,
    fuchsia::media::AudioSampleFormat dest_format = fuchsia::media::AudioSampleFormat::FLOAT) {
  fuchsia::media::AudioStreamType source_stream_type;
  source_stream_type.channels = source_channels;
  source_stream_type.frames_per_second = source_frame_rate;
  source_stream_type.sample_format = source_format;

  fuchsia::media::AudioStreamType dest_stream_type;
  dest_stream_type.channels = dest_channels;
  dest_stream_type.frames_per_second = dest_frame_rate;
  dest_stream_type.sample_format = dest_format;

  return mixer::SincSampler::Select(source_stream_type, dest_stream_type);
}

TEST(SincSamplerTest, Construction) {
  //
  // These formats are supported
  auto mixer = SelectSincSampler(1, 48000, fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 96000);
  EXPECT_NE(mixer, nullptr);

  mixer = SelectSincSampler(2, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000);
  EXPECT_NE(mixer, nullptr);

  mixer = SelectSincSampler(2, 24000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1, 22050);
  EXPECT_NE(mixer, nullptr);

  mixer = SelectSincSampler(1, 48000, fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000);
  EXPECT_NE(mixer, nullptr);

  //
  // These formats are not supported
  mixer = SelectSincSampler(3, 24000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1, 22050);
  EXPECT_EQ(mixer, nullptr);

  mixer = SelectSincSampler(1, 48000, fuchsia::media::AudioSampleFormat::FLOAT, 9, 96000);
  EXPECT_EQ(mixer, nullptr);
}

// Test that position advances as it should
TEST(SincSamplerTest, SamplingPosition_Basic) {
  auto mixer = SelectSincSampler(1, 48000, fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000);

  EXPECT_EQ(mixer->pos_filter_width(), kSincFilterSideLength - 1u);
  EXPECT_EQ(mixer->neg_filter_width(), kSincFilterSideLength - 1u);

  bool should_not_accum = false;
  bool source_is_consumed;
  float source[] = {1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f,  8.0f,  9.0f,  10.0f,
                    11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f};
  float dest[20];
  const uint32_t src_frames = fbl::count_of(source);
  const uint32_t frac_src_frames = src_frames << kPtsFractionalBits;
  const uint32_t dest_frames = fbl::count_of(dest);

  int32_t frac_src_offset = (3 << (kPtsFractionalBits - 2));
  uint32_t dest_offset = 0;

  // Pass in 20 frames
  source_is_consumed = mixer->Mix(dest, dest_frames, &dest_offset, source, frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed);
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);
  EXPECT_EQ(dest_offset, static_cast<uint32_t>(frac_src_offset) >> kPtsFractionalBits);
}

// Validate the "seam" between buffers, at unity rate-conversion
TEST(SincSamplerTest, SamplingValues_DC_Unity) {
  constexpr uint32_t kSourceRate = 44100;
  constexpr uint32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, kSourceRate, fuchsia::media::AudioSampleFormat::FLOAT, 1, kDestRate);

  bool should_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 512;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 2;
  uint32_t frac_src_frames = kSourceLen << kPtsFractionalBits;
  int32_t frac_src_offset = 0;
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = Mixer::FRAC_ONE;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);
  EXPECT_EQ(static_cast<uint32_t>(frac_src_offset) >> kPtsFractionalBits, dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  frac_src_offset -= frac_src_frames;
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while down-sampling
TEST(SincSamplerTest, SamplingValues_DC_DownSample) {
  constexpr uint32_t kSourceRate = 48000;
  constexpr uint32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, kSourceRate, fuchsia::media::AudioSampleFormat::FLOAT, 1, kDestRate);

  bool should_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 512;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 2;
  uint32_t frac_src_frames = kSourceLen << kPtsFractionalBits;
  int32_t frac_src_offset = 0;
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = (Mixer::FRAC_ONE * kSourceRate) / kDestRate;
  info.rate_modulo = Mixer::FRAC_ONE * kSourceRate - (info.step_size * kDestRate);
  info.denominator = kDestRate;
  info.src_pos_modulo = 0;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  frac_src_offset -= frac_src_frames;
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while up-sampling
TEST(SincSamplerTest, SamplingValues_DC_UpSample) {
  constexpr uint32_t kSourceRate = 12000;
  constexpr uint32_t kDestRate = 48000;
  auto mixer =
      SelectSincSampler(1, kSourceRate, fuchsia::media::AudioSampleFormat::FLOAT, 1, kDestRate);

  bool should_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 1024;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 8;
  uint32_t frac_src_frames = kSourceLen << kPtsFractionalBits;
  int32_t frac_src_offset = 0;
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = (Mixer::FRAC_ONE * kSourceRate) / kDestRate;
  info.rate_modulo = Mixer::FRAC_ONE * kSourceRate - info.step_size * kDestRate;
  info.denominator = kDestRate;
  info.src_pos_modulo = 0;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen / 2, &dest_offset, source.get(),
                                  frac_src_frames, &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);
  EXPECT_EQ(static_cast<uint64_t>(frac_src_offset) >> (kPtsFractionalBits - 2), dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  frac_src_offset -= frac_src_frames;
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), frac_src_frames,
                                  &frac_src_offset, should_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << frac_src_offset;
  EXPECT_TRUE(frac_src_offset + mixer->pos_filter_width() >= frac_src_frames);

  // The two samples before and after the "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

}  // namespace
}  // namespace media::audio::mixer
