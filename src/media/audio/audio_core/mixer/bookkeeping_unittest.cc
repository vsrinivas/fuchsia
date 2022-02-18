// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <limits>

#include <ffl/fixed.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {
namespace {

class StubMixer : public Mixer {
 public:
  StubMixer() : Mixer(Fixed(0), Fixed(0), Gain::Limits{}) {}

  void Mix(float*, int64_t, int64_t*, const void*, int64_t, Fixed*, bool) final {}
};

TEST(BookkeepingTest, Defaults) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();

  EXPECT_EQ(bookkeeping.step_size, kOneFrame);
  EXPECT_EQ(bookkeeping.rate_modulo(), 0ull);
  EXPECT_EQ(bookkeeping.denominator(), 1ull);
  EXPECT_EQ(bookkeeping.source_pos_modulo, 0ull);

  EXPECT_TRUE(bookkeeping.gain.IsUnity());
  EXPECT_FALSE(bookkeeping.gain.IsSilent());
  EXPECT_FALSE(bookkeeping.gain.IsRamping());
}

constexpr Fixed kTestSourcePos = Fixed(123) + Fixed::FromRaw(4567);
// Validate the scaling of source_pos_modulo, when setting a new rate_modulo and denominator
// During source_pos_modulo-related tests, the non-zero values of rate_modulo do not matter.
TEST(BookkeepingTest, SetRateModuloAndDenominatorScale) {
  StubMixer mixer;
  auto& bk = mixer.bookkeeping();
  EXPECT_EQ(bk.source_pos_modulo, 0ull);
  EXPECT_EQ(bk.denominator(), 1ull);

  // Zero stays zero: source_pos_modulo remains 0.
  auto source_pos = bk.SetRateModuloAndDenominator(3, 10);
  EXPECT_EQ(bk.source_pos_modulo, 0ull);
  EXPECT_EQ(bk.denominator(), 10ull);
  EXPECT_EQ(source_pos, Fixed(0));

  // Integer scale: 5/10 => 10/20
  bk.source_pos_modulo = 5;
  source_pos = bk.SetRateModuloAndDenominator(7, 20, kTestSourcePos);
  EXPECT_EQ(bk.source_pos_modulo, 10ull);
  EXPECT_EQ(bk.denominator(), 20ull);
  EXPECT_EQ(source_pos, kTestSourcePos);
}

