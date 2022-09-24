// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <vector>

#include <ffl/string.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ffl/fixed.h"
#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::media::TimelineFunction;
using ::media::TimelineRate;
using ::testing::NotNull;

TEST(SamplerTest, CreateWithUnityRate) {
  const Format source_format = Format::CreateOrDie({SampleType::kInt16, 1, 44100});
  const Format dest_format = Format::CreateOrDie({SampleType::kFloat32, 2, 44100});

  // Default should return a valid `PointSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_LT(default_sampler->pos_filter_length(), Fixed(1));
  EXPECT_LT(default_sampler->neg_filter_length(), Fixed(1));

  // `kSincSampler` should return a valid `SincSampler` although not optimal in practice.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  ASSERT_THAT(sinc_sampler, NotNull());
  EXPECT_GT(sinc_sampler->pos_filter_length(), Fixed(1));
  EXPECT_GT(sinc_sampler->neg_filter_length(), Fixed(1));
}

TEST(SamplerTest, CreateWithNonUnityRate) {
  const Format source_format = Format::CreateOrDie({SampleType::kFloat32, 2, 8000});
  const Format dest_format = Format::CreateOrDie({SampleType::kFloat32, 1, 44100});

  // Default should return a valid `SincSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_GT(default_sampler->pos_filter_length(), Fixed(1));
  EXPECT_GT(default_sampler->neg_filter_length(), Fixed(1));

  // `kSincSampler` should return the same valid `SincSampler` as the default case.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  EXPECT_THAT(sinc_sampler, NotNull());
  EXPECT_GT(sinc_sampler->pos_filter_length(), Fixed(1));
  EXPECT_GT(sinc_sampler->neg_filter_length(), Fixed(1));
}

TEST(SamplerTest, MixSampleSilent) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};

  for (const float source_sample : source_samples) {
    const float scale = 0.5f * source_sample;

    float dest_sample = -0.1f;
    MixSample<GainType::kSilent, false>(source_sample, &dest_sample, scale);
    EXPECT_FLOAT_EQ(dest_sample, 0.0f);

    float dest_sample_to_accumulate = -0.2f;
    MixSample<GainType::kSilent, true>(source_sample, &dest_sample_to_accumulate, scale);
    EXPECT_FLOAT_EQ(dest_sample_to_accumulate, -0.2f);
  }
}

TEST(SamplerTest, MixSampleNonUnityOrRamping) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};
  const std::vector<float> scales = {0.2f, 0.75f, 1.5f};

  const float kDestSampleValue = 0.4f;
  for (const float source_sample : source_samples) {
    for (const float scale : scales) {
      float dest_sample = kDestSampleValue;
      MixSample<GainType::kNonUnity, false>(source_sample, &dest_sample, scale);
      EXPECT_FLOAT_EQ(dest_sample, source_sample * scale);
      dest_sample = kDestSampleValue;
      MixSample<GainType::kRamping, false>(source_sample, &dest_sample, scale);
      EXPECT_FLOAT_EQ(dest_sample, source_sample * scale);

      float dest_sample_to_accumulate = kDestSampleValue;
      MixSample<GainType::kNonUnity, true>(source_sample, &dest_sample_to_accumulate, scale);
      EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample * scale + kDestSampleValue);
      dest_sample_to_accumulate = kDestSampleValue;
      MixSample<GainType::kRamping, true>(source_sample, &dest_sample_to_accumulate, scale);
      EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample * scale + kDestSampleValue);
    }
  }
}

TEST(SamplerTest, MixSampleUnity) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};

  for (const float source_sample : source_samples) {
    const float scale = 0.5f * source_sample;

    float dest_sample = 0.5f;
    MixSample<GainType::kUnity, false>(source_sample, &dest_sample, kUnityGainScale);
    EXPECT_FLOAT_EQ(dest_sample, source_sample);

    float dest_sample_to_accumulate = 2.0f;
    MixSample<GainType::kUnity, true>(source_sample, &dest_sample_to_accumulate, scale);
    EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample + 2.0f);
  }
}

TEST(SamplerStateTest, Defaults) {
  Sampler::State state;
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 0ull);
  EXPECT_EQ(state.step_size_denominator(), 1ull);
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.next_dest_frame(), 0);
  EXPECT_EQ(state.next_source_frame(), 0);
  EXPECT_EQ(state.source_pos_error(), zx::duration(0));
}

