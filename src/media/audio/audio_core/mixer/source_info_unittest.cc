// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {
namespace {

class StubMixer : public Mixer {
 public:
  StubMixer() : Mixer(0, 0) {}

  bool Mix(float*, uint32_t, uint32_t*, const void*, uint32_t, int32_t*, bool) final {
    return false;
  }
};

TEST(SourceInfoTest, Defaults) {
  StubMixer mixer;
  auto& info = mixer.source_info();

  EXPECT_EQ(info.next_dest_frame, 0);
  EXPECT_EQ(info.next_frac_source_frame, 0);
  EXPECT_EQ(info.next_src_pos_modulo, 0u);
  EXPECT_EQ(info.frac_source_error, 0);

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
// next_frac_source_frame is set according to dest_to_frac_src transform, next_src_pos_modulo
// according to rate_modulo and denominator.
TEST(SourceInfoTest, ResetPositions) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.rate_modulo = 5;
  bookkeeping.denominator = 7;

  auto& info = mixer.source_info();
  info.dest_frames_to_frac_source_frames = TimelineFunction(TimelineRate(17u));
  // All these values will be overwritten
  info.next_dest_frame = -97;
  info.next_frac_source_frame = Fixed(7);
  info.next_src_pos_modulo = 1u;
  info.frac_source_error = Fixed::FromRaw(-777);

  info.ResetPositions(100, bookkeeping);

  EXPECT_EQ(info.next_dest_frame, 100);
  // Calculated directly from the TimelineFunction
  EXPECT_EQ(info.next_frac_source_frame, Fixed::FromRaw(1700));
  // Calculated from rate_modulo and deominator, starting at zero. (100*5)%7 = 3.
  EXPECT_EQ(info.next_src_pos_modulo, 3u);
  EXPECT_EQ(info.frac_source_error, 0);
}

// Bookkeeping::Reset clears its own struct but should not affect SourceInfo.
TEST(SourceInfoTest, UnaffectedByBookkeepingReset) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.rate_modulo = 5;
  bookkeeping.denominator = 7;
  bookkeeping.src_pos_modulo = 3u;

  auto& info = mixer.source_info();
  info.next_dest_frame = 13;
  info.next_frac_source_frame = Fixed(11);
  info.next_src_pos_modulo = 2;
  info.frac_source_error = Fixed::FromRaw(-17);

  bookkeeping.Reset();

  EXPECT_EQ(info.next_dest_frame, 13);
  EXPECT_EQ(info.next_frac_source_frame, Fixed(11));
  EXPECT_EQ(info.next_src_pos_modulo, 2u);
  EXPECT_EQ(info.frac_source_error, Fixed::FromRaw(-17));
}

// From current values, AdvanceRunningPositions advances running positions for dest, frac_source and
// frac_source_modulo by given dest frames, based on the step_size, rate_modulo and denominator.
TEST(SourceInfoTest, AdvanceRunningPositionsTo) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.step_size = Mixer::FRAC_ONE + 2;
  bookkeeping.rate_modulo = 2;
  bookkeeping.denominator = 5;
  bookkeeping.src_pos_modulo = 3;

  auto& info = mixer.source_info();
  info.next_dest_frame = 2;
  info.next_frac_source_frame = Fixed(3);
  info.next_src_pos_modulo = 1;
  info.frac_source_error = Fixed::FromRaw(-17);

  info.AdvanceRunningPositionsTo(11, bookkeeping);

  // These should be unchanged
  EXPECT_EQ(bookkeeping.src_pos_modulo, 3u);
  EXPECT_EQ(info.frac_source_error, Fixed::FromRaw(-17));

  // These should be updated
  //
  // Starts at 3 with position modulo 1 (out of 5).
  // Advanced by 9 dest frames at step_size "1.002" with rate_modulo 2.
  // Position mod: expect 1 + (9 * 2) = 19, %5 becomes 3 subframes and position modulo 4.
  // frac_src: expect 3 + (9 * 1.002) frames (12 frames + 18 subframes), plus 3 subs from above.
  // Thus expect new running src position: 12 frames, 21 subframes, position modulo 4.
  EXPECT_EQ(info.next_dest_frame, 11u);
  EXPECT_EQ(info.next_frac_source_frame, Fixed(12) + Fixed::FromRaw(21));
  EXPECT_EQ(info.next_src_pos_modulo, 4u);
}

// Also validate AdvanceRunningPositions for negative offsets.
TEST(SourceInfoTest, NegativeAdvanceRunningPositionBy) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.step_size = Mixer::FRAC_ONE + 2;
  bookkeeping.rate_modulo = 2;
  bookkeeping.denominator = 5;

  auto& info = mixer.source_info();
  info.next_dest_frame = 12;
  info.next_frac_source_frame = Fixed(3);
  info.next_src_pos_modulo = 0;

  info.AdvanceRunningPositionsBy(-3, bookkeeping);

  // frac_src_pos starts at 3 frames, 0 subframes, with position modulo 0 out of 5.
  // Advanced by -3 dest frames at a step_size of [1 frame + 2 subframes+ mod 2/5]
  // For -3 dest frames, this is a src advance of -3 frames, -6 subframes, -6/5 mod.
  // src_pos_mod was 0/5, plus -6/5, is now -6/5, but negative modulo must be reduced.
  // 0 subframes + mod -6/5 becomes -2 subframe + mod 4/5.
  //
  // frac_src advances by -3f, -8 subframes (-6-2) to become 0 frames -8 subframes.
  EXPECT_EQ(info.next_dest_frame, 9u);
  EXPECT_EQ(info.next_frac_source_frame, Fixed::FromRaw(-8));
  EXPECT_EQ(info.next_src_pos_modulo, 4u);
}

}  // namespace
}  // namespace media::audio::mixer
