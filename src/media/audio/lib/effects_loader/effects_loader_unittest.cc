// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace media::audio {
namespace {

static constexpr const char* kPassthroughModuleName = "audiofx.so";
static constexpr uint32_t kPassthroughEffectId = 0;
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
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
}

TEST(EffectsLoaderTest, LoadUnloadMultiple) {
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_OK, loader.UnloadLibrary());
}

TEST(EffectsLoaderTest, DoubleLoad) {
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(ZX_OK, loader.LoadLibrary());
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, loader.LoadLibrary());
}

TEST(EffectsLoaderTest, DoubleUnload) {
  EffectsLoader loader(kPassthroughModuleName);
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
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader->GetNumFx(&num_effects), ZX_OK);
  EXPECT_EQ(1u, num_effects);
}

TEST(EffectsLoaderTest, GetNumEffectsNullCount) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, loader->GetNumFx(nullptr));
}

TEST(EffectsLoaderTest, GetNumEffectsModuleNotLoaded) {
  uint32_t num_effects;
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, loader.GetNumFx(&num_effects));
}

TEST(EffectsLoaderTest, GetFxInfo) {
  fuchsia_audio_dfx_description dfx_desc;
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader->GetFxInfo(kPassthroughEffectId, &dfx_desc), ZX_OK);
  EXPECT_TRUE(dfx_desc.incoming_channels == FUCHSIA_AUDIO_DFX_CHANNELS_ANY);
  EXPECT_TRUE(dfx_desc.outgoing_channels == FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN);
}

TEST(EffectsLoaderTest, GetFxInfoModuleNotLoaded) {
  fuchsia_audio_dfx_description dfx_desc;
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader.GetFxInfo(kPassthroughEffectId, &dfx_desc), ZX_ERR_NOT_FOUND);
}

TEST(EffectsLoaderTest, GetFxInfoNullInfoPointer) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader->GetFxInfo(kPassthroughEffectId, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST(EffectsLoaderTest, GetFxInfoInvalidEffextId) {
  fuchsia_audio_dfx_description dfx_desc;
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader->GetFxInfo(kInvalidEffectId, &dfx_desc), ZX_ERR_OUT_OF_RANGE);
}

TEST(EffectsLoaderTest, CreateFx) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  fx_token_t token = loader->CreateFx(kPassthroughEffectId, kFrameRate, kTwoChannels, kTwoChannels);
  EXPECT_NE(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

TEST(EffectsLoaderTest, CreateFxModuleNotLoaded) {
  EffectsLoader loader(kPassthroughModuleName);
  fx_token_t token = loader.CreateFx(kPassthroughEffectId, kFrameRate, kTwoChannels, kTwoChannels);
  EXPECT_EQ(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

TEST(EffectsLoaderTest, CreateFxInvalidEffectId) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  fx_token_t token = loader->CreateFx(kInvalidEffectId, kFrameRate, kTwoChannels, kTwoChannels);
  EXPECT_EQ(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

TEST(EffectsLoaderTest, CreateFxInvalidChannelConfiguration) {
  // The passthrough effect requres in_chans == out_chans.
  AutoEffectsLoader loader(kPassthroughModuleName);
  fx_token_t token =
      loader->CreateFx(kPassthroughEffectId, kFrameRate, kTwoChannels, kTwoChannels - 1);
  EXPECT_EQ(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

TEST(EffectsLoaderTest, CreateFxTooManyChannels) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  static constexpr uint32_t kTooManyChannels = FUCHSIA_AUDIO_DFX_CHANNELS_MAX + 1;
  fx_token_t token =
      loader->CreateFx(kPassthroughEffectId, kFrameRate, kTooManyChannels, kTooManyChannels);
  EXPECT_EQ(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

TEST(EffectsLoaderTest, DeleteFx) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  fx_token_t token = loader->CreateFx(kPassthroughEffectId, kFrameRate, kTwoChannels, kTwoChannels);
  EXPECT_NE(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_EQ(loader->DeleteFx(token), ZX_OK);
}

TEST(EffectsLoaderTest, DeleteFxInvalidToken) {
  AutoEffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader->DeleteFx(FUCHSIA_AUDIO_DFX_INVALID_TOKEN), ZX_ERR_INVALID_ARGS);
}

TEST(EffectsLoaderTest, DeleteFxLibraryNotLoaded) {
  EffectsLoader loader(kPassthroughModuleName);
  EXPECT_EQ(loader.LoadLibrary(), ZX_OK);
  fx_token_t token = loader.CreateFx(kPassthroughEffectId, kFrameRate, kTwoChannels, kTwoChannels);
  EXPECT_NE(token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_EQ(loader.UnloadLibrary(), ZX_OK);

  EXPECT_EQ(loader.DeleteFx(token), ZX_ERR_NOT_FOUND);
}

}  // namespace
}  // namespace media::audio
