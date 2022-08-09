// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio::mixer {
namespace {

class SourceInfoTest : public testing::Test {
 protected:
  void TestPositionAdvanceNoRateModulo(bool advance_source_pos_modulo);
  void TestPositionAdvanceWithRateModulo(bool advance_source_pos_modulo);
  void TestPositionAdvanceNegative(bool advance_source_pos_modulo);
};

TEST_F(SourceInfoTest, Defaults) {
  Mixer::SourceInfo info;

  EXPECT_EQ(info.next_dest_frame(), 0);
  EXPECT_EQ(info.next_source_frame(), 0);
  EXPECT_EQ(info.source_pos_error(), zx::duration(0));
}

// Reset with dest_frame: sets the running dest and frac_src position counters appropriately.
// next_source_frame is set according to dest_to_frac_source transform, source_pos_modulo
// according to rate_modulo and denominator.
TEST_F(SourceInfoTest, ResetPositions) {
  Mixer::Bookkeeping bookkeeping;
  bookkeeping.SetRateModuloAndDenominator(5, 7);

  Mixer::SourceInfo info;
  // All these values will be overwritten
  bookkeeping.set_source_pos_modulo(1u);
  info.set_next_dest_frame(-97);
  info.set_next_source_frame(Fixed(7));
  info.set_source_pos_error(zx::duration(-777));

  info.ResetPositions(100, bookkeeping, TimelineFunction(TimelineRate(17u, 1u)));

  EXPECT_EQ(info.next_dest_frame(), 100);
  // Calculated directly from the TimelineFunction
  EXPECT_EQ(info.next_source_frame(), Fixed::FromRaw(1700));

  // cleared by ResetPositions
  EXPECT_EQ(bookkeeping.source_pos_modulo(), 0ull);
  EXPECT_EQ(info.source_pos_error(), zx::duration(0));
}

// From current values, AdvanceAllPositionsTo advances running positions for dest, source and
// source_modulo to a given dest frame, based on the step_size, rate_modulo and denominator.
void SourceInfoTest::TestPositionAdvanceNoRateModulo(bool advance_source_pos_modulo) {
  Mixer::Bookkeeping bookkeeping;
  bookkeeping.set_step_size(kOneFrame + Fixed::FromRaw(2));
  bookkeeping.SetRateModuloAndDenominator(0, 1);
  bookkeeping.set_source_pos_modulo(1u);

  Mixer::SourceInfo info;
  info.set_next_source_frame(Fixed(3));
  info.set_source_pos_error(zx::duration(-17));
  info.set_next_dest_frame(2);

  if (advance_source_pos_modulo) {
    info.AdvanceAllPositionsTo(11, bookkeeping);
  } else {
    info.UpdateRunningPositionsBy(9, bookkeeping);
  }

  // These should be unchanged
  EXPECT_EQ(info.source_pos_error(), zx::duration(-17));
  EXPECT_EQ(bookkeeping.source_pos_modulo(), 1u);

  // These should be updated
  EXPECT_EQ(info.next_dest_frame(), 11u);  // starts at 2, advance 9
  // Source starts at 3, step_size "1.002", advance by 9 dest, adds 9 frames 18 subframes
  // We expect new source_pos to be 12 frames, 18 subframes.
  EXPECT_EQ(info.next_source_frame(), Fixed(12) + Fixed::FromRaw(18))
      << "next_source_frame " << ffl::String::DecRational << info.next_source_frame();
}

TEST_F(SourceInfoTest, AdvanceAllPositions_NoRateModulo) {
  TestPositionAdvanceNoRateModulo(/* advance_source_pos_modulo = */ true);
}
TEST_F(SourceInfoTest, UpdateRunningPositions_NoRateModulo) {
  TestPositionAdvanceNoRateModulo(/* advance_source_pos_modulo = */ false);
}

void SourceInfoTest::TestPositionAdvanceWithRateModulo(bool advance_source_pos_modulo) {
  Mixer::Bookkeeping bookkeeping;
  bookkeeping.set_step_size(kOneFrame + Fixed::FromRaw(2));
  bookkeeping.SetRateModuloAndDenominator(2, 5);
  bookkeeping.set_source_pos_modulo(2u);

  Mixer::SourceInfo info;
  info.set_next_dest_frame(2);
  info.set_next_source_frame(Fixed(3));
  info.set_source_pos_error(zx::duration(-17));

  if (advance_source_pos_modulo) {
    info.AdvanceAllPositionsTo(11, bookkeeping);
  } else {
    info.UpdateRunningPositionsBy(9, bookkeeping);
  }

  // This should be unchanged
  EXPECT_EQ(info.source_pos_error(), zx::duration(-17));

  // These should be updated
  // Source starts at 3 with position modulo 1/5, step_size "1.002" with rate_modulo 2/5.
  // Advancing by 9 dest frames will add 9 * (1frame + 2subframes) to source_pos
  // (9 frames + 18 subframes), plus any source_pos_modulo effects.
  if (advance_source_pos_modulo) {
    // rate_mod/denom is 2/5, so source_pos_modulo should increase by (9 * 2), from 2 to 20.
    // source_pos_modulo / denominator (20 / 5) is 4, so source_pos adds 4 subframes.
    // The remaining source_pos_modulo (20 % 5) is 0.
    // Thus new source_pos should be 12 frames (3+9), 22 subframes (18+4), modulo 0/5.
    EXPECT_EQ(bookkeeping.source_pos_modulo(), 0ull);
  } else {
    // rate_mod/denom is 2/5, so source_pos_modulo increased by (9 * 2) and ended up as 2 (22).
    // source_pos_modulo / denominator (22 / 5) is 4, so source_pos adds 4 subframes.
    // The remaining source_pos_modulo (22 % 5) is 2.
    // Thus new source_pos should be 12 frames (3+9), 22 subframes (18+4), modulo 2/5.
    EXPECT_EQ(bookkeeping.source_pos_modulo(), 2ull);
  }
  EXPECT_EQ(info.next_dest_frame(), 11u);
  EXPECT_EQ(info.next_source_frame(), Fixed(Fixed(12) + Fixed::FromRaw(22)))
      << "next_source_frame " << ffl::String::DecRational << info.next_source_frame();
}

TEST_F(SourceInfoTest, AdvanceAllPositions_WithRateModulo) {
  TestPositionAdvanceWithRateModulo(/* advance_source_pos_modulo = */ true);
}
TEST_F(SourceInfoTest, UpdateRunningPositions_WithRateModulo) {
  TestPositionAdvanceWithRateModulo(/* advance_source_pos_modulo = */ false);
}

// Validate correct translation to nsec, from running source position including source_pos_modulo
// Test inputs were captured during real-world clock synchronization.
TEST_F(SourceInfoTest, MonotonicNsecFromRunningSource) {
  Mixer::SourceInfo info;

  // 44100 Hz stream with +987ppm clock adjustment, started at ~59 sec after bootup.
  //
  info.set_next_source_frame(Fixed(296) + Fixed::FromRaw(306));
  EXPECT_EQ(info.MonotonicNsecFromRunningSource(
                26574, 78125, TimelineFunction(0, 59'468'459'010, {441'435'267, 1'220'703'125})),
            zx::time(59'475'165'257));

  // 48000 Hz stream with +4ppm clock adjustment +4ppm, started at ~319 sec after bootup.
  //
  info.set_next_source_frame(Fixed(-743) + Fixed::FromRaw(-1286));
  EXPECT_EQ(info.MonotonicNsecFromRunningSource(
                5627, 15625, TimelineFunction(0, 319'214'380'550, {96'000'384, 244'140'625})),
            zx::time(319'198'898'176));

  // 6000 Hz stream with -3ppm clock adjustment, started at ~134 sec after bootup.
  //
  info.set_next_source_frame(Fixed(-143) + Fixed::FromRaw(-3293));
  EXPECT_EQ(info.MonotonicNsecFromRunningSource(
                0, 1, TimelineFunction(0, 134'260'312'077, {11'999'964, 244'140'625})),
            zx::time(134'236'411'676));

  // same stream, from a mix 32 millisecs later
  info.set_next_source_frame(Fixed(48) + Fixed::FromRaw(4892));
  EXPECT_EQ(info.MonotonicNsecFromRunningSource(
                15167, 15625, TimelineFunction(0, 134'260'312'077, {11'999'964, 244'140'625})),
            zx::time(134'268'411'649));

  // Synthetic example that overflows a int128 if we don't prevent it
  //
  // 191999 Hz (prime) stream with -997 (prime) clock adjustment, stream-start 1 year after bootup,
  // seeked to a running source position of 6e12 frames: about 1 year also.
  //
  // We expect a zx::time that is roughly 2 yrs (now + stream position), more than 6.2e16 nsec.
  // If this particular calculation overflows, the result is positive but approx half the magnitude.
  info.set_next_source_frame(Fixed(6'000'000'000'000) + Fixed::FromRaw(8191));
  EXPECT_GT(info.MonotonicNsecFromRunningSource(
                std::numeric_limits<uint64_t>::max() - 1, std::numeric_limits<uint64_t>::max(),
                TimelineFunction(0, 31'556'736'000'000'000, {191'807'576'997, 122'070'312'500})),
            zx::time(62'838'086'000'000'000));
}

}  // namespace
}  // namespace media::audio::mixer
