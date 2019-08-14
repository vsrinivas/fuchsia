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

static constexpr uint32_t kEffectId = 0;
static constexpr uint32_t kInvalidEffectId = UINT16_MAX;
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
  EXPECT_EQ(effects_loader()->GetFxInfo(kEffectId, nullptr), ZX_ERR_INVALID_ARGS);
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
  fuchsia_audio_effects_handle_t token =
      effects_loader()->CreateFx(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_NE(token, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  EXPECT_EQ(effects_loader()->DeleteFx(token), ZX_OK);
  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, CreateFxInvalidEffectId) {
  // Since we didn't call 'add_effect' there are no valid effect_id's that can be used for
  // CreateFx.
  fuchsia_audio_effects_handle_t token =
      effects_loader()->CreateFx(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_EQ(token, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST_F(EffectsLoaderTest, CreateFxInvalidChannelConfiguration) {
  // The passthrough effect requres in_chans == out_chans.
  fuchsia_audio_effects_handle_t token =
      effects_loader()->CreateFx(kEffectId, kFrameRate, kTwoChannels, kTwoChannels - 1, {});
  EXPECT_EQ(token, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST_F(EffectsLoaderTest, CreateFxTooManyChannels) {
  static constexpr uint32_t kTooManyChannels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX + 1;
  fuchsia_audio_effects_handle_t token =
      effects_loader()->CreateFx(kEffectId, kFrameRate, kTooManyChannels, kTooManyChannels, {});
  EXPECT_EQ(token, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST_F(EffectsLoaderTest, DeleteFx) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));
  fuchsia_audio_effects_handle_t token =
      effects_loader()->CreateFx(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_NE(token, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  EXPECT_EQ(effects_loader()->DeleteFx(token), ZX_OK);
  test_effects()->clear_effects();
}

TEST_F(EffectsLoaderTest, DeleteFxInvalidToken) {
  EXPECT_EQ(effects_loader()->DeleteFx(FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace media::audio