TEST(SamplerStateTest, ResetPositions) {
  Sampler::State state;
  EXPECT_EQ(state.next_dest_frame(), 0);
  EXPECT_EQ(state.next_source_frame(), 0);

  state.set_source_pos_modulo(1u);
  state.set_source_pos_error(zx::duration(-777));

  state.ResetPositions(100, TimelineFunction(TimelineRate(17u, 1u)));
  EXPECT_EQ(state.next_dest_frame(), 100);
  EXPECT_EQ(state.next_source_frame(), Fixed::FromRaw(1700));
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.source_pos_error(), zx::duration(0));
}

TEST(SamplerStateTest, ResetSourceStrideScale) {
  Sampler::State state;
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.step_size_denominator(), 1ull);

  // Zero stays zero: `source_pos_modulo` remains 0.
  state.ResetSourceStride(TimelineRate(Fixed(10).raw_value() + 3, 10));
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.step_size_denominator(), 10ull);
  EXPECT_EQ(state.next_source_frame(), Fixed(0));

  // Integer scale: `5/10 => 10/20`
  state.set_source_pos_modulo(5);
  state.ResetSourceStride(TimelineRate(Fixed(20).raw_value() + 7, 20));
  EXPECT_EQ(state.source_pos_modulo(), 10ull);
  EXPECT_EQ(state.step_size_denominator(), 20ull);
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
}

TEST(SamplerStateTest, ResetSourceStrideRound) {
  Sampler::State state;
  state.ResetSourceStride(TimelineRate(Fixed(20).raw_value() + 7, 20));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  state.set_source_pos_modulo(10);

  // Round-up: `10/20 == 8.5/17 => 9/17`.
  state.ResetSourceStride(TimelineRate(Fixed(17).raw_value() + 2, 17));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  EXPECT_EQ(state.source_pos_modulo(), 9ull);
  EXPECT_EQ(state.step_size_denominator(), 17ull);

  // Round-down: `9/17 == 16'000'000'000.41/30'222'222'223 => 16'000'000'000/30'222'222'223`
  state.ResetSourceStride(
      TimelineRate(Fixed(30'222'222'223).raw_value() + 1'234'567'890, 30'222'222'223));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  EXPECT_EQ(state.source_pos_modulo(), 16'000'000'000ull);
  EXPECT_EQ(state.step_size_denominator(), 30'222'222'223ull);
}

TEST(SamplerStateTest, ResetSourceStrideZeroRate) {
  Sampler::State state;
  state.ResetSourceStride(TimelineRate(Fixed(20).raw_value() + 7, 20));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  state.set_source_pos_modulo(10);

  // No change (to `source_pos_modulo` OR `step_size_denominator`): `10/20 => 10/20`.
  state.ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  EXPECT_EQ(state.source_pos_modulo(), 10ull);
  EXPECT_EQ(state.step_size_denominator(), 20ull);
}

TEST(SamplerStateTest, ResetSourceStrideModuloRollover) {
  Sampler::State state;
  state.ResetSourceStride(TimelineRate(Fixed(20).raw_value() + 7, 20));
  EXPECT_EQ(state.next_source_frame(), Fixed(0));
  state.set_source_pos_modulo(19);

  // Round-up: `19/20 == 4.75/5 => 5/5 => 0/5 + Fixed::FromRaw(1)`.
  state.ResetSourceStride(TimelineRate(Fixed(5).raw_value() + 3, 5));
  EXPECT_EQ(state.next_source_frame(), Fixed::FromRaw(1));
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.step_size_denominator(), 5ull);
}

TEST(SamplerStateTest, DestFromSourceLength) {
  Sampler::State state;

  // Integral length and step, no remainder:
  EXPECT_EQ(state.DestFromSourceLength(Fixed(0)), 0);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(1)), 1);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 2);

  state.ResetSourceStride(TimelineRate(Fixed(3).raw_value(), 1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 1);

  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(4)), 2);

  // Integral length and step, with remainder:
  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 2);

  state.ResetSourceStride(TimelineRate(Fixed(4).raw_value(), 1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(9)), 3);

  // Fractional length and step, with remainder:
  state.ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed::FromRaw(1)), 1);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(1) + Fixed::FromRaw(1)), 2);

  state.ResetSourceStride(TimelineRate(Fixed(3).raw_value(), 4));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3) - Fixed::FromRaw(1)), 4);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 4);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3) + Fixed::FromRaw(1)), 5);

  state.ResetSourceStride(TimelineRate(Fixed(9).raw_value(), 8));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) - Fixed::FromRaw(1)), 16);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18)), 16);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) + Fixed::FromRaw(1)), 17);

  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 3));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2) - Fixed::FromRaw(1)), 3);
}

