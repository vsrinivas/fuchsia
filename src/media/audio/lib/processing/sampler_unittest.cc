// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(SamplerTest, CreateWithUnityRate) {
  const Format source_format = Format::CreateOrDie({AudioSampleFormat::kSigned16, 1, 44100});
  const Format dest_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 44100});

  // Default should return a valid `PointSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_EQ(default_sampler->type(), Sampler::Type::kPointSampler);

  // `kPointSampler` should return the same valid `PointSampler` as the default case.
  const auto point_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kPointSampler);
  ASSERT_THAT(point_sampler, NotNull());
  EXPECT_EQ(point_sampler->type(), Sampler::Type::kPointSampler);

  // `kSincSampler` should return a valid `SincSampler` although not optimal in practice.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  ASSERT_THAT(sinc_sampler, NotNull());
  EXPECT_EQ(sinc_sampler->type(), Sampler::Type::kSincSampler);
}

TEST(SamplerTest, CreateWithNonUnityRate) {
  const Format source_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 8000});
  const Format dest_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 1, 44100});

  // Default should return a valid `SincSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_EQ(default_sampler->type(), Sampler::Type::kSincSampler);

  // `kPointSampler` should return `nullptr` since `PointSampler` is only supported for unity rates.
  const auto point_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kPointSampler);
  EXPECT_THAT(point_sampler, IsNull());

  // `kSincSampler` should return the same valid `SincSampler` as the default case.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  EXPECT_THAT(sinc_sampler, NotNull());
  EXPECT_EQ(sinc_sampler->type(), Sampler::Type::kSincSampler);
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

constexpr Fixed kTestSourcePos = Fixed(123) + Fixed::FromRaw(4567);

TEST(SamplerStateTest, Defaults) {
  Sampler::State state;
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.rate_modulo(), 0ull);
  EXPECT_EQ(state.denominator(), 1ull);
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
}

TEST(SamplerStateTest, SetRateModuloAndDenominatorScale) {
  Sampler::State state;
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.denominator(), 1ull);

  // Zero stays zero: `source_pos_modulo` remains 0.
  auto source_pos = state.SetRateModuloAndDenominator(3, 10);
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.denominator(), 10ull);
  EXPECT_EQ(source_pos, Fixed(0));

  // Integer scale: `5/10 => 10/20`
  state.set_source_pos_modulo(5);
  source_pos = state.SetRateModuloAndDenominator(7, 20, kTestSourcePos);
  EXPECT_EQ(state.source_pos_modulo(), 10ull);
  EXPECT_EQ(state.denominator(), 20ull);
  EXPECT_EQ(source_pos, kTestSourcePos);
}

TEST(SamplerStateTest, SetRateModuloAndDenominatorRound) {
  Sampler::State state;
  auto source_pos = state.SetRateModuloAndDenominator(7, 20);
  EXPECT_EQ(source_pos, Fixed(0));
  state.set_source_pos_modulo(10);

  // Round-up: `10/20 == 8.5/17 => 9/17`.
  source_pos = state.SetRateModuloAndDenominator(2, 17, kTestSourcePos);
  EXPECT_EQ(state.source_pos_modulo(), 9ull);
  EXPECT_EQ(state.denominator(), 17ull);
  EXPECT_EQ(source_pos, kTestSourcePos);

  // Round-down: `9/17 == 16'000'000'000.41/30'222'222'223 => 16'000'000'000/30'222'222'223`
  source_pos = state.SetRateModuloAndDenominator(1'234'567'890, 30'222'222'223, kTestSourcePos);
  EXPECT_EQ(state.source_pos_modulo(), 16'000'000'000ull);
  EXPECT_EQ(state.denominator(), 30'222'222'223ull);
  EXPECT_EQ(source_pos, kTestSourcePos);
}

TEST(SamplerStateTest, SetRateModuloAndDenominatorZeroRate) {
  Sampler::State state;
  auto source_pos = state.SetRateModuloAndDenominator(7, 20, kTestSourcePos);
  EXPECT_EQ(source_pos, kTestSourcePos);
  state.set_source_pos_modulo(10);

  source_pos = state.SetRateModuloAndDenominator(0, 1);
  // No change (to `source_pos_modulo` OR `denominator`): `10/20 => 10/20`.
  EXPECT_EQ(state.source_pos_modulo(), 10ull);
  EXPECT_EQ(state.denominator(), 20ull);
  EXPECT_EQ(source_pos, Fixed(0));
}

TEST(SamplerStateTest, SetRateModuloAndDenominatorModuloRollover) {
  Sampler::State state;
  auto source_pos = state.SetRateModuloAndDenominator(7, 20);
  EXPECT_EQ(source_pos, Fixed(0));
  state.set_source_pos_modulo(19);

  source_pos = state.SetRateModuloAndDenominator(3, 5, kTestSourcePos);
  // Round-up: `19/20 == 4.75/5 => 5/5 => 0/5 + Fixed::FromRaw(1)`.
  EXPECT_EQ(state.source_pos_modulo(), 0ull);
  EXPECT_EQ(state.denominator(), 5ull);
  EXPECT_EQ(source_pos, Fixed(kTestSourcePos + Fixed::FromRaw(1)));
}

TEST(SamplerStateTest, DestFromSourceLengthNoModulo) {
  Sampler::State state;

  // Integral length and step, no remainder:
  EXPECT_EQ(state.DestFromSourceLength(Fixed(0)), 0);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(1)), 1);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 2);

  state.set_step_size(Fixed(3));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 1);

  state.set_step_size(Fixed(2));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(4)), 2);

  // Integral length and step, with remainder:
  state.set_step_size(Fixed(2));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 2);

  state.set_step_size(Fixed(4));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(9)), 3);

  // Fractional length and step, with remainder:
  state.set_step_size(Fixed(1));
  EXPECT_EQ(state.DestFromSourceLength(Fixed::FromRaw(1)), 1);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(1) + Fixed::FromRaw(1)), 2);

  state.set_step_size(ffl::FromRatio(3, 4));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3) - Fixed::FromRaw(1)), 4);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 4);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3) + Fixed::FromRaw(1)), 5);

  state.set_step_size(ffl::FromRatio(9, 8));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) - Fixed::FromRaw(1)), 16);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18)), 16);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) + Fixed::FromRaw(1)), 17);

  // Ideally, this would result in 3 steps needed, but step_size was reduced to `Fixed::FromRaw(1)`
  // precision and thus is slightly less than a perfect 2/3, so 3 steps is _just_ short.
  state.set_step_size(ffl::FromRatio(2, 3));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 4);
  // _Just_ short by exactly one fractional frame, in fact.
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2) - Fixed::FromRaw(1)), 3);
}

