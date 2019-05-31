// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/audio_device_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>

#include <cmath>
#include <cstring>
#include <utility>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/test/audio_tests_shared.h"

namespace media::audio::test {

//
// AudioDeviceTest static variables
//
std::unique_ptr<sys::ComponentContext> AudioDeviceTest::startup_context_;
fuchsia::virtualaudio::ControlSyncPtr AudioDeviceTest::control_sync_;

uint16_t AudioDeviceTest::initial_input_device_count_ = kInvalidDeviceCount;
uint16_t AudioDeviceTest::initial_output_device_count_ = kInvalidDeviceCount;
uint64_t AudioDeviceTest::initial_input_default_ = ZX_KOID_INVALID;
uint64_t AudioDeviceTest::initial_output_default_ = ZX_KOID_INVALID;
float AudioDeviceTest::initial_input_gain_db_ = NAN;
float AudioDeviceTest::initial_output_gain_db_ = NAN;
uint32_t AudioDeviceTest::initial_input_gain_flags_ = 0;
uint32_t AudioDeviceTest::initial_output_gain_flags_ = 0;

//
// AudioDeviceTest implementation
//

// static
void AudioDeviceTest::SetStartupContext(
    std::unique_ptr<sys::ComponentContext> startup_context) {
  startup_context_ = std::move(startup_context);
}

// static
void AudioDeviceTest::SetControl(
    fuchsia::virtualaudio::ControlSyncPtr control_sync) {
  AudioDeviceTest::control_sync_ = std::move(control_sync);
}

// static
void AudioDeviceTest::ResetVirtualDevices() {
  DisableVirtualDevices();
  zx_status_t status = control_sync_->Enable();
  ASSERT_EQ(status, ZX_OK);
}

// static
void AudioDeviceTest::DisableVirtualDevices() {
  zx_status_t status = control_sync_->Disable();
  ASSERT_EQ(status, ZX_OK);

  uint32_t num_inputs = -1, num_outputs = -1, num_tries = 0;
  do {
    status = control_sync_->GetNumDevices(&num_inputs, &num_outputs);
    ASSERT_EQ(status, ZX_OK);

    ++num_tries;
  } while ((num_inputs != 0 || num_outputs != 0) && num_tries < 100);
  ASSERT_EQ(num_inputs, 0u);
  ASSERT_EQ(num_outputs, 0u);
}

// static
void AudioDeviceTest::TearDownTestSuite() { DisableVirtualDevices(); }

void AudioDeviceTest::SetUp() {
  gtest::RealLoopFixture::SetUp();

  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };

  startup_context_->svc()->Connect(audio_dev_enum_.NewRequest());
  audio_dev_enum_.set_error_handler(err_handler);
}

void AudioDeviceTest::TearDown() {
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_TRUE(audio_dev_enum_.is_bound());
  audio_dev_enum_.Unbind();

  gtest::RealLoopFixture::TearDown();
}

void AudioDeviceTest::ExpectCallback() {
  received_callback_ = false;
  received_device_ = kInvalidDeviceInfo;
  received_removed_token_ = kInvalidDeviceToken;
  received_default_token_ = received_old_token_ = kInvalidDeviceToken;
  received_gain_token_ = kInvalidDeviceToken;
  received_gain_info_ = kInvalidGainInfo;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_callback_; },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_TRUE(audio_dev_enum_.is_bound());

  EXPECT_FALSE(timed_out) << kTimeoutErr;
  EXPECT_TRUE(received_callback_);
}

void AudioDeviceTest::SetOnDeviceAddedEvent() {
  audio_dev_enum_.events().OnDeviceAdded =
      [this](fuchsia::media::AudioDeviceInfo dev) {
        received_callback_ = true;
        received_device_ = std::move(dev);
      };
}

void AudioDeviceTest::SetOnDeviceRemovedEvent() {
  audio_dev_enum_.events().OnDeviceRemoved = [this](uint64_t token_id) {
    received_callback_ = true;
    received_removed_token_ = token_id;
  };
}

