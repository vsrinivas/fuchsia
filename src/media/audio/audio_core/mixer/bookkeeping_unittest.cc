// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio {
namespace {

namespace mixer {

class ObservableMixer : public Mixer {
 public:
  ObservableMixer() : Mixer(0, 0) {}

  bool Mix(float*, uint32_t, uint32_t*, const void*, uint32_t, int32_t*, bool, Bookkeeping*) final {
    return false;
  }

  void Reset() { reset_called_ = true; }
  bool reset_called() { return reset_called_; }

 private:
  bool reset_called_ = false;
};

}  // namespace mixer

TEST(BookkeepingTest, Defaults) {
  Bookkeeping info;

  EXPECT_FALSE(info.mixer);

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
  Bookkeeping info;

  // This common  timeline function reduces to 147/160.
  info.dest_frames_to_frac_source_frames = TimelineFunction(0, 1, 44100u, 48000u);
  EXPECT_EQ(info.SnapshotDenominatorFromDestTrans(), 160u);
}

// Upon Reset, Bookkeeping should clear position modulo and gain ramp, and reset its mixer.
TEST(BookkeepingTest, Reset) {
  Bookkeeping info;

  info.src_pos_modulo = 4321u;

  info.gain.SetSourceGainWithRamp(-42.0f, zx::sec(1),
                                  fuchsia::media::audio::RampType::SCALE_LINEAR);
  EXPECT_TRUE(info.gain.IsRamping());

  info.mixer = std::make_unique<mixer::ObservableMixer>();
  auto observable_mixer = reinterpret_cast<mixer::ObservableMixer*>(info.mixer.get());
  EXPECT_FALSE(observable_mixer->reset_called());

  info.Reset();

  EXPECT_EQ(info.src_pos_modulo, 0u);
  EXPECT_FALSE(info.gain.IsRamping());
  EXPECT_TRUE(observable_mixer->reset_called());
}

}  // namespace
}  // namespace media::audio