TEST(SamplerStateTest, DestFromSourceLengthWithModulo) {
  Sampler::State state;

  // `source_pos_modulo` but no `rate_modulo`, where the initial `source_pos_modulo` should be
  // entirely ignored.
  state.set_step_size(ffl::FromRatio(3, 4));
  state.SetRateModuloAndDenominator(0, 21, Fixed(20));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(3)), 4);

  state.set_step_size(ffl::FromRatio(9, 8));
  state.SetRateModuloAndDenominator(0, 999, Fixed(998));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18)), 16);

  // `rate_modulo` adds up to just one unit shy of rolling over (initial mod is 0 or unspecified).
  state.set_step_size(ffl::FromRatio(2, 3));
  state.SetRateModuloAndDenominator(33, 100, Fixed(0));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 4);

  // `rate_modulo` adds up to just one unit shy of rolling over (non-zero initial mod is specified).
  state.set_step_size(ffl::FromRatio(2, 3));
  state.SetRateModuloAndDenominator(32, 100, Fixed(6));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 4);

  // `rate_modulo` exactly rolls over (no or 0 initial_mod).
  state.set_step_size(ffl::FromRatio(2, 3));
  state.SetRateModuloAndDenominator(1, 3, Fixed(0));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 3);

  state.set_step_size(ffl::FromRatio(9, 8));
  state.SetRateModuloAndDenominator(1, 16, Fixed(0));
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) + Fixed::FromRaw(1)), 16);

  // `rate_modulo` exactly rolls over (non-zero initial mod).
  state.set_step_size(ffl::FromRatio(2, 3));
  state.SetRateModuloAndDenominator(33, 100);
  state.set_source_pos_modulo(1);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 3);

  state.set_step_size(ffl::FromRatio(2, 3));
  state.SetRateModuloAndDenominator(31, 100);
  state.set_source_pos_modulo(7);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(2)), 3);

  state.set_step_size(ffl::FromRatio(9, 8));
  state.SetRateModuloAndDenominator(1, 32);
  state.set_source_pos_modulo(16);
  EXPECT_EQ(state.DestFromSourceLength(Fixed(18) + Fixed::FromRaw(1)), 16);
}

