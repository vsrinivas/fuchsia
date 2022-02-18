// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/point_sampler.h"

namespace media::audio::mixer {
namespace {

using Resampler = ::media::audio::Mixer::Resampler;

// SamplerDeathTest is parameterized to run for each sampler type.
class SamplerDeathTest : public testing::TestWithParam<Resampler> {
 protected:
  struct MixParams {
   public:
    std::vector<int16_t> source;
    int64_t source_frames;
    Fixed source_offset;

    std::vector<float> dest;
    int64_t dest_frames;
    int64_t dest_offset;

    bool accum;

   private:
  };

  void SetUp() override {
    auto mixer_type = GetParam();
    ASSERT_TRUE(mixer_type != Resampler::Default)
        << "'Default' resampler type should not be used in these tests";
    ASSERT_TRUE(mixer_type == Resampler::SampleAndHold || mixer_type == Resampler::WindowedSinc)
        << "Unknown resampler type " << static_cast<uint64_t>(mixer_type);

    mixer_ = Mixer::Select(
        {
            .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
            .channels = 1,
            .frames_per_second = 48000,
        },  // source format
        {
            .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
            .channels = 1,
            .frames_per_second = 48000,
        },  // dest format
        mixer_type);
    ASSERT_NE(mixer_, nullptr) << "Mixer could not be created with default parameters";
  }

  static MixParams DefaultMixParams() {
    constexpr int64_t kDefaultSourceLen = 3;
    constexpr int64_t kDefaultDestLen = kDefaultSourceLen;

    return {
        .source = std::vector<int16_t>(kDefaultSourceLen),
        .source_frames = kDefaultSourceLen,
        .source_offset = Fixed(0),

        .dest = std::vector<float>(kDefaultDestLen),
        .dest_frames = kDefaultDestLen,
        .dest_offset = 0,

        .accum = false,
    };
  }

  void MixWithParams(MixParams& mix_params) {
    mixer_->Mix(mix_params.dest.data(), mix_params.dest_frames, &mix_params.dest_offset,
                mix_params.source.data(), mix_params.source_frames, &mix_params.source_offset,
                mix_params.accum);
  }

  std::unique_ptr<Mixer> mixer_;
};

TEST_P(SamplerDeathTest, BaselineShouldSucceed) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  MixWithParams(mix_params);  // don't crash
}

// Incoming dest_offset cannot be negative.
TEST_P(SamplerDeathTest, DestPositionTooLow) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.dest_offset = -1;
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming dest_offset can be just less than, but not equal to, the amount of dest frames.
TEST_P(SamplerDeathTest, DestPositionTooHigh) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.dest_offset = mix_params.dest_frames - 1;
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.dest_offset = mix_params.dest_frames;
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming source_frames can be 1, but cannot be 0.
TEST_P(SamplerDeathTest, SourceFramesTooLow) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_frames = 1;
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_frames = 0;
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming source_offset can be equal to, but not less than, -pos_filter_width().
TEST_P(SamplerDeathTest, SourcePositionTooLow) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_offset = Fixed(0) - mixer_->pos_filter_width();
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_offset = Fixed(0) - mixer_->pos_filter_width() - Fixed::FromRaw(1);
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming source_offset can be equal to, but not more than, the amount of source frames.
TEST_P(SamplerDeathTest, SourcePositionTooHigh) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_offset = Fixed(mix_params.source_frames);
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mix_params.source_offset = Fixed(mix_params.source_frames) + Fixed::FromRaw(1);
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming step_size can be as low as 1 fractional frame, but not zero.
TEST_P(SamplerDeathTest, StepSizeTooLow) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mixer_->bookkeeping().step_size = Fixed::FromRaw(1);
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mixer_->bookkeeping().step_size = Fixed::FromRaw(0);
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Incoming denominator cannot be 0.
TEST_P(SamplerDeathTest, DenominatorTooLow) {
  EXPECT_DEATH(mixer_->bookkeeping().SetRateModuloAndDenominator(0, 0), "");
}

// Incoming numerator cannot equal denominator.
TEST_P(SamplerDeathTest, NumeratorTooHigh) {
  EXPECT_DEATH(mixer_->bookkeeping().SetRateModuloAndDenominator(42, 42), "");
}

// Incoming source_pos_modulo can be just less than, but cannot equal, denominator.
TEST_P(SamplerDeathTest, SourcePosModuloTooHigh) {
  auto mix_params = SamplerDeathTest::DefaultMixParams();
  mixer_->bookkeeping().SetRateModuloAndDenominator(64, 243);
  mixer_->bookkeeping().source_pos_modulo = 242;
  MixWithParams(mix_params);  // (we expect to continue without process exit)

  mix_params = SamplerDeathTest::DefaultMixParams();
  mixer_->bookkeeping().source_pos_modulo = 243;
  EXPECT_DEATH(MixWithParams(mix_params), "");
}

// Display test case names with human-readable labels for the sampler type, instead of an integer.
template <typename TestClass>
std::string PrintResamplerParam(
    const ::testing::TestParamInfo<typename TestClass::ParamType>& info) {
  switch (info.param) {
    case Resampler::SampleAndHold:
      return "Point";
    case Resampler::WindowedSinc:
      return "Sinc";
    case Resampler::Default:
      return "Default";
    default:
      return "Unknown";
  }
}

#define INSTANTIATE_SYNC_TEST_SUITE(_test_class_name)                                            \
  INSTANTIATE_TEST_SUITE_P(DeathTesting, _test_class_name,                                       \
                           ::testing::Values(Resampler::SampleAndHold, Resampler::WindowedSinc), \
                           PrintResamplerParam<_test_class_name>)

INSTANTIATE_SYNC_TEST_SUITE(SamplerDeathTest);

}  // namespace
}  // namespace media::audio::mixer