TEST(SamplerStateTest, DestFromSourceLengthLimits) {
  const Fixed max_length = Fixed::Max();

  Sampler::State state;

  // Largest return value.
  state.ResetSourceStride(TimelineRate(1, 1));
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());

  state.ResetSourceStride(
      TimelineRate(std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max()));
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());

  // The largest possible step size is equal to the largest possible length.
  state.ResetSourceStride(TimelineRate(std::numeric_limits<int64_t>::max(), 1));
  EXPECT_EQ(state.DestFromSourceLength(max_length), 1);
}

TEST(SamplerStateTest, SourceFromDestLength) {
  Sampler::State state;

  // Integral step:
  EXPECT_EQ(state.SourceFromDestLength(0), Fixed(0));
  EXPECT_EQ(state.SourceFromDestLength(1), Fixed(1));
  EXPECT_EQ(state.SourceFromDestLength(2), Fixed(2));

  state.ResetSourceStride(TimelineRate(Fixed(3).raw_value(), 1));
  EXPECT_EQ(state.SourceFromDestLength(1), Fixed(3));

  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 1));
  EXPECT_EQ(state.SourceFromDestLength(2), Fixed(4));

  // Fractional step:
  state.ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 2));
  EXPECT_EQ(state.SourceFromDestLength(3), Fixed(1) + ffl::FromRatio(1, 2));

  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 3));
  EXPECT_EQ(state.SourceFromDestLength(1), ffl::FromRatio(2, 3));

  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 3));
  EXPECT_EQ(state.SourceFromDestLength(3), Fixed(2));

  state.ResetSourceStride(TimelineRate(Fixed(9).raw_value(), 8));
  EXPECT_EQ(state.SourceFromDestLength(1), ffl::FromRatio(9, 8));

  state.ResetSourceStride(TimelineRate(Fixed(9).raw_value(), 8));
  EXPECT_EQ(state.SourceFromDestLength(8), Fixed(9));
}

