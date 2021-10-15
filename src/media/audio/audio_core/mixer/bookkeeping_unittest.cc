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

  bool Mix(float*, int64_t, int64_t*, const void*, int64_t, Fixed*, bool) final { return false; }
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

// Validate the calculations that do not use rate_modulo etc.
TEST(BookkeepingTest, StepsNeededForDelta_NoModulo) {
  Mixer::Bookkeeping bookkeeping;

  // integral delta and step, no remainder
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(0), Fixed(1), 0, 1, 0), 0);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(1), Fixed(1), 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), Fixed(1), 0, 1, 0), 2);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3), Fixed(3), 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(4), Fixed(2), 0, 1, 0), 2);

  // integral delta and step, w/remainder
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3), Fixed(2), 0, 1, 0), 2);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(9), Fixed(4), 0, 1, 0), 3);

  // fractional delta and step, w/remainder
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed::FromRaw(1), Fixed(1), 0, 1, 0), 1);
  EXPECT_EQ(
      Mixer::Bookkeeping::StepsNeededForDelta(Fixed(1) + Fixed::FromRaw(1), Fixed(1), 0, 1, 0), 2);

  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3) - Fixed::FromRaw(1),
                                                    ffl::FromRatio(3, 4), 0, 1, 0),
            4);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3), ffl::FromRatio(3, 4), 0, 1, 0), 4);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3) + Fixed::FromRaw(1),
                                                    ffl::FromRatio(3, 4), 0, 1, 0),
            5);

  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(18) - Fixed::FromRaw(1),
                                                    ffl::FromRatio(9, 8), 0, 1, 0),
            16);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(18), ffl::FromRatio(9, 8), 0, 1, 0), 16);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                    ffl::FromRatio(9, 8), 0, 1, 0),
            17);

  // Ideally, this would result in 3 steps needed, but step_size was reduced to Fixed::FromRaw(1)
  // precision and thus is slightly less than a perfect 2/3, so 3 steps is _just_ short.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 0, 1, 0), 4);
  // _Just_ short by exactly one fractional frame, in fact.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2) - Fixed::FromRaw(1),
                                                    ffl::FromRatio(2, 3), 0, 1, 0),
            3);
}

// Validate the calculations that use rate_modulo and denominator
// These are needed for example to perfectly capture a 2/3 step_size.
TEST(BookkeepingTest, StepsNeededForDelta_WithModulo) {
  Mixer::Bookkeeping bookkeeping;
  // pos_modulo but no rate_modulo. initial_source_pos_modulo should be entirely ignored, so
  // these should reduce to  3 / (3/4)  and  18 / (9/8)  -- or exactly 4 and 16.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(3), ffl::FromRatio(3, 4), 0, 21, 20), 4);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(18), ffl::FromRatio(9, 8), 0, 999, 998),
            16);

  // rate_modulo adds up to just one unit shy of rolling over (initial mod is 0 or unspecified)
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 33, 100, 0), 4);

  // rate_modulo adds up to just one unit shy of rolling over (non-zero initial mod is specified)
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 31, 100, 6), 4);

  // rate_modulo exactly rolls over (no or 0 initial_mod)
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 1, 3, 0), 3);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                    ffl::FromRatio(9, 8), 1, 16, 0),
            16);

  // rate_modulo exactly rolls over (non-zero initial mod)
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 33, 100, 1), 3);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(2), ffl::FromRatio(2, 3), 31, 100, 7), 3);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(Fixed(Fixed(18) + Fixed::FromRaw(1)),
                                                    ffl::FromRatio(9, 8), 1, 32, 16),
            16);
}

// Validate the limiting parameters of this calculation -- combinations of the max and min inputs.
TEST(BookkeepingTest, StepsNeededForDelta_Limits) {
  Mixer::Bookkeeping bookkeeping;

  auto max_delta = Fixed::Max();
  auto max_step_size = Fixed::Max();
  auto max_denom = std::numeric_limits<uint64_t>::max();
  auto max_rate_mod = max_denom - 1u;

  auto min_step_size = Fixed::FromRaw(1);
  auto min_nonzero_rate_mod = 1u;
  auto max_initial_pos_mod = max_denom - 1u;

  // Largest return value without modulo factors
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, 0, 1, 0),
            std::numeric_limits<int64_t>::max());

  // Largest return value with modulo factors
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, min_nonzero_rate_mod,
                                                    max_denom, 0),
            std::numeric_limits<int64_t>::max());

  // The largest possible step_size is equal to the largest possible delta
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, max_step_size, 0, 1, 0), 1);
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, max_step_size, max_rate_mod,
                                                    max_denom, max_initial_pos_mod),
            1);

  // This is the largest possible rate_mod/initial_pos_mod contribution, relative to step_size
  EXPECT_EQ(
      Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, max_rate_mod, max_denom, 0),
      std::numeric_limits<int64_t>::max() / 2 + 1);
}

// Validate initial_pos_modulo's precise contribution: exactly what is needed, versus one less
TEST(BookkeepingTest, StepsNeededForDelta_PosModuloContribution) {
  Mixer::Bookkeeping bookkeeping;

  auto max_delta = Fixed::Max();
  auto min_step_size = Fixed::FromRaw(1);

  auto max_denom = std::numeric_limits<uint64_t>::max();
  auto large_denom = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());

  auto max_rate_mod = max_denom - 1u;
  auto min_nonzero_rate_mod = 1u;

  auto large_initial_pos_mod = large_denom + 2u;

  // With smaller denominator, rate_mod contributes 1 frac frame which reduces steps by 1.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, min_nonzero_rate_mod,
                                                    large_denom, 1),
            std::numeric_limits<int64_t>::max() - 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, min_nonzero_rate_mod,
                                                    large_denom, 0),
            std::numeric_limits<int64_t>::max());

  // This exact large initial position modulo ultimately reduces steps by 1.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, min_nonzero_rate_mod,
                                                    max_denom, large_initial_pos_mod),
            std::numeric_limits<int64_t>::max() - 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, min_nonzero_rate_mod,
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
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, max_rate_mod - 3,
                                                    max_denom, 1),
            std::numeric_limits<int64_t>::max() / 2 + 1);
  // ...at just 1 initial position modulo less, we require an additional step to cover the delta.
  EXPECT_EQ(Mixer::Bookkeeping::StepsNeededForDelta(max_delta, min_step_size, max_rate_mod - 3,
                                                    max_denom, 0),
            std::numeric_limits<int64_t>::max() / 2 + 2);
}

}  // namespace
}  // namespace media::audio::mixer
