// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ffl/string.h"
#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {
namespace {

class StubMixer : public Mixer {
 public:
  StubMixer() : Mixer(Fixed(0), Fixed(0), Gain::Limits{}) {}

  bool Mix(float*, int64_t, int64_t*, const void*, int64_t, Fixed*, bool) final { return false; }
};

class SourceInfoTest : public testing::Test {
 protected:
  void TestPositionAdvanceNoRateModulo(bool advance_source_pos_modulo);
  void TestPositionAdvanceWithRateModulo(bool advance_source_pos_modulo);
  void TestPositionAdvanceNegative(bool advance_source_pos_modulo);

  StubMixer mixer_;
};

TEST_F(SourceInfoTest, Defaults) {
  auto& info = mixer_.source_info();

  EXPECT_EQ(info.next_dest_frame, 0);
  EXPECT_EQ(info.next_source_frame, 0);
  EXPECT_EQ(info.source_pos_error, zx::duration(0));

  EXPECT_EQ(info.dest_frames_to_frac_source_frames.subject_time(), 0);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.reference_time(), 0);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.subject_delta(), 0u);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.reference_delta(), 1u);

  EXPECT_EQ(info.clock_mono_to_frac_source_frames.subject_time(), 0);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.reference_time(), 0);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.subject_delta(), 0u);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.reference_delta(), 1u);
}

// Reset with dest_frame: sets the running dest and frac_src position counters appropriately.
// next_source_frame is set according to dest_to_frac_source transform, source_pos_modulo
// according to rate_modulo and denominator.
TEST_F(SourceInfoTest, ResetPositions) {
  auto& bookkeeping = mixer_.bookkeeping();
  bookkeeping.SetRateModuloAndDenominator(5, 7);

  auto& info = mixer_.source_info();
  info.dest_frames_to_frac_source_frames = TimelineFunction(TimelineRate(17u, 1u));
  // All these values will be overwritten
  bookkeeping.source_pos_modulo = 1u;
  info.next_dest_frame = -97;
  info.next_source_frame = Fixed(7);
  info.source_pos_error = zx::duration(-777);

  info.ResetPositions(100, bookkeeping);

  EXPECT_EQ(info.next_dest_frame, 100);
  // Calculated directly from the TimelineFunction
  EXPECT_EQ(info.next_source_frame, Fixed::FromRaw(1700));

  // cleared by ResetPositions
  EXPECT_EQ(bookkeeping.source_pos_modulo, 0ull);
  EXPECT_EQ(info.source_pos_error, zx::duration(0));
}

// From current values, AdvanceAllPositionsTo advances running positions for dest, source and
// source_modulo to a given dest frame, based on the step_size, rate_modulo and denominator.
void SourceInfoTest::TestPositionAdvanceNoRateModulo(bool advance_source_pos_modulo) {
  auto& bookkeeping = mixer_.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(0, 1);
  bookkeeping.source_pos_modulo = 1;

  auto& info = mixer_.source_info();
  info.next_source_frame = Fixed(3);
  info.source_pos_error = zx::duration(-17);
  info.next_dest_frame = 2;

  if (advance_source_pos_modulo) {
    info.AdvanceAllPositionsTo(11, bookkeeping);
  } else {
    info.UpdateRunningPositionsBy(9, bookkeeping);
  }

  // These should be unchanged
  EXPECT_EQ(info.source_pos_error, zx::duration(-17));
  EXPECT_EQ(bookkeeping.source_pos_modulo, 1u);

  // These should be updated
  EXPECT_EQ(info.next_dest_frame, 11u);  // starts at 2, advance 9
  // Source starts at 3, step_size "1.002", advance by 9 dest, adds 9 frames 18 subframes
  // We expect new source_pos to be 12 frames, 18 subframes.
  EXPECT_EQ(info.next_source_frame, Fixed(12) + Fixed::FromRaw(18))
      << "next_source_frame " << ffl::String::DecRational << info.next_source_frame;
}

TEST_F(SourceInfoTest, AdvanceAllPositions_NoRateModulo) {
  TestPositionAdvanceNoRateModulo(/* advance_source_pos_modulo = */ true);
}
TEST_F(SourceInfoTest, UpdateRunningPositions_NoRateModulo) {
  TestPositionAdvanceNoRateModulo(/* advance_source_pos_modulo = */ false);
}

