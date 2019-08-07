// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <cmath>
#include <cstring>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/test/device/audio_device_test.h"
#include "src/media/audio/audio_core/test/device/virtual_audio_device_test.h"

namespace media::audio::test {

//
// VirtualAudioSystemGainTest declaration
//
// These tests verifies async usage of AudioDeviceEnumerator w/SystemGain.
class VirtualAudioSystemGainTest : public VirtualAudioDeviceTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  void ExpectCallback() override;
  void ExpectSystemGainMuteChanged();

  void AddDeviceForSystemGainTesting(bool is_input);
  void ChangeAndVerifySystemGain();
  void ChangeAndVerifySystemMute();
  void TestDeviceGainAfterChangeSystemGainMute(bool use_get_devices, bool is_input, bool set_gain);
  void TestOnDeviceGainChangedAfterChangeSystemGainMute(bool is_input, bool set_gain);

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

  environment()->ConnectToService(audio_core_.NewRequest());
  audio_core_.set_error_handler(ErrorHandler());

  audio_core_.events().SystemGainMuteChanged =
      CompletionCallback([this](float gain_db, bool muted) {
        received_system_gain_db_ = gain_db;
        received_system_mute_ = muted;
      });
  ExpectSystemGainMuteChanged();

  if (received_system_gain_db_ != kInitialSystemGainDb) {
    audio_core_->SetSystemGain(kInitialSystemGainDb);
    ExpectSystemGainMuteChanged();
  }

  if (received_system_mute_) {
    audio_core_->SetSystemMute(false);
    ExpectSystemGainMuteChanged();
  }
  // received_system_gain_db_/received_system_mute_ now contain initial state.
}

void VirtualAudioSystemGainTest::TearDown() {
  audio_core_.events().SystemGainMuteChanged = nullptr;
  audio_core_->SetSystemGain(kInitialSystemGainDb);
  audio_core_->SetSystemMute(false);

  audio_core_.Unbind();

  VirtualAudioDeviceTest::TearDown();
}

void VirtualAudioSystemGainTest::ExpectCallback() {
  received_system_gain_db_ = NAN;

  VirtualAudioDeviceTest::ExpectCallback();
}

void VirtualAudioSystemGainTest::ExpectSystemGainMuteChanged() {
  received_system_gain_db_ = NAN;

  ExpectCondition([this]() { return error_occurred_ || !isnan(received_system_gain_db_); });

  ASSERT_FALSE(error_occurred_);
  ASSERT_FALSE(isnan(received_system_gain_db_));
}