TEST(SamplerStateTest, DestFromSourceLengthLimits) {
  const Fixed max_length = Fixed::Max();
  const Fixed max_step_size = Fixed::Max();
  const uint64_t max_denominator = std::numeric_limits<uint64_t>::max();
  const uint64_t max_rate_modulo = max_denominator - 1u;

  const Fixed min_step_size = Fixed::FromRaw(1);
  const uint64_t min_nonzero_rate_modulo = 1u;

  Sampler::State state;

  // Largest return value without modulo factors.
  state.set_step_size(min_step_size);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());
  // Largest return value with modulo factors.
  state.SetRateModuloAndDenominator(min_nonzero_rate_modulo, max_denominator);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());

  // The largest possible step size is equal to the largest possible length.
  state.set_step_size(max_step_size);
  state.SetRateModuloAndDenominator(0, 1);
  EXPECT_EQ(state.DestFromSourceLength(max_length), 1);
  state.SetRateModuloAndDenominator(max_rate_modulo, max_denominator, Fixed(max_denominator - 1u));
  EXPECT_EQ(state.DestFromSourceLength(max_length), 1);

  // The largest possible `rate_modulo`/`source_pos_modulo` contribution, relative to `step_size`.
  state.set_step_size(min_step_size);
  state.SetRateModuloAndDenominator(max_rate_modulo, max_denominator);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max() / 2 + 1);
}

TEST(SamplerTest, DestFromSourceLengthPosModuloContribution) {
  const Fixed max_length = Fixed::Max();
  const Fixed min_step_size = Fixed::FromRaw(1);

  const uint64_t max_denominator = std::numeric_limits<uint64_t>::max();
  const uint64_t large_denominator = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());

  const uint64_t max_rate_modulo = max_denominator - 1u;
  const uint64_t min_nonzero_rate_modulo = 1u;

  const uint64_t large_source_pos_modulo = large_denominator + 2u;

  Sampler::State state;

  // With smaller denominator, `rate_modulo` contributes 1 frac frame which reduces steps by 1.
  state.set_step_size(min_step_size);
  state.SetRateModuloAndDenominator(min_nonzero_rate_modulo, large_denominator);
  state.set_source_pos_modulo(1);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max() - 1);

  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  state.set_source_pos_modulo(0);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());

  // This exact large initial position modulo ultimately reduces steps by 1.
  state.SetRateModuloAndDenominator(min_nonzero_rate_modulo, max_denominator);
  state.set_source_pos_modulo(large_source_pos_modulo);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max() - 1);

  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  state.set_source_pos_modulo(large_source_pos_modulo - 1);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max());

  // Very small `step_size` and large `rate_modulo` where `source_pos_modulo` exactly makes the
  // difference, starting values:
  //    length 7FFFFFFF'FFFFFFFF, `step_size` 0'00000001, `rate_modulo`/`denominator` FF..FB/FF..FF,
  //    `source_pos_modulo` 1
  // After 40..00 steps:
  // *  `step_size` contributes 40..00
  //     `rate_modulo` advances 3F..FE'C0..00 / FF..FF, which == 3F..FE
  // *   `rate_modulo`contributes 3F..FE
  //     `source_pos_modulo` advances by 3F..FE'C0..00 % FF..FF, which == FF..FE (/ FF..FF)
  //     ... plus initial modulo 1 (/ FF..FF), to exactly equal FF..FF / FF..FF
  // *   `source_pos_modulo` contributes 1.
  //
  // 40..00 + 3F..FE + 1 == 7F..FF, exactly the length we needed to cover.
  state.SetRateModuloAndDenominator(max_rate_modulo - 3, max_denominator);
  state.set_source_pos_modulo(1);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max() / 2 + 1);

  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  state.set_source_pos_modulo(0);
  EXPECT_EQ(state.DestFromSourceLength(max_length), std::numeric_limits<int64_t>::max() / 2 + 2);
}

TEST(SamplerStateTest, SourceFromDestLength) {
  Sampler::State state;
  state.set_step_size(Fixed(2) + Fixed::FromRaw(11));

  // No `rate_modulo`:
  //    `3 * (2+11/8192 + (0/7)/8192) + (6/7)/8192 == 6+33/8192 + (6/7)/8192 == 6+33/8192`
  state.SetRateModuloAndDenominator(0, 7, Fixed(6));
  EXPECT_EQ(state.SourceFromDestLength(3), Fixed(Fixed(6) + Fixed::FromRaw(33)));

  // `source_pos_modulo` almost rolls over:
  //    `3 * (2+11/8192 + (5/7)/8192) + (5/7)/8192 == 6+33/8192 + (20/7)/8192 == 6+35/8192`
  state.SetRateModuloAndDenominator(5, 7);
  state.set_source_pos_modulo(5);
  EXPECT_EQ(state.SourceFromDestLength(3), Fixed(Fixed(6) + Fixed::FromRaw(35)));

  // `source_pos_modulo` exacly rolls over:
  //    `3 * (2+11/8192 + (5/7)/8192) + (6/7)/8192 == 6+33/8192 + (21/7)/8192 == 6+36/8192`
  state.SetRateModuloAndDenominator(5, 7);
  state.set_source_pos_modulo(6);
  EXPECT_EQ(state.SourceFromDestLength(3), Fixed(Fixed(6) + Fixed::FromRaw(36)));
}

}  // namespace
}  // namespace media_audio
