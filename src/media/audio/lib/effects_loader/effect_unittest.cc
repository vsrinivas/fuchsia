// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effect.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/effects_loader/testing/effects_loader_test_base.h"

namespace media::audio {
namespace {

class EffectTest : public testing::EffectsLoaderTestBase {};

TEST_F(EffectTest, MoveEffect) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  Effect effect1 = effects_loader()->CreateEffect(0, 1, 1, 1, {});
  ASSERT_TRUE(effect1);

  // New, invalid, effect.
  Effect effect2;
  ASSERT_FALSE(effect2);

  // Move effect1 -> effect2.
  effect2 = std::move(effect1);
  ASSERT_TRUE(effect2);
  ASSERT_FALSE(effect1);

  // Create effect3 via move ctor.
  Effect effect3(std::move(effect2));
  ASSERT_TRUE(effect3);
  ASSERT_FALSE(effect2);
}

}  // namespace
}  // namespace media::audio
