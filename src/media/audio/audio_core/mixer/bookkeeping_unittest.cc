// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

TEST(BookkeepingTest, Defaults) {
  StubMixer mixer;
  auto& info = mixer.bookkeeping();

  EXPECT_EQ(info.step_size, Mixer::FRAC_ONE);
  EXPECT_EQ(info.rate_modulo, 0u);
  EXPECT_EQ(info.denominator, 0u);
  EXPECT_EQ(info.src_pos_modulo, 0u);

  EXPECT_EQ(info.SnapshotDenominatorFromDestTrans(), 1u);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.subject_time(), 0);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.reference_time(), 0);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.subject_delta(), 0u);
  EXPECT_EQ(info.dest_frames_to_frac_source_frames.reference_delta(), 1u);
  EXPECT_EQ(info.dest_trans_gen_id, kInvalidGenerationId);

  EXPECT_EQ(info.clock_mono_to_frac_source_frames.subject_time(), 0);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.reference_time(), 0);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.subject_delta(), 0u);
  EXPECT_EQ(info.clock_mono_to_frac_source_frames.reference_delta(), 1u);
  EXPECT_EQ(info.source_trans_gen_id, kInvalidGenerationId);
}

TEST(BookkeepingTest, SnapshotDenominator) {
  StubMixer mixer;
  auto& info = mixer.bookkeeping();

  // This common  timeline function reduces to 147/160.
  info.dest_frames_to_frac_source_frames = TimelineFunction(0, 1, 44100u, 48000u);
  EXPECT_EQ(info.SnapshotDenominatorFromDestTrans(), 160u);
}

// Upon Reset, Bookkeeping should clear position modulo and gain ramp, and reset its mixer.
TEST(BookkeepingTest, Reset) {
  StubMixer mixer;
  auto& info = mixer.bookkeeping();

  info.src_pos_modulo = 4321u;

  info.gain.SetSourceGainWithRamp(-42.0f, zx::sec(1),
                                  fuchsia::media::audio::RampType::SCALE_LINEAR);
  EXPECT_TRUE(info.gain.IsRamping());

  info.Reset();

  EXPECT_EQ(info.src_pos_modulo, 0u);
  EXPECT_FALSE(info.gain.IsRamping());
}

}  // namespace
}  // namespace media::audio::mixer
