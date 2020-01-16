// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/effects_loader/testing/effects_loader_test_base.h"

namespace media::audio {
namespace {

class EffectsLoaderTest : public testing::EffectsLoaderTestBase {};

static constexpr uint32_t kInvalidEffectId = 1;
static constexpr uint32_t kFrameRate = 48000;
static constexpr uint16_t kTwoChannels = 2;

// The |EffectsLoaderModuleNotLoadedTest| suite holds tests that exercise the `EffectsLoader` in a
// state before a valid module has been loaded. This is done by the test fixture for
// |EffectsLoaderTest| so don't use the fixture for these test cases.
TEST(EffectsLoaderModuleNotLoadedTest, CreateWithInvalidModule) {
  std::unique_ptr<EffectsLoader> loader;
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, EffectsLoader::CreateWithModule("does_not_exist.so", &loader));
  EXPECT_FALSE(loader);
}

TEST(EffectsLoaderModuleNotLoadedTest, CreateWithNullModule) {
  // Sanity test the null module behaves as expected.
  std::unique_ptr<EffectsLoader> loader = EffectsLoader::CreateWithNullModule();
  ASSERT_TRUE(loader);

  EXPECT_EQ(0u, loader->GetNumEffects());

  // Test that |GetEffectInfo| and |CreateEffect| behave as expected. These are unimplemented for
  // the null module, so we just sanity check here the valid effect ID check is implemented by
  // the loader itself and not deferred to the (unimplemented) module functions.
  fuchsia_audio_effects_description desc = {};
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, loader->GetEffectInfo(0, &desc));
  auto effect = loader->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_FALSE(effect);
}

TEST_F(EffectsLoaderTest, GetNumEffects) {
  // Add effect 1
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  EXPECT_EQ(1u, effects_loader()->GetNumEffects());

  // Add effect 2
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_2.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               2.0}));
  EXPECT_EQ(2u, effects_loader()->GetNumEffects());

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, GetEffectInfoNullInfoPointer) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));

  EXPECT_EQ(effects_loader()->GetEffectInfo(0, nullptr), ZX_ERR_INVALID_ARGS);

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, GetEffectInfoInvalidEffectId) {
  fuchsia_audio_effects_description dfx_desc;

  EXPECT_EQ(effects_loader()->GetEffectInfo(kInvalidEffectId, &dfx_desc), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(EffectsLoaderTest, CreateEffectByEffectId) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  {
    ASSERT_EQ(0u, test_effects()->num_instances());
    Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels, {});
    EXPECT_TRUE(e);
    ASSERT_EQ(1u, test_effects()->num_instances());
  }

  // Let |e| go out of scope, verify the instance was removed.
  ASSERT_EQ(0u, test_effects()->num_instances());

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, CreateEffectInvalidEffectId) {
  // Since we didn't call 'add_effect' there are no valid effect_id's that can be used for
  // CreateEffect.
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

TEST_F(EffectsLoaderTest, CreateEffectByName) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  // The fixture creates the loader by default. Since the loader caches the set of effects at
  // create time, we need to recreate the loader to see the new effect name.
  RecreateLoader();
  {
    ASSERT_EQ(0u, test_effects()->num_instances());
    Effect e = effects_loader()->CreateEffectByName("assign_to_1.0", kFrameRate, kTwoChannels,
                                                    kTwoChannels, {});
    EXPECT_TRUE(e);
    ASSERT_EQ(1u, test_effects()->num_instances());
  }

  // Let |e| go out of scope, verify the instance was removed.
  ASSERT_EQ(0u, test_effects()->num_instances());

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, CreateEffectByNameInvalidName) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  // The fixture creates the loader by default. Since the loader caches the set of effects at
  // create time, we need to recreate the loader to see the new effect name.
  RecreateLoader();
  {
    ASSERT_EQ(0u, test_effects()->num_instances());
    Effect e = effects_loader()->CreateEffectByName("invalid_name", kFrameRate, kTwoChannels,
                                                    kTwoChannels, {});
    EXPECT_FALSE(e);
    ASSERT_EQ(0u, test_effects()->num_instances());
  }

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, CreateEffectInvalidChannelConfiguration) {
  // The passthrough effect requires in_chans == out_chans.
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels - 1, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

TEST_F(EffectsLoaderTest, CreateEffectTooManyChannels) {
  static constexpr uint32_t kTooManyChannels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX + 1;
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTooManyChannels, kTooManyChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

}  // namespace
}  // namespace media::audio