void AudioDeviceTest::SetOnDeviceGainChangedEvent() {
  audio_dev_enum_.events().OnDeviceGainChanged =
      [this](uint64_t dev_token, fuchsia::media::AudioGainInfo dev_gain_info) {
        received_callback_ = true;
        received_gain_token_ = dev_token;
        received_gain_info_ = dev_gain_info;
      };
}

void AudioDeviceTest::SetOnDefaultDeviceChangedEvent() {
  audio_dev_enum_.events().OnDefaultDeviceChanged =
      [this](uint64_t old_default_token, uint64_t new_default_token) {
        received_callback_ = true;
        received_default_token_ = new_default_token;
        received_old_token_ = old_default_token;
      };
}

uint32_t AudioDeviceTest::GainFlagsFromBools(bool can_mute, bool cur_mute,
                                             bool can_agc, bool cur_agc) {
  return ((can_mute && cur_mute) ? fuchsia::media::AudioGainInfoFlag_Mute
                                 : 0u) |
         (can_agc ? fuchsia::media::AudioGainInfoFlag_AgcSupported : 0u) |
         ((can_agc && cur_agc) ? fuchsia::media::AudioGainInfoFlag_AgcEnabled
                               : 0u);
}

uint32_t AudioDeviceTest::SetFlagsFromBools(bool set_gain, bool set_mute,
                                            bool set_agc) {
  return (set_gain ? fuchsia::media::SetAudioGainFlag_GainValid : 0u) |
         (set_mute ? fuchsia::media::SetAudioGainFlag_MuteValid : 0u) |
         (set_agc ? fuchsia::media::SetAudioGainFlag_AgcValid : 0u);
}

void AudioDeviceTest::RetrieveDefaultDevInfoUsingGetDevices(bool get_input) {
  audio_dev_enum_->GetDevices(
      [this,
       get_input](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        received_callback_ = true;

        for (auto& dev : devices) {
          if (dev.is_default && (dev.is_input == get_input)) {
            received_device_ = dev;
          }
        }
      });

  ExpectCallback();
}

void AudioDeviceTest::RetrieveGainInfoUsingGetDevices(uint64_t token) {
  audio_dev_enum_->GetDevices(
      [this,
       token](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        received_callback_ = true;

        for (auto& dev : devices) {
          if (dev.token_id == token) {
            received_gain_info_ = dev.gain_info;
          }
        }
      });

  ExpectCallback();
}

void AudioDeviceTest::RetrieveGainInfoUsingGetDeviceGain(uint64_t token,
                                                         bool valid_token) {
  audio_dev_enum_->GetDeviceGain(
      token,
      [this](uint64_t dev_token, fuchsia::media::AudioGainInfo dev_gain_info) {
        received_callback_ = true;
        received_gain_token_ = dev_token;
        received_gain_info_ = dev_gain_info;
      });

  ExpectCallback();
  EXPECT_EQ(received_gain_token_, (valid_token ? token : ZX_KOID_INVALID));
}

void AudioDeviceTest::RetrieveTokenUsingGetDefault(bool is_input) {
  auto get_default_handler = [this](uint64_t device_token) {
    received_callback_ = true;
    received_default_token_ = device_token;
  };

  if (is_input) {
    audio_dev_enum_->GetDefaultInputDevice(get_default_handler);
  } else {
    audio_dev_enum_->GetDefaultOutputDevice(get_default_handler);
  }

  ExpectCallback();
}

void AudioDeviceTest::RetrievePreExistingDevices() {
  if (AudioDeviceTest::initial_input_device_count_ != kInvalidDeviceCount &&
      AudioDeviceTest::initial_output_device_count_ != kInvalidDeviceCount) {
    return;
  }

  // Wait for any completion (not disconnect) callbacks to drain, then go on.
  RunLoopUntilIdle();

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_TRUE(audio_dev_enum_.is_bound());

  audio_dev_enum_->GetDevices(
      [this](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        received_callback_ = true;
        AudioDeviceTest::initial_input_device_count_ = 0;
        AudioDeviceTest::initial_output_device_count_ = 0;

        for (auto& dev : devices) {
          if (dev.is_input) {
            ++AudioDeviceTest::initial_input_device_count_;
            if (dev.is_default) {
              AudioDeviceTest::initial_input_default_ = dev.token_id;
              AudioDeviceTest::initial_input_gain_db_ = dev.gain_info.gain_db;
              AudioDeviceTest::initial_input_gain_flags_ = dev.gain_info.flags;
            }
          } else {
            ++AudioDeviceTest::initial_output_device_count_;
            if (dev.is_default) {
              AudioDeviceTest::initial_output_default_ = dev.token_id;
              AudioDeviceTest::initial_output_gain_db_ = dev.gain_info.gain_db;
              AudioDeviceTest::initial_output_gain_flags_ = dev.gain_info.flags;
            }
          }
        }
      });

  ExpectCallback();
}