void VirtualAudioSystemGainTest::AddDeviceForSystemGainTesting(bool is_input) {
  float system_gain_db = received_system_gain_db_;
  bool system_mute = received_system_mute_;

  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  VirtualAudioDeviceTest::PopulateUniqueIdArr(is_input, unique_id.data());

  if (is_input) {
    input_->SetGainProperties(-160.0f, 24.0f, 0.25f, kInitialSystemGainDb, true, false, false,
                              false);
    input_->SetUniqueId(unique_id);
    input_->Add();
  } else {
    output_->SetGainProperties(-160.0f, 24.0f, 0.25f, kInitialSystemGainDb, true, false, false,
                               false);
    output_->SetUniqueId(unique_id);
    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  // If the device is different than expected, set it up as we expect.
  if ((received_device_.gain_info.gain_db != kInitialSystemGainDb) ||
      ((received_device_.gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute) != 0) ||
      ((received_device_.gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled) != 0)) {
    fuchsia::media::AudioGainInfo gain_info = {.gain_db = kInitialSystemGainDb, .flags = 0};
    uint32_t set_flags = fuchsia::media::SetAudioGainFlag_GainValid |
                         fuchsia::media::SetAudioGainFlag_MuteValid |
                         fuchsia::media::SetAudioGainFlag_AgcValid;
    SetOnDeviceGainChangedEvent();
    audio_dev_enum_->SetDeviceGain(added_token, gain_info, set_flags);
    ExpectGainChanged(added_token);
  }

  if (system_gain_db != kInitialSystemGainDb) {
    audio_core_->SetSystemGain(kInitialSystemGainDb);
    ExpectSystemGainMuteChanged();
  }
  if (system_mute) {
    audio_core_->SetSystemMute(false);
    ExpectSystemGainMuteChanged();
  }
  received_device_.token_id = added_token;
  ASSERT_NE(received_device_.token_id, ZX_KOID_INVALID);
}

void VirtualAudioSystemGainTest::ChangeAndVerifySystemGain() {
  float expect_gain_db = kChangedSystemGainDb;
  bool expect_mute = false;

  audio_core_->SetSystemGain(expect_gain_db);
  ExpectSystemGainMuteChanged();

  ASSERT_EQ(received_system_gain_db_, expect_gain_db);
  ASSERT_EQ(received_system_mute_, expect_mute);
}

void VirtualAudioSystemGainTest::ChangeAndVerifySystemMute() {
  float expect_gain_db = kInitialSystemGainDb;
  bool expect_mute = true;

  audio_core_->SetSystemMute(expect_mute);
  ExpectSystemGainMuteChanged();

  ASSERT_EQ(received_system_gain_db_, expect_gain_db);
  ASSERT_EQ(received_system_mute_, expect_mute);
}

// Add device, get its token and gain baseline
// Change System Gain or Mute, verify System change
// Get device gain via GetDevices, verify the change(s?)
void VirtualAudioSystemGainTest::TestDeviceGainAfterChangeSystemGainMute(bool use_get_devices,
                                                                         bool is_input,
                                                                         bool set_gain) {
  if (HasPreExistingDevices()) {
    FXL_DLOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }

  AddDeviceForSystemGainTesting(is_input);
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

  float expect_gain_db = ((set_gain && !is_input) ? kChangedSystemGainDb : kInitialSystemGainDb);
  uint32_t expect_gain_flags =
      ((set_gain || is_input) ? 0 : fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_EQ(received_gain_info_.gain_db, expect_gain_db);
  EXPECT_EQ(received_gain_info_.flags, expect_gain_flags);
}

void VirtualAudioSystemGainTest::TestOnDeviceGainChangedAfterChangeSystemGainMute(bool is_input,
                                                                                  bool set_gain) {
  if (HasPreExistingDevices()) {
    FXL_DLOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }

  float expect_gain_db = kInitialSystemGainDb;
  bool expect_mute = false;

  // First add a virtual device, and reset device & system gains.
  AddDeviceForSystemGainTesting(is_input);
  uint64_t added_token = received_device_.token_id;

  // With SystemGain and DeviceGain events set, change System Gain or Mute
  SetOnDeviceGainChangedEvent();
  received_gain_token_ = kInvalidDeviceToken;
  received_gain_info_ = kInvalidGainInfo;
  received_system_gain_db_ = NAN;

  if (set_gain) {
    expect_gain_db = kChangedSystemGainDb;
    audio_core_->SetSystemGain(expect_gain_db);
  } else {
    expect_mute = true;
    audio_core_->SetSystemMute(expect_mute);
  }

  // SystemGain only takes effect upon Output devices
  if (is_input) {
    // For inputs, we expect NO device gain callback.
    ExpectSystemGainMuteChanged();

    // Give an erroneous device gain callback a chance to arrive
    RunLoopUntilIdle();
    EXPECT_NE(received_gain_token_, added_token);
  } else {
    // For outputs, expect both callbacks (in indeterminate order).
    ExpectCondition([this, added_token]() {
      return error_occurred_ ||
             (received_gain_token_ == added_token && !isnan(received_system_gain_db_));
    });

    // Verify the device gain notification
    ASSERT_NE(received_gain_token_, kInvalidDeviceToken);
    EXPECT_EQ(expect_gain_db, received_gain_info_.gain_db);
    EXPECT_EQ(expect_mute,
              ((received_gain_info_.flags & fuchsia::media::AudioGainInfoFlag_Mute) != 0));
  }

  // Verify the system gain notification
  ASSERT_FALSE(isnan(received_system_gain_db_));
  EXPECT_EQ(expect_gain_db, received_system_gain_db_);
  EXPECT_EQ(expect_mute, received_system_mute_);
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

TEST_F(VirtualAudioSystemGainTest, OnDeviceGainChangedMatchesAddInputSetSystemGain) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(true, true);
}

TEST_F(VirtualAudioSystemGainTest, OnDeviceGainChangedMatchesAddOutputSetSystemGain) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(false, true);
}

TEST_F(VirtualAudioSystemGainTest, OnDeviceGainChangedMatchesAddInputSetSystemMute) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(true, false);
}

TEST_F(VirtualAudioSystemGainTest, OnDeviceGainChangedMatchesAddOutputSetSystemMute) {
  TestOnDeviceGainChangedAfterChangeSystemGainMute(false, false);
}

}  // namespace media::audio::test
