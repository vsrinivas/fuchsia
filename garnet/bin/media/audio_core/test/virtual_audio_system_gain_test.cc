// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/test/virtual_audio_device_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <cmath>
#include <cstring>

#include "gtest/gtest.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "src/lib/fxl/logging.h"

namespace media::audio::test {

//
// VirtualAudioSystemGainTest declaration
//
// These tests verifies async usage of AudioDeviceEnumerator w/SystemGain.
class VirtualAudioSystemGainTest : public VirtualAudioDeviceTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  bool ExpectCallback() override;

  void AddDeviceForSystemGainTesting(bool is_input);
  void ChangeAndVerifySystemGain();
  void ChangeAndVerifySystemMute();
  void TestDeviceGainAfterChangeSystemGainMute(bool use_get_devices,
                                               bool is_input, bool set_gain);
  void TestOnDeviceGainChangedAfterChangeSystemGainMute(bool is_input,
                                                        bool set_gain);

  static constexpr float kInitialSystemGainDb = -12.0f;
  static constexpr float kChangedSystemGainDb = -2.0f;

  fuchsia::media::AudioCorePtr audio_core_;

  float received_system_gain_db_ = NAN;
  bool received_system_mute_;
};

//
// VirtualAudioSystemGainTest implementation
//
void VirtualAudioSystemGainTest::SetUp() {
  VirtualAudioDeviceTest::SetUp();

  environment_services_->ConnectToService(audio_core_.NewRequest());
  audio_core_.set_error_handler(
      [this](zx_status_t error) { error_occurred_ = true; });

  audio_core_.events().SystemGainMuteChanged = [this](float gain_db,
                                                      bool muted) {
    received_callback_ = true;

    received_system_gain_db_ = gain_db;
    received_system_mute_ = muted;
  };
  ASSERT_TRUE(ExpectCallback());

  if (received_system_gain_db_ != kInitialSystemGainDb) {
    audio_core_->SetSystemGain(kInitialSystemGainDb);
    ASSERT_TRUE(ExpectCallback());
  }

  if (received_system_mute_) {
    audio_core_->SetSystemMute(false);
    ASSERT_TRUE(ExpectCallback());
  }
  // received_system_gain_db_/received_system_mute_ now contain initial state.
}

void VirtualAudioSystemGainTest::TearDown() {
  audio_core_->SetSystemGain(kInitialSystemGainDb);
  audio_core_->SetSystemMute(false);
  audio_core_.set_error_handler(nullptr);

  VirtualAudioDeviceTest::TearDown();
}

bool VirtualAudioSystemGainTest::ExpectCallback() {
  received_system_gain_db_ = NAN;

  return VirtualAudioDeviceTest::ExpectCallback();
}

void VirtualAudioSystemGainTest::AddDeviceForSystemGainTesting(bool is_input) {
  float system_gain_db = received_system_gain_db_;
  bool system_mute = received_system_mute_;

  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id;
  unique_id[0] = (is_input ? 0xF1 : 0xF0);
  unique_id[1] = sequential_devices_.Next();

  if (is_input) {
    input_->SetGainProperties(-160.0f, 24.0f, 0.25f, kInitialSystemGainDb, true,
                              false, false, false);
    input_->SetUniqueId(unique_id);
    input_->Add();
  } else {
    output_->SetGainProperties(-160.0f, 24.0f, 0.25f, kInitialSystemGainDb,
                               true, false, false, false);
    output_->SetUniqueId(unique_id);
    output_->Add();
  }

  ASSERT_TRUE(ExpectCallback());
  uint64_t added_token = received_device_.token_id;
  ASSERT_NE(added_token, ZX_KOID_INVALID);

  // If the device is different than expected, set it up as we expect.
  if ((received_device_.gain_info.gain_db != kInitialSystemGainDb) ||
      ((received_device_.gain_info.flags &
        fuchsia::media::AudioGainInfoFlag_Mute) != 0) ||
      ((received_device_.gain_info.flags &
        fuchsia::media::AudioGainInfoFlag_AgcEnabled) != 0)) {
    fuchsia::media::AudioGainInfo gain_info = {.gain_db = kInitialSystemGainDb,
                                               .flags = 0};
    uint32_t set_flags = fuchsia::media::SetAudioGainFlag_GainValid |
                         fuchsia::media::SetAudioGainFlag_MuteValid |
                         fuchsia::media::SetAudioGainFlag_AgcValid;
    SetOnDeviceGainChangedEvent();
    audio_dev_enum_->SetDeviceGain(added_token, gain_info, set_flags);
    ASSERT_TRUE(ExpectCallback());
  }

  if (system_gain_db != kInitialSystemGainDb) {
    audio_core_->SetSystemGain(kInitialSystemGainDb);
    ASSERT_TRUE(ExpectCallback());
  }
  if (system_mute) {
    audio_core_->SetSystemMute(false);
    ASSERT_TRUE(ExpectCallback());
  }
  received_device_.token_id = added_token;
}

void VirtualAudioSystemGainTest::ChangeAndVerifySystemGain() {
  float expect_gain_db = kChangedSystemGainDb;
  bool expect_mute = false;

  audio_core_->SetSystemGain(expect_gain_db);

  ASSERT_TRUE(ExpectCallback());
  ASSERT_EQ(received_system_gain_db_, expect_gain_db);
  ASSERT_EQ(received_system_mute_, expect_mute);
}

