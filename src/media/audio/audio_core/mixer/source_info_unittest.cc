// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {
namespace {

class StubMixer : public Mixer {
 public:
  StubMixer() : Mixer(Fixed(0), Fixed(0), Gain::Limits{}) {}

  bool Mix(float*, int64_t, int64_t*, const void*, int64_t, Fixed*, bool) final { return false; }
};

TEST(SourceInfoTest, Defaults) {
  StubMixer mixer;
  auto& info = mixer.source_info();

  EXPECT_EQ(info.next_dest_frame, 0);
  EXPECT_EQ(info.next_source_frame, 0);
  EXPECT_EQ(info.next_source_pos_modulo, 0ull);
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
// next_source_frame is set according to dest_to_frac_source transform, next_source_pos_modulo
// according to rate_modulo and denominator.
TEST(SourceInfoTest, ResetPositions) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.SetRateModuloAndDenominator(5, 7);

  auto& info = mixer.source_info();
  info.dest_frames_to_frac_source_frames = TimelineFunction(TimelineRate(17u, 1u));
  // All these values will be overwritten
  info.next_dest_frame = -97;
  info.next_source_frame = Fixed(7);
  info.next_source_pos_modulo = 1u;
  info.source_pos_error = zx::duration(-777);

  info.ResetPositions(100, bookkeeping);

  EXPECT_EQ(info.next_dest_frame, 100);
  // Calculated directly from the TimelineFunction
  EXPECT_EQ(info.next_source_frame, Fixed::FromRaw(1700));

  // cleared by ResetPositions
  EXPECT_EQ(info.next_source_pos_modulo, 0ull);
  EXPECT_EQ(info.source_pos_error, zx::duration(0));
}

// Bookkeeping::Reset clears its own struct but should not affect SourceInfo.
TEST(SourceInfoTest, UnaffectedByBookkeepingReset) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.SetRateModuloAndDenominator(5, 7);
  bookkeeping.source_pos_modulo = 3u;

  auto& info = mixer.source_info();
  info.next_dest_frame = 13;
  info.next_source_frame = Fixed(11);
  info.next_source_pos_modulo = 2;
  info.source_pos_error = zx::duration(-17);

  bookkeeping.Reset();

  EXPECT_EQ(info.next_dest_frame, 13);
  EXPECT_EQ(info.next_source_frame, Fixed(11));
  EXPECT_EQ(info.next_source_pos_modulo, 2ull);
  EXPECT_EQ(info.source_pos_error, zx::duration(-17));
}

// From current values, AdvanceRunningPositions advances running positions for dest, source and
// source_modulo by given dest frames, based on the step_size, rate_modulo and denominator.
TEST(SourceInfoTest, AdvanceRunningPositions_NoRateModulo) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(0, 1);
  bookkeeping.source_pos_modulo = 3;

  auto& info = mixer.source_info();
  info.next_source_frame = Fixed(3);
  info.next_source_pos_modulo = 1;
  info.source_pos_error = zx::duration(-17);
  info.next_dest_frame = 2;

  info.AdvanceRunningPositionsBy(9, bookkeeping);

  // This should be unchanged
  EXPECT_EQ(info.source_pos_error, zx::duration(-17));
  EXPECT_EQ(info.next_source_pos_modulo, 1u);
  EXPECT_EQ(bookkeeping.source_pos_modulo, 3u);

  // These should be updated
  EXPECT_EQ(info.next_dest_frame, 11u);  // starts at 2, advance 9
  //
  // Source starts at 3, step_size "1.002", advance by 9 dest, adds 9 frames 18 subframes
  // We expect new source_pos to be 12 frames, 18 subframes.
  EXPECT_EQ(info.next_source_frame, Fixed(12) + Fixed::FromRaw(18))
      << "next_source_frame "
      << (info.next_source_frame.raw_value() >> Fixed::Format::FractionalBits) << "."
      << info.next_source_frame.Fraction().raw_value();
}

TEST(SourceInfoTest, AdvanceRunningPositionsTo) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(2, 5);
  bookkeeping.source_pos_modulo = 3;

  auto& info = mixer.source_info();
  info.next_dest_frame = 2;
  info.next_source_frame = Fixed(3);
  info.next_source_pos_modulo = 1;
  info.source_pos_error = zx::duration(-17);

  info.AdvanceRunningPositionsTo(11, bookkeeping);

  // This should be unchanged
  EXPECT_EQ(info.source_pos_error, zx::duration(-17));

  // These should be updated
  //
  // Source starts at 3 with position modulo 1/5, step_size "1.002" with rate_modulo 2/5.
  // Advancing by 9 dest frames adds (9 * 1.002) to source_pos, plus rate_modulo effects.
  // source_pos_modulo should increase by (9 * 2), from 1 to 19.
  // source_pos_modulo / denominator 19/5 (3) means that source_pos += "0.003"
  // and source_pos_modulo becomes 19 % 5 == 4
  // We expect new source_pos to be 12 frames, 21 subframes, modulo 4/5.
  EXPECT_EQ(info.next_dest_frame, 11u);
  EXPECT_EQ(info.next_source_frame, Fixed(Fixed(12) + Fixed::FromRaw(21)))
      << "next_source_frame "
      << (info.next_source_frame.raw_value() >> Fixed::Format::FractionalBits) << "."
      << info.next_source_frame.Fraction().raw_value();
  EXPECT_EQ(info.next_source_pos_modulo, 4ull);
  // Starts at position modulo 3 (out of 5). Advance by 9 with rate_modulo 2.
  // Position mod: expect 3 + (9 * 2) = 21, %5 becomes position modulo 1.
  EXPECT_EQ(bookkeeping.source_pos_modulo, 1u);
}

// Also validate AdvanceRunningPositions for negative offsets.
TEST(SourceInfoTest, NegativeAdvanceRunningPositionBy) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.step_size = kOneFrame + Fixed::FromRaw(2);
  bookkeeping.SetRateModuloAndDenominator(2, 5);

  auto& info = mixer.source_info();
  info.next_dest_frame = 12;
  info.next_source_frame = Fixed(3);
  info.next_source_pos_modulo = 0;

  info.AdvanceRunningPositionsBy(-3, bookkeeping);

  // source_pos starts at 3 frames, 0 subframes, with position modulo 0 out of 5.
  // Advanced by -3 dest frames at a step_size of [1 frame + 2 subframes+ mod 2/5]
  // For -3 dest frames, this is a source advance of -3 frames, -6 subframes, -6/5 mod.
  // source_pos_mod was 0/5, plus -6/5, is now -6/5, but negative modulo must be reduced.
  // 0 subframes + mod -6/5 becomes -2 subframe + mod 4/5.
  //
  // source advances by -3f, -8 subframes (-6-2) to become 0 frames -8 subframes.
  EXPECT_EQ(info.next_dest_frame, 9u);
  EXPECT_EQ(info.next_source_frame, Fixed::FromRaw(-8))
      << "next_source_frame "
      << (info.next_source_frame.raw_value() >> Fixed::Format::FractionalBits) << "."
      << info.next_source_frame.Fraction().raw_value();
  EXPECT_EQ(info.next_source_pos_modulo, 4ul);
}

}  // namespace
}  // namespace media::audio::mixer
