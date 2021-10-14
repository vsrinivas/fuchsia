// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effect_v1.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/effects_loader/testing/effects_loader_v1_test_base.h"

namespace media::audio {
namespace {

class EffectV1Test : public testing::EffectsLoaderV1TestBase {};

static const std::string kInstanceName = "instance name";

TEST_F(EffectV1Test, MoveEffect) {
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);

  EffectV1 effect1 = effects_loader()->CreateEffect(0, kInstanceName, 1, 1, 1, {});
  ASSERT_TRUE(effect1);
  ASSERT_EQ(kInstanceName, effect1.instance_name());

  // New, invalid, effect.
  EffectV1 effect2;
  ASSERT_FALSE(effect2);

  // Move effect1 -> effect2.
  effect2 = std::move(effect1);
  ASSERT_TRUE(effect2);
  ASSERT_FALSE(effect1);

  // Create effect3 via move ctor.
  EffectV1 effect3(std::move(effect2));
  ASSERT_TRUE(effect3);
  ASSERT_FALSE(effect2);
}

}  // namespace
}  // namespace media::audio