// Validate the rounding of fractional source_pos_modulo, after scaling.
TEST(BookkeepingTest, SetRateModuloAndDenominatorRound) {
  StubMixer mixer;
  auto& bk = mixer.bookkeeping();
  auto source_pos = bk.SetRateModuloAndDenominator(7, 20);
  EXPECT_EQ(source_pos, Fixed(0));
  bk.source_pos_modulo = 10;

  // Round-up: 10/20 == 8.5/17 => 9/17.
  source_pos = bk.SetRateModuloAndDenominator(2, 17, kTestSourcePos);
  EXPECT_EQ(bk.source_pos_modulo, 9ull);
  EXPECT_EQ(bk.denominator(), 17ull);
  EXPECT_EQ(source_pos, kTestSourcePos);

  // Round-down: 9/17 == 16'000'000'000.41/30'222'222'223 => 16'000'000'000/30'222'222'223
  source_pos = bk.SetRateModuloAndDenominator(1'234'567'890, 30'222'222'223, kTestSourcePos);
  EXPECT_EQ(bk.source_pos_modulo, 16'000'000'000ull);
  EXPECT_EQ(bk.denominator(), 30'222'222'223ull);
  EXPECT_EQ(source_pos, kTestSourcePos);
}

// Validate that source_pos_modulo and denominator are both unchanged, if new rate_modulo is zero.
TEST(BookkeepingTest, SetRateModuloAndDenominatorZeroRate) {
  StubMixer mixer;
  auto& bk = mixer.bookkeeping();
  auto source_pos = bk.SetRateModuloAndDenominator(7, 20, kTestSourcePos);
  EXPECT_EQ(source_pos, kTestSourcePos);
  bk.source_pos_modulo = 10;

  source_pos = bk.SetRateModuloAndDenominator(0, 1);
  // No change (to source_pos_modulo OR denominator): 10/20 => 10/20.
  EXPECT_EQ(bk.source_pos_modulo, 10ull);
  EXPECT_EQ(bk.denominator(), 20ull);
  EXPECT_EQ(source_pos, Fixed(0));
}

// Validate that an incremented source_pos is returned, when source_pos_modulo rolls over.
TEST(BookkeepingTest, SetRateModuloAndDenominatorModuloRollover) {
  StubMixer mixer;
  auto& bk = mixer.bookkeeping();
  auto source_pos = bk.SetRateModuloAndDenominator(7, 20);
  EXPECT_EQ(source_pos, Fixed(0));
  bk.source_pos_modulo = 19;

  source_pos = bk.SetRateModuloAndDenominator(3, 5, kTestSourcePos);
  // Round-up: 19/20 == 4.75/5 => 5/5 => 0/5+Fixed::FromRaw(1).
  EXPECT_EQ(bk.source_pos_modulo, 0ull);
  EXPECT_EQ(bk.denominator(), 5ull);
  EXPECT_EQ(source_pos, Fixed(kTestSourcePos + Fixed::FromRaw(1)));
}

// Validate the calculations that do not use rate_modulo etc.
TEST(BookkeepingTest, SourceLenToDestLen_NoModulo) {
  // integral delta and step, no remainder
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(0), Fixed(1), 0, 1, 0), 0);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(1), Fixed(1), 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), Fixed(1), 0, 1, 0), 2);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3), Fixed(3), 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(4), Fixed(2), 0, 1, 0), 2);

  // integral delta and step, w/remainder
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3), Fixed(2), 0, 1, 0), 2);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(9), Fixed(4), 0, 1, 0), 3);

  // fractional delta and step, w/remainder
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed::FromRaw(1), Fixed(1), 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(1) + Fixed::FromRaw(1), Fixed(1), 0, 1, 0),
            2);

  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3) - Fixed::FromRaw(1),
                                                   ffl::FromRatio(3, 4), 0, 1, 0),
            4);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3), ffl::FromRatio(3, 4), 0, 1, 0), 4);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3) + Fixed::FromRaw(1),
                                                   ffl::FromRatio(3, 4), 0, 1, 0),
            5);

  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(18) - Fixed::FromRaw(1),
                                                   ffl::FromRatio(9, 8), 0, 1, 0),
            16);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(18), ffl::FromRatio(9, 8), 0, 1, 0), 16);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                   ffl::FromRatio(9, 8), 0, 1, 0),
            17);

  // Ideally, this would result in 3 steps needed, but step_size was reduced to Fixed::FromRaw(1)
  // precision and thus is slightly less than a perfect 2/3, so 3 steps is _just_ short.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 0, 1, 0), 4);
  // _Just_ short by exactly one fractional frame, in fact.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2) - Fixed::FromRaw(1),
                                                   ffl::FromRatio(2, 3), 0, 1, 0),
            3);
}

// Validate the calculations that use rate_modulo and denominator
// These are needed for example to perfectly capture a 2/3 step_size.
TEST(BookkeepingTest, SourceLenToDestLen_WithModulo) {
  // pos_modulo but no rate_modulo. initial_source_pos_modulo should be entirely ignored, so
  // these should reduce to  3 / (3/4)  and  18 / (9/8)  -- or exactly 4 and 16.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(3), ffl::FromRatio(3, 4), 0, 21, 20), 4);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(18), ffl::FromRatio(9, 8), 0, 999, 998),
            16);

  // rate_modulo adds up to just one unit shy of rolling over (initial mod is 0 or unspecified)
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 33, 100, 0), 4);

  // rate_modulo adds up to just one unit shy of rolling over (non-zero initial mod is specified)
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 31, 100, 6), 4);

  // rate_modulo exactly rolls over (no or 0 initial_mod)
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 1, 3, 0), 3);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                   ffl::FromRatio(9, 8), 1, 16, 0),
            16);

  // rate_modulo exactly rolls over (non-zero initial mod)
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 33, 100, 1), 3);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(2), ffl::FromRatio(2, 3), 31, 100, 7), 3);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                   ffl::FromRatio(9, 8), 1, 32, 16),
            16);
}

// Validate the limiting parameters of this calculation -- combinations of the max and min inputs.
TEST(BookkeepingTest, SourceLenToDestLen_Limits) {
  auto max_delta = Fixed::Max();
  auto max_step_size = Fixed::Max();
  auto max_denom = std::numeric_limits<uint64_t>::max();
  auto max_rate_mod = max_denom - 1u;

  auto min_step_size = Fixed::FromRaw(1);
  auto min_nonzero_rate_mod = 1u;
  auto max_initial_pos_mod = max_denom - 1u;

  // Largest return value without modulo factors
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, 0, 1, 0),
            std::numeric_limits<int64_t>::max());

  // Largest return value with modulo factors
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, min_nonzero_rate_mod,
                                                   max_denom, 0),
            std::numeric_limits<int64_t>::max());

  // The largest possible step_size is equal to the largest possible delta
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, max_step_size, 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, max_step_size, max_rate_mod,
                                                   max_denom, max_initial_pos_mod),
            1);

  // This is the largest possible rate_mod/initial_pos_mod contribution, relative to step_size
  EXPECT_EQ(
      Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, max_rate_mod, max_denom, 0),
      std::numeric_limits<int64_t>::max() / 2 + 1);
}

