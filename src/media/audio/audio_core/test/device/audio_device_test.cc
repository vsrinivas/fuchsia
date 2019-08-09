// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/device/audio_device_test.h"

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>
#include <cstring>
#include <utility>

#include "src/lib/fxl/logging.h"

namespace media::audio::test {

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
// Convert 16-byte arr to equivalent 32-char str (as returned by get_devices).
std::string AudioDeviceTest::PopulateUniqueIdStr(const std::array<uint8_t, 16>& unique_id) {
  char unique_id_chars[33];

  for (uint8_t i = 0; i < 16; ++i) {
    sprintf(unique_id_chars + (i * 2), "%02hhx", unique_id[i]);
  }

  return std::string{unique_id_chars};
}

void AudioDeviceTest::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_dev_enum_.NewRequest());
  audio_dev_enum_.set_error_handler(ErrorHandler());
}

void AudioDeviceTest::TearDown() {
  EXPECT_TRUE(audio_dev_enum_.is_bound());
  audio_dev_enum_.Unbind();

  HermeticAudioTest::TearDown();
}

void AudioDeviceTest::ExpectCallback() {
  received_device_ = kInvalidDeviceInfo;
  received_removed_token_ = kInvalidDeviceToken;
  received_default_token_ = received_old_token_ = kInvalidDeviceToken;
  received_gain_token_ = kInvalidDeviceToken;
  received_gain_info_ = kInvalidGainInfo;

  HermeticAudioTest::ExpectCallback();

  EXPECT_TRUE(audio_dev_enum_.is_bound());
}

void AudioDeviceTest::SetOnDeviceAddedEvent() {
  audio_dev_enum_.events().OnDeviceAdded = CompletionCallback(
      [this](fuchsia::media::AudioDeviceInfo dev) { received_device_ = std::move(dev); });
}

void AudioDeviceTest::SetOnDeviceRemovedEvent() {
  audio_dev_enum_.events().OnDeviceRemoved =
      CompletionCallback([this](uint64_t token_id) { received_removed_token_ = token_id; });
}

void AudioDeviceTest::SetOnDeviceGainChangedEvent() {
  audio_dev_enum_.events().OnDeviceGainChanged =
      CompletionCallback([this](uint64_t dev_token, fuchsia::media::AudioGainInfo dev_gain_info) {
        received_gain_token_ = dev_token;
        received_gain_info_ = dev_gain_info;
      });
}

void AudioDeviceTest::SetOnDefaultDeviceChangedEvent() {
  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([this](uint64_t old_default_token, uint64_t new_default_token) {
        received_default_token_ = new_default_token;
        received_old_token_ = old_default_token;
      });
}

// Wait for device (specified by unique ID) to be added, accommodating other
// async callbacks until then.
void AudioDeviceTest::ExpectDeviceAdded(const std::array<uint8_t, 16>& unique_id_arr) {
  received_device_ = kInvalidDeviceInfo;
  auto unique_id_str = PopulateUniqueIdStr(unique_id_arr);

  // Our parent TestFixture class uses infinite for polling granularity, which
  // guarantees that we only process one callback at a time. By checking against
  // unique_id, we should be fully reliable in the face of multiple device adds.
  ExpectCondition([this, &unique_id_str]() {
    return error_occurred_ || (unique_id_str.compare(received_device_.unique_id) == 0);
  });

  ASSERT_FALSE(error_occurred_);
  ASSERT_NE(received_device_.token_id, kInvalidDeviceToken);
}

// Wait for device (specified by token) to be removed, accommodating other async
// callbacks until then.
void AudioDeviceTest::ExpectDeviceRemoved(uint64_t remove_token) {
  received_removed_token_ = kInvalidDeviceToken;

  ExpectCondition([this, remove_token]() {
    return error_occurred_ || (received_removed_token_ == remove_token);
  });

  ASSERT_FALSE(error_occurred_);
  ASSERT_NE(received_removed_token_, kInvalidDeviceToken);
}

// Wait for device (specified by token) to become the default, accommodating
// other async callbacks until then.
void AudioDeviceTest::ExpectDefaultChanged(uint64_t default_token) {
  received_default_token_ = kInvalidDeviceToken;

  ExpectCondition([this, default_token]() {
    return error_occurred_ || (received_default_token_ == default_token);
  });

  ASSERT_FALSE(error_occurred_);
  ASSERT_NE(received_default_token_, kInvalidDeviceToken);
}

// Wait for device (specified by token) to report a gain change, accommodating
// other async callbacks until then.
void AudioDeviceTest::ExpectGainChanged(uint64_t gain_token) {
  received_gain_token_ = kInvalidDeviceToken;
  received_gain_info_ = kInvalidGainInfo;

  ExpectCondition(
      [this, gain_token]() { return error_occurred_ || (received_gain_token_ == gain_token); });

  ASSERT_FALSE(error_occurred_);
  ASSERT_NE(received_gain_token_, kInvalidDeviceToken);
}

uint32_t AudioDeviceTest::GainFlagsFromBools(bool can_mute, bool cur_mute, bool can_agc,
                                             bool cur_agc) {
  return ((can_mute && cur_mute) ? fuchsia::media::AudioGainInfoFlag_Mute : 0u) |
         (can_agc ? fuchsia::media::AudioGainInfoFlag_AgcSupported : 0u) |
         ((can_agc && cur_agc) ? fuchsia::media::AudioGainInfoFlag_AgcEnabled : 0u);
}