void SourceInfoTest::TestPositionAdvanceWithRateModulo(bool advance_source_pos_modulo) {
  auto& bookkeeping = mixer_.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(2, 5);
  bookkeeping.source_pos_modulo = 2;

  auto& info = mixer_.source_info();
  info.next_dest_frame = 2;
  info.next_source_frame = Fixed(3);
  info.source_pos_error = zx::duration(-17);

  if (advance_source_pos_modulo) {
    info.AdvanceAllPositionsTo(11, bookkeeping);
  } else {
    info.UpdateRunningPositionsBy(9, bookkeeping);
  }

  // This should be unchanged
  EXPECT_EQ(info.source_pos_error, zx::duration(-17));

  // These should be updated
  // Source starts at 3 with position modulo 1/5, step_size "1.002" with rate_modulo 2/5.
  // Advancing by 9 dest frames will add 9 * (1frame + 2subframes) to source_pos
  // (9 frames + 18 subframes), plus any source_pos_modulo effects.
  if (advance_source_pos_modulo) {
    // rate_mod/denom is 2/5, so source_pos_modulo should increase by (9 * 2), from 2 to 20.
    // source_pos_modulo / denominator (20 / 5) is 4, so source_pos adds 4 subframes.
    // The remaining source_pos_modulo (20 % 5) is 0.
    // Thus new source_pos should be 12 frames (3+9), 22 subframes (18+4), modulo 0/5.
    EXPECT_EQ(bookkeeping.source_pos_modulo, 0ull);
  } else {
    // rate_mod/denom is 2/5, so source_pos_modulo increased by (9 * 2) and ended up as 2 (22).
    // source_pos_modulo / denominator (22 / 5) is 4, so source_pos adds 4 subframes.
    // The remaining source_pos_modulo (22 % 5) is 2.
    // Thus new source_pos should be 12 frames (3+9), 22 subframes (18+4), modulo 2/5.
    EXPECT_EQ(bookkeeping.source_pos_modulo, 2ull);
  }
  EXPECT_EQ(info.next_dest_frame, 11u);
  EXPECT_EQ(info.next_source_frame, Fixed(Fixed(12) + Fixed::FromRaw(22)))
      << "next_source_frame " << ffl::String::DecRational << info.next_source_frame;
}

TEST_F(SourceInfoTest, AdvanceAllPositions_WithRateModulo) {
  TestPositionAdvanceWithRateModulo(/* advance_source_pos_modulo = */ true);
}
TEST_F(SourceInfoTest, UpdateRunningPositions_WithRateModulo) {
  TestPositionAdvanceWithRateModulo(/* advance_source_pos_modulo = */ false);
}

// Also validate AdvanceAllPositionsTo for negative offsets.
void SourceInfoTest::TestPositionAdvanceNegative(bool advance_source_pos_modulo) {
  auto& bookkeeping = mixer_.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(2, 5);

  auto& info = mixer_.source_info();
  info.next_dest_frame = 12;
  info.next_source_frame = Fixed(3);
  bookkeeping.source_pos_modulo = 0;

  // Advance by -3 dest frames at a step_size of [1 frame + 2 subframes + mod 2/5]
  // For -3 dest frames, this is an "advance" of -3 frames, -6 subframes, -6/5 mod.
  if (advance_source_pos_modulo) {
    info.AdvanceAllPositionsTo(9, bookkeeping);

    // source_pos starts at 3 frames, 0 subframes, with position modulo 3 out of 5.
    // source was 3 frames 0 subframes, plus -3f -6sf, is now 0 frames -6 subframes.
    // source_pos_mod was 0/5, plus -6/5, is now -6/5 (negative modulo must be reduced).
    // -6 subframes -6/5 becomes 8 subframes + mod 4/5.
    // Thus source become 0 frames -8 subframes, pos_mod 4/5.
    EXPECT_EQ(info.next_source_frame, Fixed::FromRaw(-8))
        << "next_source_frame " << ffl::String::DecRational << info.next_source_frame;
    EXPECT_EQ(bookkeeping.source_pos_modulo, 4ul);
  } else {
    info.UpdateRunningPositionsBy(-3, bookkeeping);

    // source_pos starts at 3 frames, 0 subframes, with final position modulo 0 / 5.
    // 3 frames 0 subframes, plus -3 frames -6 subframes, is now 0 frames -6 subframes.
    // Unknown source_pos_mod, plus -6/5, is now 0/5 (or -5/5 reduced: must have started at 1/5).
    // -6 subframes -5/5 becomes -7 subframes + mod 0/5.
    //
    // source become 0 frames -7 subframes, pos_mod 0/5.
    EXPECT_EQ(info.next_source_frame, Fixed::FromRaw(-7))
        << "next_source_frame " << ffl::String::DecRational << info.next_source_frame;
    EXPECT_EQ(bookkeeping.source_pos_modulo, 0ul);
  }
  EXPECT_EQ(info.next_dest_frame, 9u);
}

TEST_F(SourceInfoTest, AdvanceAllPositions_Negative) {
  TestPositionAdvanceNegative(/* advance_source_pos_modulo = */ true);
}
TEST_F(SourceInfoTest, UpdateRunningPositions_Negative) {
  TestPositionAdvanceNegative(/* advance_source_pos_modulo = */ false);
}

}  // namespace
}  // namespace media::audio::mixer
