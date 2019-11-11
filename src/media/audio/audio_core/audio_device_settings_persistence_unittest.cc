// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_persistence.h"

#include <fbl/ref_ptr.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/media/audio/audio_core/audio_device_settings_serialization_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

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

static constexpr const char* kTestOutputDevicePath =
    "/tmp/settings/000102030405060708090a0b0c0d0e0f-output.json";

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

class AudioDeviceSettingsPersistenceTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    // Ensure we have no files left around.
    ASSERT_TRUE(files::DeletePath(kSettingsPath, true));
    ASSERT_TRUE(files::DeletePath(kDefaultSettingsPath, true));
    // The "default settings" directory should already exist (expected on a read-only directory).
    ASSERT_TRUE(files::CreateDirectory(kDefaultSettingsPath));
  }

  void ClearFile(const char* path) {
    files::WriteFile(path, "", 0);
    ASSERT_TRUE(IsFileEmpty(path));
  }
  bool IsFileEmpty(const char* path) {
    uint64_t size;
    FX_CHECK(files::GetFileSize(path, &size));
    return size == 0;
  }

  template <typename V, typename E>
  fit::result<V, E> RunPromise(fit::executor* executor, fit::promise<V, E> promise) {
    fit::result<V, E> result;
    executor->schedule_task(
        std::move(promise).then([&result](fit::result<V, E>& r) { result = std::move(r); }));
    RunLoopUntilIdle();
    FX_CHECK(!result.is_pending());
    return result;
  }
};

TEST_F(AudioDeviceSettingsPersistenceTest, LoadSettingsWithNoDefaultShouldWriteSettings) {
  {
    AudioDeviceSettingsPersistence settings_persistence(
        &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
        kTestConfigSources);

    // Load settings; since none exist we expect a settings file to be created.
    auto settings =
        fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);

    auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                                settings_persistence.LoadSettings(settings));
    ASSERT_TRUE(result.is_ok());
    // Sanity check the file is created:
    EXPECT_TRUE(files::IsFile("/tmp/settings/000102030405060708090a0b0c0d0e0f-output.json"));

    // Update gain & finalize (write back to disk).
    fuchsia::media::AudioGainInfo gain_info;
    gain_info.gain_db = 10.0;
    EXPECT_TRUE(settings->SetGainInfo(gain_info, fuchsia::media::SetAudioGainFlag_GainValid));
    result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                           settings_persistence.FinalizeSettings(*settings));
    ASSERT_TRUE(result.is_ok());
  }

  // Create a new AudioDeviceSettingsPersistence/AudioDeviceSettings; verify settings are loaded.
  {
    AudioDeviceSettingsPersistence settings_persistence(
        &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
        kTestConfigSources);
    auto settings =
        fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
    auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                                settings_persistence.LoadSettings(settings));
    ASSERT_TRUE(result.is_ok());

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

  AudioDeviceSettingsPersistence settings_persistence(
      &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
      kTestConfigSources);

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
  auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                              settings_persistence.LoadSettings(settings));
  ASSERT_TRUE(result.is_ok());

  // We expect the default settings are now replicated to settings.
  EXPECT_TRUE(files::IsFile(kTestOutputDevicePath));

  // Verify settings match what was in the default-settings JSON.
  settings->GetGainInfo(&gain_info);
  EXPECT_TRUE(settings->Ignored());
  EXPECT_TRUE(settings->AutoRoutingDisabled());
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_EQ(11.0, gain_info.gain_db);
}

TEST_F(AudioDeviceSettingsPersistenceTest, LoadSettingsDuplicateDeviceId) {
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
  AudioDeviceSettingsPersistence settings_persistence(
      &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
      kTestConfigSources);

  // Create 2 device settings instances with the same device id.
  HwGainState gain_state = kDefaultInitialHwGainState;
  gain_state.can_agc = true;
  gain_state.cur_agc = false;
  gain_state.cur_mute = false;
  gain_state.cur_gain = 1.0;
  auto settings1 = fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, gain_state, false);
  auto settings2 = fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, gain_state, false);

  // Load settings. We expect this to read and deserialize the values in the JSON above.
  auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                              settings_persistence.LoadSettings(settings1));
  ASSERT_TRUE(result.is_ok());

  // Make a change to the loaded settings.
  settings1->SetIgnored(!settings1->Ignored());

  // Now load settings 2. It has the same device id but we expect this to be initialized to a clone
  // of settings 1.
  result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                         settings_persistence.LoadSettings(settings2));
  ASSERT_TRUE(result.is_ok());

  // Verify |settings2| matches |settings1|.
  fuchsia::media::AudioGainInfo gain_info1, gain_info2;
  settings1->GetGainInfo(&gain_info1);
  settings2->GetGainInfo(&gain_info2);
  EXPECT_EQ(settings1->Ignored(), settings2->Ignored());
  EXPECT_EQ(settings1->AutoRoutingDisabled(), settings2->AutoRoutingDisabled());
  EXPECT_FLOAT_EQ(gain_info1.gain_db, gain_info2.gain_db);
  EXPECT_EQ(gain_info1.flags, gain_info2.flags);

  // Remove the settings file so we can detect when changes are written back.
  EXPECT_TRUE(files::IsFile(kTestOutputDevicePath));
  ClearFile(kTestOutputDevicePath);

  // Mutate settings 2
  settings2->SetAutoRoutingDisabled(!settings2->AutoRoutingDisabled());

  // Finalize |settings2|. Since this is initialized as a clone we expect no writeback even though
  // we've changed the state.
  result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                         settings_persistence.FinalizeSettings(*settings2));
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));

  // Finalize |settings1|. This one _should_ write back.
  result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                         settings_persistence.FinalizeSettings(*settings1));
  ASSERT_TRUE(result.is_ok());
  EXPECT_FALSE(IsFileEmpty(kTestOutputDevicePath));
}

