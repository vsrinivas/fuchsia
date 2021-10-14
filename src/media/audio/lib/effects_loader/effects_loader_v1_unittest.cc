// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader_v1.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/effects_loader/testing/effects_loader_v1_test_base.h"

namespace media::audio {
namespace {

class EffectsLoaderV1Test : public testing::EffectsLoaderV1TestBase {};

static constexpr uint32_t kInvalidEffectId = 1;
static constexpr int32_t kFrameRate = 48000;
static constexpr int32_t kTwoChannels = 2;

static const std::string kInstanceName = "instance name";

// The |EffectsLoaderV1ModuleNotLoadedTest| suite holds tests that exercise the `EffectsLoaderV1` in
// a state before a valid module has been loaded. This is done by the test fixture for
// |EffectsLoaderV1Test| so don't use the fixture for these test cases.
TEST(EffectsLoaderV1ModuleNotLoadedTest, CreateWithInvalidModule) {
  std::unique_ptr<EffectsLoaderV1> loader;
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, EffectsLoaderV1::CreateWithModule("does_not_exist.so", &loader));
  EXPECT_FALSE(loader);
}

TEST(EffectsLoaderV1ModuleNotLoadedTest, CreateWithNullModule) {
  // Sanity test the null module behaves as expected.
  std::unique_ptr<EffectsLoaderV1> loader = EffectsLoaderV1::CreateWithNullModule();
  ASSERT_TRUE(loader);

  EXPECT_EQ(0u, loader->GetNumEffects());

  // Test that |GetEffectInfo| and |CreateEffect| behave as expected. These are unimplemented for
  // the null module, so we just sanity check here the valid effect ID check is implemented by
  // the loader itself and not deferred to the (unimplemented) module functions.
  fuchsia_audio_effects_description desc = {};
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, loader->GetEffectInfo(0, &desc));
  auto effect = loader->CreateEffect(0, "", kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_FALSE(effect);
}

TEST_F(EffectsLoaderV1Test, GetNumEffects) {
  // Add effect 1
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);
  EXPECT_EQ(1u, effects_loader()->GetNumEffects());

  // Add effect 2
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 2.0);
  EXPECT_EQ(2u, effects_loader()->GetNumEffects());
}

TEST_F(EffectsLoaderV1Test, GetEffectInfoNullInfoPointer) {
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);

  EXPECT_EQ(effects_loader()->GetEffectInfo(0, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST_F(EffectsLoaderV1Test, GetEffectInfoInvalidEffectId) {
  fuchsia_audio_effects_description dfx_desc;

  EXPECT_EQ(effects_loader()->GetEffectInfo(kInvalidEffectId, &dfx_desc), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(EffectsLoaderV1Test, CreateEffectByEffectId) {
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);
  {
    ASSERT_EQ(0u, test_effects().InstanceCount());
    EffectV1 e = effects_loader()->CreateEffect(0, kInstanceName, kFrameRate, kTwoChannels,
                                                kTwoChannels, {});
    EXPECT_TRUE(e);
    EXPECT_EQ(kInstanceName, e.instance_name());
    ASSERT_EQ(1u, test_effects().InstanceCount());
  }

  // Let |e| go out of scope, verify the instance was removed.
  ASSERT_EQ(0u, test_effects().InstanceCount());
}

TEST_F(EffectsLoaderV1Test, CreateEffectInvalidEffectId) {
  // Since we didn't call 'AddEffect' there are no valid effect_id's that can be used for
  // CreateEffect.
  EffectV1 e = effects_loader()->CreateEffect(0, "", kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects().InstanceCount());
}

TEST_F(EffectsLoaderV1Test, CreateEffectByName) {
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);

  // The fixture creates the loader by default. Since the loader caches the set of effects at
  // create time, we need to recreate the loader to see the new effect name.
  RecreateLoader();
  {
    ASSERT_EQ(0u, test_effects().InstanceCount());
    EffectV1 e = effects_loader()->CreateEffectByName("assign_to_1.0", kInstanceName, kFrameRate,
                                                      kTwoChannels, kTwoChannels, {});
    EXPECT_TRUE(e);
    EXPECT_EQ(kInstanceName, e.instance_name());
    ASSERT_EQ(1u, test_effects().InstanceCount());
  }

  // Let |e| go out of scope, verify the instance was removed.
  ASSERT_EQ(0u, test_effects().InstanceCount());
}

TEST_F(EffectsLoaderV1Test, CreateEffectByNameInvalidName) {
  test_effects().AddEffect("assign_to_1.0").WithAction(TEST_EFFECTS_ACTION_ASSIGN, 1.0);

  // The fixture creates the loader by default. Since the loader caches the set of effects at
  // create time, we need to recreate the loader to see the new effect name.
  RecreateLoader();
  {
    ASSERT_EQ(0u, test_effects().InstanceCount());
    EffectV1 e = effects_loader()->CreateEffectByName("invalid_name", "", kFrameRate, kTwoChannels,
                                                      kTwoChannels, {});
    EXPECT_FALSE(e);
    ASSERT_EQ(0u, test_effects().InstanceCount());
  }
}

TEST_F(EffectsLoaderV1Test, CreateEffectInvalidChannelConfiguration) {
  // The passthrough effect requires in_chans == out_chans.
  EffectV1 e =
      effects_loader()->CreateEffect(0, "", kFrameRate, kTwoChannels, kTwoChannels - 1, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects().InstanceCount());
}

TEST_F(EffectsLoaderV1Test, CreateEffectTooManyChannels) {
  static constexpr int32_t kTooManyChannels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX + 1;
  EffectV1 e =
      effects_loader()->CreateEffect(0, "", kFrameRate, kTooManyChannels, kTooManyChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects().InstanceCount());
}

}  // namespace
}  // namespace media::audio
