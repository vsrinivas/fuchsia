// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

namespace media::audio {
namespace {

class AudioTunerImplTest : public testing::ThreadingModelFixture {
 protected:
  testing::TestEffectsModule test_effects_ = testing::TestEffectsModule::Open();
  AudioTunerImpl under_test_;
};

TEST_F(AudioTunerImplTest, GetAvailableAudioEffects) {
  // Create an effect we can load.
  test_effects_.AddEffect("test_effect");

  std::vector<fuchsia::media::tuning::AudioEffectType> available_effects;
  under_test_.GetAvailableAudioEffects(
      [&available_effects](std::vector<fuchsia::media::tuning::AudioEffectType> effects) {
        available_effects = effects;
      });
  EXPECT_EQ(available_effects.size(), 1u);
  EXPECT_EQ(available_effects[0].module_name, testing::kTestEffectsModuleName);
  EXPECT_EQ(available_effects[0].effect_name, "test_effect");
}

}  // namespace
}  // namespace media::audio
