// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_persistence.h"

#include <lib/gtest/test_loop_fixture.h>

#include <fbl/ref_ptr.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {
namespace {

// The default configs will look in /data and /config, which we don't have mapped for unittest. We
// do have isolated-temp so redirect both of these directories to our /tmp.
const std::string kSettingsPath = "/tmp/settings";
const std::string kDefaultSettingsPath = "/tmp/default-settings";
static const AudioDeviceSettingsPersistence::ConfigSource kTestConfigSources[2] = {
    {.prefix = kSettingsPath, .is_default = false},
    {.prefix = kDefaultSettingsPath, .is_default = true},
};

static constexpr audio_stream_unique_id_t kTestUniqueId = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

static constexpr HwGainState kDefaultInitialHwGainState = {
    false,   /* cur_mute */
    false,   /* cur_agc */
    0.0,     /* cur_gain */
    true,    /* can_mute */
    true,    /* can_agc */
    -160.0f, /* min_gain */
    24.0f,   /* max_gain */
    1.0f     /* gain_step */
};

class AudioDeviceSettingsPersistenceTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    // Ensure we have no files left around.
    ASSERT_TRUE(files::DeletePath(kSettingsPath, true));
    ASSERT_TRUE(files::DeletePath(kDefaultSettingsPath, true));
    // The "default settings" directory shoudl already exist (expected on a read-only directory).
    ASSERT_TRUE(files::CreateDirectory(kDefaultSettingsPath));
  }
  void TearDown() override {}
};

TEST_F(AudioDeviceSettingsPersistenceTest, InitializeShouldCreateSettingsPath) {
  AudioDeviceSettingsPersistence settings_persistence(dispatcher(), kTestConfigSources);

  EXPECT_FALSE(files::IsDirectory(kSettingsPath));
  settings_persistence.Initialize();
  EXPECT_TRUE(files::IsDirectory(kSettingsPath));
}

TEST_F(AudioDeviceSettingsPersistenceTest, LoadSettingsWithNoDefaultShouldWriteSettings) {
  {
    AudioDeviceSettingsPersistence settings_persistence(dispatcher(), kTestConfigSources);
    settings_persistence.Initialize();

    // Load settings; since none existe we expect a settings file to be created.
    auto settings =
        fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
    ASSERT_EQ(ZX_OK, settings_persistence.LoadSettings(settings));
    // Sanity check the file is created:
    EXPECT_TRUE(files::IsFile("/tmp/settings/000102030405060708090a0b0c0d0e0f-output.json"));

    // Update gain & finalize (write back to disk).
    fuchsia::media::AudioGainInfo gain_info;
    gain_info.gain_db = 10.0;
    EXPECT_TRUE(settings->SetGainInfo(gain_info, fuchsia::media::SetAudioGainFlag_GainValid));
    settings_persistence.FinalizeDeviceSettings(*settings);
  }

  // Create a new AudioDeviceSettingsPersistence/AudioDeviceSettings; verify settings are loaded.
  {
    AudioDeviceSettingsPersistence settings_persistence(dispatcher(), kTestConfigSources);
    settings_persistence.Initialize();
    auto settings =
        fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
    ASSERT_EQ(ZX_OK, settings_persistence.LoadSettings(settings));

    fuchsia::media::AudioGainInfo gain_info;
    settings->GetGainInfo(&gain_info);
    EXPECT_EQ(10.0, gain_info.gain_db);
  }
}

TEST_F(AudioDeviceSettingsPersistenceTest, LoadSettingsWithDefault) {
  static std::string kDefaultSettings =
      R"JSON({
    "gain": {
      "gain_db": 11.0,
      "mute": true,
      "agc": true
    },
    "ignore_device": true,
    "disallow_auto_routing": true
  })JSON";
  ASSERT_TRUE(files::WriteFile("/tmp/default-settings/000102030405060708090a0b0c0d0e0f-output.json",
                               kDefaultSettings.data(), kDefaultSettings.size()));

  AudioDeviceSettingsPersistence settings_persistence(dispatcher(), kTestConfigSources);
  settings_persistence.Initialize();

  // Initialize AudioDeviceSettings to be different from the values in the JSON above so that we
  // can detect those changes have been applied.
  HwGainState gain_state = kDefaultInitialHwGainState;
  gain_state.can_agc = true;
  gain_state.cur_agc = false;
  gain_state.cur_mute = false;
  gain_state.cur_gain = 1.0;
  auto settings = fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, gain_state, false);

  // Verify our initial values.
  fuchsia::media::AudioGainInfo gain_info;
  settings->GetGainInfo(&gain_info);
  EXPECT_FALSE(settings->Ignored());
  EXPECT_FALSE(settings->AutoRoutingDisabled());
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_EQ(1.0, gain_info.gain_db);

  // Load settings. We expect this to read and deserialize the values in the JSON above.
  ASSERT_EQ(ZX_OK, settings_persistence.LoadSettings(settings));

  // We expect the default settings are now replicated to settings.
  EXPECT_TRUE(files::IsFile("/tmp/settings/000102030405060708090a0b0c0d0e0f-output.json"));

  // Verify settings match what was in the default-settings JSON.
  settings->GetGainInfo(&gain_info);
  EXPECT_TRUE(settings->Ignored());
  EXPECT_TRUE(settings->AutoRoutingDisabled());
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_EQ(11.0, gain_info.gain_db);
}

}  // namespace
}  // namespace media::audio