uint32_t AudioDeviceTest::SetFlagsFromBools(bool set_gain, bool set_mute, bool set_agc) {
  return (set_gain ? fuchsia::media::SetAudioGainFlag_GainValid : 0u) |
         (set_mute ? fuchsia::media::SetAudioGainFlag_MuteValid : 0u) |
         (set_agc ? fuchsia::media::SetAudioGainFlag_AgcValid : 0u);
}

void AudioDeviceTest::RetrieveDefaultDevInfoUsingGetDevices(bool get_input) {
  received_device_ = kInvalidDeviceInfo;

  audio_dev_enum_->GetDevices(CompletionCallback(
      [this, get_input](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        for (auto& dev : devices) {
          if (dev.is_default && (dev.is_input == get_input)) {
            received_device_ = dev;
          }
        }
      }));

  ExpectCondition(
      [this]() { return error_occurred_ || (received_device_.token_id != kInvalidDeviceToken); });

  ASSERT_FALSE(error_occurred_);
  ASSERT_NE(received_device_.token_id, kInvalidDeviceToken);
}

void AudioDeviceTest::RetrieveGainInfoUsingGetDevices(uint64_t gain_token) {
  audio_dev_enum_->GetDevices(CompletionCallback(
      [this, gain_token](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        for (auto& dev : devices) {
          if (dev.token_id == gain_token) {
            received_gain_token_ = dev.token_id;
            received_gain_info_ = dev.gain_info;
          }
        }
      }));

  ExpectGainChanged(gain_token);
}

void AudioDeviceTest::RetrieveGainInfoUsingGetDeviceGain(uint64_t gain_token, bool valid_token) {
  audio_dev_enum_->GetDeviceGain(
      gain_token,
      CompletionCallback([this](uint64_t dev_token, fuchsia::media::AudioGainInfo dev_gain_info) {
        received_gain_token_ = dev_token;
        received_gain_info_ = dev_gain_info;
      }));

  auto expect_token = (valid_token ? gain_token : ZX_KOID_INVALID);
  ExpectGainChanged(expect_token);
}

void AudioDeviceTest::RetrieveTokenUsingGetDefault(bool is_input) {
  received_default_token_ = kInvalidDeviceToken;
  auto get_default_handler =
      CompletionCallback([this](uint64_t device_token) { received_default_token_ = device_token; });

  if (is_input) {
    audio_dev_enum_->GetDefaultInputDevice(get_default_handler);
  } else {
    audio_dev_enum_->GetDefaultOutputDevice(get_default_handler);
  }

  ExpectCondition(
      [this]() { return error_occurred_ || (received_default_token_ != kInvalidDeviceToken); });

  ASSERT_FALSE(error_occurred_);
  EXPECT_NE(received_default_token_, kInvalidDeviceToken);
}

void AudioDeviceTest::RetrievePreExistingDevices() {
  if (AudioDeviceTest::initial_input_device_count_ != kInvalidDeviceCount &&
      AudioDeviceTest::initial_output_device_count_ != kInvalidDeviceCount) {
    return;
  }

  // Wait for any completion (not disconnect) callbacks to drain, then go on.
  RunLoopUntilIdle();

  EXPECT_FALSE(error_occurred_) << kDisconnectErr;
  EXPECT_TRUE(audio_dev_enum_.is_bound());

  audio_dev_enum_->GetDevices(
      CompletionCallback([](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
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
      }));

  ExpectCondition([this]() {
    return error_occurred_ ||
           (AudioDeviceTest::initial_input_device_count_ != kInvalidDeviceCount &&
            AudioDeviceTest::initial_output_device_count_ != kInvalidDeviceCount);
  });

  ASSERT_FALSE(error_occurred_);
  EXPECT_NE(AudioDeviceTest::initial_input_device_count_, kInvalidDeviceCount);
  EXPECT_NE(AudioDeviceTest::initial_output_device_count_, kInvalidDeviceCount);
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
      CompletionCallback([](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {}));

  ExpectCallback();
}

TEST_F(AudioDeviceTest, GetDevicesHandlesLackOfDevices) {
  uint16_t num_devs = kInvalidDeviceCount;
  audio_dev_enum_->GetDevices(
      CompletionCallback([&num_devs](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        num_devs = devices.size();
      }));

  ExpectCallback();
  EXPECT_EQ(num_devs, 0u);
}

TEST_F(AudioDeviceTest, GetDefaultInputDeviceHandlesLackOfDevices) {
  RetrieveTokenUsingGetDefault(true);
  EXPECT_EQ(received_default_token_, ZX_KOID_INVALID);
}

TEST_F(AudioDeviceTest, GetDefaultOutputDeviceHandlesLackOfDevices) {
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
  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID, {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

// Given invalid token to SetDeviceGain, FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, SetDeviceGainHandlesBadToken) {
  audio_dev_enum_->SetDeviceGain(kInvalidDeviceToken, {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

// Given invalid token to GetDeviceGain, callback should be received with
// ZX_KOID_INVALID device; FIDL interface should not disconnect.
TEST_F(AudioDeviceTest, OnDeviceGainChangedIgnoresSetDeviceGainNullToken) {
  SetOnDeviceGainChangedEvent();

  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID, {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

TEST_F(AudioDeviceTest, OnDeviceGainChangedIgnoresSetDeviceGainBadToken) {
  SetOnDeviceGainChangedEvent();

  audio_dev_enum_->SetDeviceGain(kInvalidDeviceToken, {.gain_db = 0.0f, .flags = 0u},
                                 fuchsia::media::SetAudioGainFlag_GainValid);
  RunLoopUntilIdle();
}

}  // namespace media::audio::test