// Validate initial_pos_modulo's precise contribution: exactly what is needed, versus one less
TEST(BookkeepingTest, SourceLenToDestLen_PosModuloContribution) {
  auto max_delta = Fixed::Max();
  auto min_step_size = Fixed::FromRaw(1);

  auto max_denom = std::numeric_limits<uint64_t>::max();
  auto large_denom = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());

  auto max_rate_mod = max_denom - 1u;
  auto min_nonzero_rate_mod = 1u;

  auto large_initial_pos_mod = large_denom + 2u;

  // With smaller denominator, rate_mod contributes 1 frac frame which reduces steps by 1.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, min_nonzero_rate_mod,
                                                   large_denom, 1),
            std::numeric_limits<int64_t>::max() - 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, min_nonzero_rate_mod,
                                                   large_denom, 0),
            std::numeric_limits<int64_t>::max());

  // This exact large initial position modulo ultimately reduces steps by 1.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, min_nonzero_rate_mod,
                                                   max_denom, large_initial_pos_mod),
            std::numeric_limits<int64_t>::max() - 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, min_nonzero_rate_mod,
                                                   max_denom, large_initial_pos_mod - 1),
            std::numeric_limits<int64_t>::max());

  // Very small step_size and large rate_mod where initial_pos_mod exactly makes the difference:
  // Starting values:
  //     delta 7FFFFFFF'FFFFFFFF, step_size 0'00000001, rate_mod/denom FF..FB/FF..FF, init_mod 1
  // After 40..00 steps:
  // +   step_size contributes 40..00
  //
  //     rate_mod advances 3F..FE'C0..00 / FF..FF, which == 3F..FE
  // +   rate_mod contributes 3F..FE
  //
  //     pos_modulo advances by 3F..FE'C0..00 % FF..FF, which == FF..FE (/ FF..FF)
  //     ... plus initial_modulo 1 (/ FF..FF), to exactly equal FF..FF / FF..FF
  // +   pos_modulo contributes 1.
  //
  // 40..00 + 3F..FE + 1 == 7F..FF, exactly the delta we needed to cover.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, max_rate_mod - 3,
                                                   max_denom, 1),
            std::numeric_limits<int64_t>::max() / 2 + 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::SourceLenToDestLen(max_delta, min_step_size, max_rate_mod - 3,
                                                   max_denom, 0),
            std::numeric_limits<int64_t>::max() / 2 + 2);
}

// Verify that we correctly calculate the equivalent source length for a given dest frame length,
// using step_size and (rate_modulo, denominator, initial_source_pos_modulo).
TEST(BookkeepingTest, DestLenToSourceLen) {
  // Test the no-rate-modulo case
  // 3 * (2+11/8192 + (0/7)/8192) + (6/7)/8192 == 6+33/8192 + (6/7)/8192 == 6+33/8192.
  EXPECT_EQ(Mixer::Bookkeeping::DestLenToSourceLen(3, Fixed(2) + Fixed::FromRaw(11), 0, 7, 6),
            Fixed(Fixed(6) + Fixed::FromRaw(33)));

  // Test the source_pos_modulo-almost-rolls-over case.
  // 3 * (2+11/8192 + (5/7)/8192) + (5/7)/8192 == 6+33/8192 + (20/7)/8192 == 6+35/8192.
  EXPECT_EQ(Mixer::Bookkeeping::DestLenToSourceLen(3, Fixed(2) + Fixed::FromRaw(11), 5, 7, 5),
            Fixed(Fixed(6) + Fixed::FromRaw(35)));

  // Test the source_pos_modulo-exactly-rolls-over case.
  // 3 * (2+11/8192 + (5/7)/8192) + (6/7)/8192 == 6+33/8192 + (21/7)/8192 == 6+36/8192.
  EXPECT_EQ(Mixer::Bookkeeping::DestLenToSourceLen(3, Fixed(2) + Fixed::FromRaw(11), 5, 7, 6),
            Fixed(Fixed(6) + Fixed::FromRaw(36)));
}

}  // namespace
}  // namespace media::audio::mixer