bool AudioDeviceTest::HasPreExistingDevices() {
  RetrievePreExistingDevices();

  EXPECT_NE(AudioDeviceTest::initial_input_device_count_, kInvalidDeviceCount);
  EXPECT_NE(AudioDeviceTest::initial_output_device_count_, kInvalidDeviceCount);

  return ((AudioDeviceTest::initial_input_device_count_ +
           AudioDeviceTest::initial_output_device_count_) > 0);
}

//
// AudioDeviceTest test cases
//

// Basic validation: we don't disconnect and callback is delivered.
// Later tests use RetrievePreExistingDevices which further validates
// GetDevices().
TEST_F(AudioDeviceTest, ReceivesGetDevicesCallback) {
  audio_dev_enum_->GetDevices(
      [this](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        received_callback_ = true;
      });

  ExpectCallback();
}

TEST_F(AudioDeviceTest, GetDevicesHandlesLackOfDevices) {
  if (HasPreExistingDevices()) {
    FXL_DLOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }

  uint16_t num_devs = kInvalidDeviceCount;
  audio_dev_enum_->GetDevices(
      [this,
       &num_devs](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        received_callback_ = true;
        num_devs = devices.size();
      });

  ExpectCallback();
  EXPECT_EQ(num_devs, 0u);
}

TEST_F(AudioDeviceTest, GetDefaultInputDeviceHandlesLackOfDevices) {
  if (HasPreExistingDevices()) {
    FXL_DLOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }
  RetrieveTokenUsingGetDefault(true);
  EXPECT_EQ(received_default_token_, ZX_KOID_INVALID);
}

TEST_F(AudioDeviceTest, GetDefaultOutputDeviceHandlesLackOfDevices) {
  if (HasPreExistingDevices()) {
    FXL_DLOG(INFO) << "Test case requires an environment with no audio devices";
    return;
  }
  RetrieveTokenUsingGetDefault(false);
  EXPECT_EQ(received_default_token_, ZX_KOID_INVALID);
}

// Given invalid token to GetDeviceGain, callback should be received with
// ZX_KOID_INVALID device; FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, GetDeviceGainHandlesNullToken) {
  RetrieveGainInfoUsingGetDeviceGain(ZX_KOID_INVALID);
}

// Given invalid token to GetDeviceGain, callback should be received with
// ZX_KOID_INVALID device; FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, GetDeviceGainHandlesBadToken) {
  RetrieveGainInfoUsingGetDeviceGain(kInvalidDeviceToken, false);
}

// Given null token to GetDeviceGain, FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, SetDeviceGainHandlesNullToken) {
  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID,
                                 {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

// Given invalid token to SetDeviceGain, FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, SetDeviceGainHandlesBadToken) {
  audio_dev_enum_->SetDeviceGain(kInvalidDeviceToken,
                                 {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

// Given invalid token to GetDeviceGain, callback should be received with
// ZX_KOID_INVALID device; FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, OnDeviceGainChangedIgnoresSetDeviceGainNullToken) {
  SetOnDeviceGainChangedEvent();

  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID,
                                 {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

TEST_F(AudioDeviceTest, OnDeviceGainChangedIgnoresSetDeviceGainBadToken) {
  SetOnDeviceGainChangedEvent();

  audio_dev_enum_->SetDeviceGain(kInvalidDeviceToken,
                                 {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

}  // namespace media::audio::test