TEST(SamplerStateTest, MonoTimeFromRunningSource) {
  media_audio::Sampler::State state;

  // 44100 Hz stream with +987ppm clock adjustment, started at ~59 sec after bootup.
  state.set_next_source_frame(Fixed(296) + Fixed::FromRaw(306));
  state.ResetSourceStride(TimelineRate(Fixed(78125).raw_value() + 1, 78125));
  state.set_source_pos_modulo(26574);
  EXPECT_EQ(state.MonoTimeFromRunningSource(
                TimelineFunction(0, 59'468'459'010, {441'435'267, 1'220'703'125})),
            zx::time(59'475'165'257));

  // 48000 Hz stream with +4ppm clock adjustment +4ppm, started at ~319 sec after bootup.
  state.set_next_source_frame(Fixed(-743) + Fixed::FromRaw(-1286));
  state.ResetSourceStride(TimelineRate(Fixed(15625).raw_value() + 1, 15625));
  state.set_source_pos_modulo(5627);
  EXPECT_EQ(state.MonoTimeFromRunningSource(
                TimelineFunction(0, 319'214'380'550, {96'000'384, 244'140'625})),
            zx::time(319'198'898'176));

  // 6000 Hz stream with -3ppm clock adjustment, started at ~134 sec after bootup.
  state.set_next_source_frame(Fixed(-143) + Fixed::FromRaw(-3293));
  state.ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));
  state.set_source_pos_modulo(0);
  EXPECT_EQ(state.MonoTimeFromRunningSource(
                TimelineFunction(0, 134'260'312'077, {11'999'964, 244'140'625})),
            zx::time(134'236'411'676));

  // same stream, from a mix 32 millisecs later
  state.set_next_source_frame(Fixed(48) + Fixed::FromRaw(4892));
  state.ResetSourceStride(TimelineRate(Fixed(15625).raw_value() + 1, 15625));
  state.set_source_pos_modulo(15167);
  EXPECT_EQ(state.MonoTimeFromRunningSource(
                TimelineFunction(0, 134'260'312'077, {11'999'964, 244'140'625})),
            zx::time(134'268'411'649));

  // Synthetic example that overflows a int128 if we don't prevent it
  //
  // 191999 Hz (prime) stream with -997 (prime) clock adjustment, stream-start 1 year after bootup,
  // seeked to a running source position of 6e12 frames: about 1 year also.
  //
  // We expect a zx::time that is roughly 2 yrs (now + stream position), more than 6.2e16 nsec.
  // If this particular calculation overflows, the result is positive but approx half the magnitude.
  state.set_next_source_frame(Fixed(6'000'000'000'000) + Fixed::FromRaw(8191));
  state.ResetSourceStride(TimelineRate(1, std::numeric_limits<uint64_t>::max()));
  state.set_source_pos_modulo(std::numeric_limits<uint64_t>::max() - 1);
  EXPECT_GT(state.MonoTimeFromRunningSource(
                TimelineFunction(0, 31'556'736'000'000'000, {191'807'576'997, 122'070'312'500})),
            zx::time(62'838'086'000'000'000));
}

class SamplerStatePositionTest : public testing::Test {
 protected:
  static void TestWithNoRateModulo(bool advance_source_pos_modulo) {
    media_audio::Sampler::State state;
    state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 1));
    state.set_source_pos_modulo(1u);
    state.set_next_source_frame(Fixed(3));
    state.set_source_pos_error(zx::duration(-17));
    state.set_next_dest_frame(2);

    if (advance_source_pos_modulo) {
      state.AdvanceAllPositionsTo(11);
    } else {
      state.UpdateRunningPositionsBy(9);
    }

    // These should be unchanged.
    EXPECT_EQ(state.source_pos_error(), zx::duration(-17));
    EXPECT_EQ(state.source_pos_modulo(), 1u);

    // These should be updated.
    EXPECT_EQ(state.next_dest_frame(), 11u);
    EXPECT_EQ(state.next_source_frame(), Fixed(21))
        << "next_source_frame " << ffl::String::DecRational << state.next_source_frame();
  }

  static void TestWithRateModulo(bool advance_source_pos_modulo) {
    media_audio::Sampler::State state;
    state.ResetSourceStride(TimelineRate(Fixed(5).raw_value() + 2, 5));
    state.set_source_pos_modulo(2u);
    state.set_next_dest_frame(2);
    state.set_next_source_frame(Fixed(3));
    state.set_source_pos_error(zx::duration(-17));

    if (advance_source_pos_modulo) {
      state.AdvanceAllPositionsTo(11);
    } else {
      state.UpdateRunningPositionsBy(9);
    }

    // This should be unchanged.
    EXPECT_EQ(state.source_pos_error(), zx::duration(-17));

    // These should be updated.
    EXPECT_EQ(state.next_dest_frame(), 11u);
    if (advance_source_pos_modulo) {
      // `step_size_modulo / step_size_denominator` is 2/5, so `source_pos_modulo` should increase
      // by (9 * 2), from 2 to 20. `source_pos_modulo / step_size_denominator` is `(20 / 5) = 4`, so
      // source position adds 4 subframes. The remaining `source_pos_modulo` is `(20 % 5) = 0`. Thus
      // new source position should be 12 frames (3+9), 4 subframes, modulo 0/5.
      EXPECT_EQ(state.source_pos_modulo(), 0ull);
    } else {
      // `step_size_modulo / step_size_denominator` is 2/5, so `source_pos_modulo` increased by (9 *
      // 2) and ended up as 2 (22). `source_pos_modulo / step_size_denominator` is `(22 / 5) = 4`,
      // so source position adds 4 subframes. The remaining `source_pos_modulo` is `(22 % 5) = 2`.
      // Thus new source position should be 12 frames (3+9), 4 subframes, modulo 2/5.
      EXPECT_EQ(state.source_pos_modulo(), 2ull);
    }
    EXPECT_EQ(state.next_source_frame(), Fixed(Fixed(12) + Fixed::FromRaw(4)))
        << "next_source_frame " << ffl::String::DecRational << state.next_source_frame();
  }
};

TEST_F(SamplerStatePositionTest, AdvanceAllPositionsWithNoRateModulo) {
  TestWithNoRateModulo(/*advance_source_pos_modulo=*/true);
}
TEST_F(SamplerStatePositionTest, UpdateRunningPositionsWithNoRateModulo) {
  TestWithNoRateModulo(/*advance_source_pos_modulo=*/false);
}

TEST_F(SamplerStatePositionTest, AdvanceAllPositionsWithRateModulo) {
  TestWithRateModulo(/*advance_source_pos_modulo=*/true);
}
TEST_F(SamplerStatePositionTest, UpdateRunningPositionsWithRateModulo) {
  TestWithRateModulo(/*advance_source_pos_modulo=*/false);
}

}  // namespace
}  // namespace media_audio
