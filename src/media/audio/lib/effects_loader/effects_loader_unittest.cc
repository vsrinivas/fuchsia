// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/lib/effects_loader/effects_loader_test_base.h"

namespace media::audio {
namespace {

class EffectsLoaderTest : public test::EffectsLoaderTestBase {};

static constexpr uint32_t kInvalidEffectId = 1;
static constexpr uint32_t kFrameRate = 48000;
static constexpr uint16_t kTwoChannels = 2;

// The |EffectsLoaderModuleNotLoadedTest| suite holds tests that exercise the `EffectsLoader` in a
// state before a valid module has been loaded. This is done by the test fixture for
// |EffectsLoaderTest| so don't use the fixture for these test cases.

TEST(EffectsLoaderModuleNotLoadedTest, LoadInvalidModule) {
  EffectsLoader loader("does_not_exist.so");
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, loader.LoadLibrary());
}

TEST(EffectsLoaderModuleNotLoadedTest, GetNumEffectsModuleNotLoaded) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  uint32_t num_effects = 0;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, loader.GetNumFx(&num_effects));
}

TEST_F(EffectsLoaderTest, GetNumEffects) {
  uint32_t num_effects;

  // Add effect 1
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  EXPECT_EQ(effects_loader()->GetNumFx(&num_effects), ZX_OK);
  EXPECT_EQ(1u, num_effects);

  // Add effect 2
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_2.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               2.0}));
  EXPECT_EQ(effects_loader()->GetNumFx(&num_effects), ZX_OK);
  EXPECT_EQ(2u, num_effects);

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, GetNumEffectsNullCount) {
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, effects_loader()->GetNumFx(nullptr));
}

TEST_F(EffectsLoaderTest, GetFxInfo) {
  fuchsia_audio_effects_description dfx_desc;

  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));

  EXPECT_EQ(effects_loader()->GetFxInfo(0, &dfx_desc), ZX_OK);
  EXPECT_TRUE(dfx_desc.incoming_channels == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY);
  EXPECT_TRUE(dfx_desc.outgoing_channels == FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN);

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, GetFxInfoNullInfoPointer) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));

  EXPECT_EQ(effects_loader()->GetFxInfo(0, nullptr), ZX_ERR_INVALID_ARGS);

  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, GetFxInfoInvalidEffectId) {
  fuchsia_audio_effects_description dfx_desc;

  EXPECT_EQ(effects_loader()->GetFxInfo(kInvalidEffectId, &dfx_desc), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(EffectsLoaderTest, CreateFx) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
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

TEST_F(EffectsLoaderTest, CreateFxInvalidEffectId) {
  // Since we didn't call 'add_effect' there are no valid effect_id's that can be used for
  // CreateFx.
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

TEST_F(EffectsLoaderTest, CreateFxInvalidChannelConfiguration) {
  // The passthrough effect requires in_chans == out_chans.
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTwoChannels, kTwoChannels - 1, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

TEST_F(EffectsLoaderTest, CreateFxTooManyChannels) {
  static constexpr uint32_t kTooManyChannels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX + 1;
  Effect e = effects_loader()->CreateEffect(0, kFrameRate, kTooManyChannels, kTooManyChannels, {});
  EXPECT_FALSE(e);
  ASSERT_EQ(0u, test_effects()->num_instances());
}

}  // namespace
}  // namespace media::audio