void VirtualAudioSystemGainTest::ChangeAndVerifySystemMute() {
  float expect_gain_db = kInitialSystemGainDb;
  bool expect_mute = true;

  audio_core_->SetSystemMute(expect_mute);

  ASSERT_TRUE(ExpectCallback());
  ASSERT_EQ(received_system_gain_db_, expect_gain_db);
  ASSERT_EQ(received_system_mute_, expect_mute);
}

// Add device, get its token and gain baseline
// Change System Gain or Mute, verify System change
// Get device gain via GetDevices, verify the change(s?)
void VirtualAudioSystemGainTest::TestDeviceGainAfterChangeSystemGainMute(
    bool use_get_devices, bool is_input, bool set_gain) {
  if (HasPreExistingDevices()) {
    FXL_LOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }

  AddDeviceForSystemGainTesting(is_input);
  ASSERT_NE(received_device_.token_id, ZX_KOID_INVALID);
  uint64_t added_token = received_device_.token_id;

  if (set_gain) {
    ChangeAndVerifySystemGain();
  } else {
    ChangeAndVerifySystemMute();
  }

  if (use_get_devices) {
    RetrieveGainInfoUsingGetDevices(added_token);
  } else {
    RetrieveGainInfoUsingGetDeviceGain(added_token);
  }

  float expect_gain_db =
      ((set_gain && !is_input) ? kChangedSystemGainDb : kInitialSystemGainDb);
  uint32_t expect_gain_flags =
      ((set_gain || is_input) ? 0 : fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_EQ(received_gain_info_.gain_db, expect_gain_db);
  EXPECT_EQ(received_gain_info_.flags, expect_gain_flags);
}

void VirtualAudioSystemGainTest::
    TestOnDeviceGainChangedAfterChangeSystemGainMute(bool is_input,
                                                     bool set_gain) {
  if (HasPreExistingDevices()) {
    FXL_LOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }

  float expect_gain_db = kInitialSystemGainDb;
  bool expect_mute = false;

  // First add a virtual device, and reset device & system gains.
  AddDeviceForSystemGainTesting(is_input);
  ASSERT_NE(received_device_.token_id, ZX_KOID_INVALID);
  uint64_t added_token = received_device_.token_id;

  // With SystemGain and DeviceGain events set, change System Gain or Mute
  SetOnDeviceGainChangedEvent();
  if (set_gain) {
    expect_gain_db = kChangedSystemGainDb;
    audio_core_->SetSystemGain(expect_gain_db);
  } else {
    expect_mute = true;
    audio_core_->SetSystemMute(expect_mute);
  }

  // Wait for both callback events to arrive (indeterminate order).
  fuchsia::media::AudioGainInfo gain_info = kInvalidGainInfo;
  float system_gain_db = NAN;
  bool system_mute = false;

  // SystemGain only takes effect upon Output devices
  bool need_device_event = !is_input;
  bool need_system_event = true;

  while (need_device_event || need_system_event) {
    if (!ExpectCallback()) {
      break;
    }
    if (received_gain_token_ != kInvalidDeviceToken) {
      EXPECT_EQ(received_gain_token_, added_token);
      gain_info = received_gain_info_;

      need_device_event = false;
    }
    if (!isnan(received_system_gain_db_)) {
      system_gain_db = received_system_gain_db_;
      system_mute = received_system_mute_;

      need_system_event = false;
    }
  }
  EXPECT_EQ(expect_gain_db, system_gain_db);
  EXPECT_EQ(expect_mute, system_mute);

  // Received Output device gain/mute should equal the system gain/mute sent.
  if (!is_input) {
    EXPECT_EQ(expect_gain_db, gain_info.gain_db);
    EXPECT_EQ(expect_mute, ((gain_info.flags &
                             fuchsia::media::AudioGainInfoFlag_Mute) != 0));
  } else {
    EXPECT_TRUE(isnan(gain_info.gain_db));
  }
}

// NO-change Gain -or- NO-change Mute

//
// VirtualAudioSystemGainTest test cases
//
TEST_F(VirtualAudioSystemGainTest, GetDevicesMatchesAddInputSetSystemGain) {
  TestDeviceGainAfterChangeSystemGainMute(true, true, true);
}

TEST_F(VirtualAudioSystemGainTest, GetDevicesMatchesAddInputSetSystemMute) {
  TestDeviceGainAfterChangeSystemGainMute(true, true, false);
}

TEST_F(VirtualAudioSystemGainTest, GetDeviceGainMatchesAddInputSetSystemGain) {
  TestDeviceGainAfterChangeSystemGainMute(false, true, true);
}

TEST_F(VirtualAudioSystemGainTest, GetDeviceGainMatchesAddInputSetSystemMute) {
  TestDeviceGainAfterChangeSystemGainMute(false, true, false);
}

TEST_F(VirtualAudioSystemGainTest,
       OnDeviceGainChangedMatchesAddInputSetSystemGain) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(true, true);
}

TEST_F(VirtualAudioSystemGainTest,
       OnDeviceGainChangedMatchesAddOutputSetSystemGain) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(false, true);
}

TEST_F(VirtualAudioSystemGainTest,
       OnDeviceGainChangedMatchesAddInputSetSystemMute) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(true, false);
}

TEST_F(VirtualAudioSystemGainTest,
       OnDeviceGainChangedMatchesAddOutputSetSystemMute) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(false, false);
}

}  // namespace media::audio::test