TEST_F(AudioDeviceSettingsPersistenceTest, ChangingSettingsShouldWriteToFile) {
  AudioDeviceSettingsPersistence settings_persistence(
      &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
      kTestConfigSources);

  // Load settings; since none exist we expect a settings file to be created.
  auto settings =
      fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
  auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                              settings_persistence.LoadSettings(settings));
  ASSERT_TRUE(result.is_ok());

  // Expect our settings file was created. Then clear the file so we can detect when it's been
  // written again.
  EXPECT_TRUE(files::IsFile(kTestOutputDevicePath));
  ClearFile(kTestOutputDevicePath);

  // Mutate settings. Since writeback is done with a delay we expect no files to be immediately
  // written.
  settings->SetIgnored(!settings->Ignored());
  EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));

  // But we do expect the file to be written after a delay
  RunLoopFor(AudioDeviceSettingsPersistence::kUpdateDelay);
  EXPECT_FALSE(IsFileEmpty(kTestOutputDevicePath));
}

TEST_F(AudioDeviceSettingsPersistenceTest, RapidSettingsChangesExtendsWritebackDelay) {
  AudioDeviceSettingsPersistence settings_persistence(
      &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
      kTestConfigSources);

  // Load settings; since none exist we expect a settings file to be created.
  auto settings =
      fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
  auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                              settings_persistence.LoadSettings(settings));
  ASSERT_TRUE(result.is_ok());

  // Expect our settings file was created. Then clear the file so we can detect when it's been
  // written again.
  EXPECT_TRUE(files::IsFile(kTestOutputDevicePath));
  ClearFile(kTestOutputDevicePath);

  // Mutate settings. Since writeback is done with a delay we expect no files to be immediately
  // written.
  settings->SetIgnored(!settings->Ignored());
  EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));

  // Pass some time but not enough to trigger a writeback. Verify file has not yet been written
  // back.
  RunLoopFor(AudioDeviceSettingsPersistence::kUpdateDelay - zx::msec(50));
  EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));

  // Mutate settings again, this should push out the writeback time until kUpdateDelay from _now_.
  //
  // Run the loop for another 50ms to see if we get our initial writeback (we should not).
  settings->SetIgnored(!settings->Ignored());
  RunLoopFor(zx::msec(50));
  EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));

  // Finally verify we get the writeback as expected.
  RunLoopFor(AudioDeviceSettingsPersistence::kUpdateDelay - zx::msec(50));
  EXPECT_FALSE(IsFileEmpty(kTestOutputDevicePath));
}

TEST_F(AudioDeviceSettingsPersistenceTest, AudioDeviceSettingsWritebackIsBoundedByMaxUpdateDelay) {
  AudioDeviceSettingsPersistence settings_persistence(
      &threading_model(), AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer(),
      kTestConfigSources);

  // Load settings; since none exist we expect a settings file to be created.
  auto settings =
      fbl::MakeRefCounted<AudioDeviceSettings>(kTestUniqueId, kDefaultInitialHwGainState, false);
  auto result = RunPromise<void, zx_status_t>(threading_model().FidlDomain().executor(),
                                              settings_persistence.LoadSettings(settings));
  ASSERT_TRUE(result.is_ok());

  // Expect our settings file was created. Then clear the file so we can detect when it's been
  // written again.
  EXPECT_TRUE(files::IsFile(kTestOutputDevicePath));
  ClearFile(kTestOutputDevicePath);

  // Make an initial mutation and request a commit of dirty settings.
  settings->SetIgnored(!settings->Ignored());

  // Keep mutating the settings before kUpdateDelay time passes. Do this until right before we'll
  // elapse past kMaxUpdateDelay.
  zx::duration increment = AudioDeviceSettingsPersistence::kUpdateDelay - zx::msec(50);
  zx::duration end = AudioDeviceSettingsPersistence::kMaxUpdateDelay - increment;
  zx::duration elapsed(0);
  while (elapsed < end) {
    RunLoopFor(increment);
    elapsed += increment;
    EXPECT_TRUE(IsFileEmpty(kTestOutputDevicePath));
    settings->SetIgnored(!settings->Ignored());
  }

  // Run the final iteration which pushes us over kMaxUpdateDelay. We now expect that we do write
  // back despite frequent updates.
  RunLoopFor(increment);
  elapsed += increment;
  EXPECT_GT(elapsed, AudioDeviceSettingsPersistence::kMaxUpdateDelay);
  EXPECT_FALSE(IsFileEmpty(kTestOutputDevicePath));
}

}  // namespace
}  // namespace media::audio
