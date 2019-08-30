// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_json.h"

#include <lib/gtest/test_loop_fixture.h>
#include <zircon/device/audio.h>

#include <cstdio>

#include "src/media/audio/audio_core/audio_device_settings.h"
#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {
namespace {

static const std::string kAudioDeviceSettingsInvalidSchema = "asdf";
static constexpr audio_stream_unique_id_t kTestUniqueId = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

// Both mute and agc are supported but disabled. Gain is initialized to 0.0f.
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

class AudioDeviceSettingsJsonTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    file_ = std::tmpfile();
    ASSERT_NE(file_, nullptr);
  }
  void TearDown() override {
    ASSERT_NE(file_, nullptr);
    std::fclose(file_);
  }
  void WriteToFile(std::string_view s) {
    ASSERT_EQ(s.size(), std::fwrite(s.data(), 1, s.size(), file_));
    ASSERT_EQ(0, std::fflush(file_));
  }

  int fd() const { return fileno(file_); }

 private:
  std::FILE* file_ = nullptr;
};

TEST_F(AudioDeviceSettingsJsonTest, CreateWithSchema) {
  std::unique_ptr<AudioDeviceSettingsJson> device_json;
  zx_status_t status = AudioDeviceSettingsJson::Create(&device_json);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(device_json);
}

TEST_F(AudioDeviceSettingsJsonTest, CreateWithInvalidSchemaFails) {
  std::unique_ptr<AudioDeviceSettingsJson> device_json;
  zx_status_t status = AudioDeviceSettingsJson::CreateWithSchema(
      kAudioDeviceSettingsInvalidSchema.c_str(), &device_json);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  ASSERT_FALSE(device_json);
}

// Verify we can read a valid config JSON.
TEST_F(AudioDeviceSettingsJsonTest, Deserialize) {
  std::unique_ptr<AudioDeviceSettingsJson> device_json;
  zx_status_t status = AudioDeviceSettingsJson::Create(&device_json);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(device_json);

  WriteToFile(
      R"JSON({
      "gain": {
        "gain_db": 5.0,
        "mute": true,
        "agc": true
      },
      "ignore_device": true,
      "disallow_auto_routing": true
    })JSON");

  // Initialize AudioDeviceSettings from HwGainState.
  AudioDeviceSettings settings(kTestUniqueId, kDefaultInitialHwGainState, false);

  // Verify initial conditions.
  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);
  EXPECT_EQ(0.0f, gain_info.gain_db);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_FALSE(settings.AutoRoutingDisabled());
  EXPECT_FALSE(settings.Ignored());

  // Deserialize and verify new settings.
  EXPECT_EQ(ZX_OK, device_json->Deserialize(fd(), &settings));
  settings.GetGainInfo(&gain_info);
  EXPECT_EQ(5.0f, gain_info.gain_db);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(settings.AutoRoutingDisabled());
  EXPECT_TRUE(settings.Ignored());
}

TEST_F(AudioDeviceSettingsJsonTest, DeserializeExtraToplevelKeysFailsToDeserialize) {
  std::unique_ptr<AudioDeviceSettingsJson> device_json;
  zx_status_t status = AudioDeviceSettingsJson::Create(&device_json);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(device_json);

  WriteToFile(
      R"JSON({
      "EXTRA_KEY": true,
      "gain": {
        "gain_db": 5.0,
        "mute": true,
        "agc": true
      },
      "ignore_device": true,
      "disallow_auto_routing": true
    })JSON");

  // Initialize AudioDeviceSettings from HwGainState.
  AudioDeviceSettings settings(kTestUniqueId, kDefaultInitialHwGainState, false);

  // Deserialize should fail.
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, device_json->Deserialize(fd(), &settings));
}

TEST_F(AudioDeviceSettingsJsonTest, DeserializeExtraGainKeysFails) {
  std::unique_ptr<AudioDeviceSettingsJson> device_json;
  zx_status_t status = AudioDeviceSettingsJson::Create(&device_json);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(device_json);

  WriteToFile(
      R"JSON({
      "gain": {
        "EXTRA_KEY": true,
        "gain_db": 5.0,
        "mute": true,
        "agc": true
      },
      "ignore_device": true,
      "disallow_auto_routing": true
    })JSON");

  // Initialize AudioDeviceSettings from HwGainState.
  AudioDeviceSettings settings(kTestUniqueId, kDefaultInitialHwGainState, false);

  // Deserialize should fail.
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, device_json->Deserialize(fd(), &settings));
}

}  // namespace
}  // namespace media::audio
