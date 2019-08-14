// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/lib/effects_loader/effects_loader_test_base.h"

namespace media::audio {
namespace {

static constexpr uint32_t kEffectId = 0;
static constexpr uint32_t kInvalidEffectId = 1;
static constexpr uint32_t kFrameRate = 48000;
static constexpr uint16_t kTwoChannels = 2;

// RAII wrapper around `EffectsLoader` that Loads and Unloads automatically.
class AutoEffectsLoader {
 public:
  AutoEffectsLoader(const char* libname) : effects_loader_(libname) {
    FXL_CHECK(effects_loader_.LoadLibrary() == ZX_OK);
  }

  ~AutoEffectsLoader() { FXL_CHECK(effects_loader_.UnloadLibrary() == ZX_OK); }

  EffectsLoader* operator->() { return &effects_loader_; }

 private:
  EffectsLoader effects_loader_;
};

TEST(EffectsLoaderTest, LoadUnloadLibrary) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
}

TEST(EffectsLoaderTest, LoadUnloadMultiple) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
}

TEST(EffectsLoaderTest, DoubleLoad) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, loader.LoadLibrary());
}

TEST(EffectsLoaderTest, DoubleUnload) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, loader.UnloadLibrary());
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, loader.UnloadLibrary());
}

TEST(EffectsLoaderTest, LoadInvalidModule) {
  EffectsLoader loader("does_not_exist.so");
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, loader.LoadLibrary());
}

TEST(EffectsLoaderTest, GetNumEffects) {
  uint32_t num_effects;
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  auto test_effects = test::OpenTestEffectsExt();

  // Add effect 1
  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             TEST_EFFECTS_ACTION_ASSIGN,
                                             1.0}));
  EXPECT_EQ(loader->GetNumFx(&num_effects), ZX_OK);
  EXPECT_EQ(1u, num_effects);

  // Add effect 2
  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"assign_to_2.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             TEST_EFFECTS_ACTION_ASSIGN,
                                             2.0}));
  EXPECT_EQ(loader->GetNumFx(&num_effects), ZX_OK);
  EXPECT_EQ(2u, num_effects);

  test_effects->clear_effects();
}

TEST(EffectsLoaderTest, GetNumEffectsNullCount) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, loader->GetNumFx(nullptr));
}

TEST(EffectsLoaderTest, GetNumEffectsModuleNotLoaded) {
  uint32_t num_effects;
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, loader.GetNumFx(&num_effects));
}

TEST(EffectsLoaderTest, GetFxInfo) {
  fuchsia_audio_effects_description effect_desc;
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  auto test_effects = test::OpenTestEffectsExt();

  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             TEST_EFFECTS_ACTION_ASSIGN,
                                             1.0}));

  EXPECT_EQ(loader->GetFxInfo(0, &effect_desc), ZX_OK);
  EXPECT_TRUE(effect_desc.incoming_channels == FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY);
  EXPECT_TRUE(effect_desc.outgoing_channels == FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN);

  test_effects->clear_effects();
}

TEST(EffectsLoaderTest, GetFxInfoModuleNotLoaded) {
  fuchsia_audio_effects_description effect_desc;
  EffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(loader.GetFxInfo(kEffectId, &effect_desc), ZX_ERR_NOT_FOUND);
}

TEST(EffectsLoaderTest, GetFxInfoNullInfoPointer) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(loader->GetFxInfo(kEffectId, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST(EffectsLoaderTest, GetFxInfoInvalidEffectId) {
  fuchsia_audio_effects_description effect_desc;
  AutoEffectsLoader loader(test::kTestEffectsModuleName);

  EXPECT_EQ(loader->GetFxInfo(kInvalidEffectId, &effect_desc), ZX_ERR_OUT_OF_RANGE);
}

TEST(EffectsLoaderTest, CreateFx) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  auto test_effects = test::OpenTestEffectsExt();

  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             TEST_EFFECTS_ACTION_ASSIGN,
                                             1.0}));
  fuchsia_audio_effects_handle_t handle =
      loader->CreateFx(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_NE(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  EXPECT_EQ(loader->DeleteFx(handle), ZX_OK);
  test_effects->clear_effects();
}

TEST(EffectsLoaderTest, CreateFxModuleNotLoaded) {
  EffectsLoader loader(test::kTestEffectsModuleName);
  fuchsia_audio_effects_handle_t handle =
      loader.CreateFx(kEffectId, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_EQ(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST(EffectsLoaderTest, CreateFxInvalidEffectId) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  fuchsia_audio_effects_handle_t handle =
      loader->CreateFx(kInvalidEffectId, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_EQ(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST(EffectsLoaderTest, CreateFxInvalidChannelConfiguration) {
  // The passthrough effect requres in_chans == out_chans.
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  fuchsia_audio_effects_handle_t handle =
      loader->CreateFx(kEffectId, kFrameRate, kTwoChannels, kTwoChannels - 1, {});
  EXPECT_EQ(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST(EffectsLoaderTest, CreateFxTooManyChannels) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  static constexpr uint32_t kTooManyChannels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX + 1;
  fuchsia_audio_effects_handle_t handle =
      loader->CreateFx(kEffectId, kFrameRate, kTooManyChannels, kTooManyChannels, {});
  EXPECT_EQ(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

TEST(EffectsLoaderTest, DeleteFx) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  auto test_effects = test::OpenTestEffectsExt();

  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             TEST_EFFECTS_ACTION_ASSIGN,
                                             1.0}));
  fuchsia_audio_effects_handle_t handle =
      loader->CreateFx(0, kFrameRate, kTwoChannels, kTwoChannels, {});
  EXPECT_NE(handle, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  EXPECT_EQ(loader->DeleteFx(handle), ZX_OK);
  test_effects->clear_effects();
}

TEST(EffectsLoaderTest, DeleteFxInvalidhandle) {
  AutoEffectsLoader loader(test::kTestEffectsModuleName);
  EXPECT_EQ(loader->DeleteFx(FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace media::audio
