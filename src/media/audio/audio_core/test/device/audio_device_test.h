// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_DEVICE_AUDIO_DEVICE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_DEVICE_AUDIO_DEVICE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

constexpr uint32_t kGainFlagMask = fuchsia::media::AudioGainInfoFlag_Mute |
                                   fuchsia::media::AudioGainInfoFlag_AgcSupported |
                                   fuchsia::media::AudioGainInfoFlag_AgcEnabled;
constexpr uint32_t kSetFlagMask = fuchsia::media::SetAudioGainFlag_GainValid |
                                  fuchsia::media::SetAudioGainFlag_MuteValid |
                                  fuchsia::media::SetAudioGainFlag_AgcValid;

// We set vars to these values before async callbacks, to detect no-response.
constexpr uint16_t kInvalidDeviceCount = -1;
constexpr uint64_t kInvalidDeviceToken = -1;
constexpr fuchsia::media::AudioGainInfo kInvalidGainInfo = {.gain_db = NAN,
                                                            .flags = ~kGainFlagMask};
const fuchsia::media::AudioDeviceInfo kInvalidDeviceInfo = {
    .name = "Invalid name",
    .unique_id = "Invalid unique_id (len 32 chars)",
    .token_id = kInvalidDeviceToken,
    .is_input = true,
    .gain_info = kInvalidGainInfo,
    .is_default = true};

class AudioDeviceTest : public HermeticAudioTest {
 protected:
  static std::string PopulateUniqueIdStr(const std::array<uint8_t, 16>& unique_id);

  void SetUp() override;
  void TearDown() override;
  void ExpectCallback() override;

  void SetOnDeviceAddedEvent();
  void SetOnDeviceRemovedEvent();
  void SetOnDeviceGainChangedEvent();
  void SetOnDefaultDeviceChangedEvent();

  // These methods wait for the four AudioDeviceEnumerator events (or error),
  // explicitly tolerating other callbacks on the async loop before the event
  // for the specified device is received. Each call resets the relevant "what
  // we received" state variables to default values and handles error_occurred_.
  virtual void ExpectDeviceAdded(const std::array<uint8_t, 16>& unique_id);
  virtual void ExpectDeviceRemoved(uint64_t remove_token);
  void ExpectDefaultChanged(uint64_t default_token);
  void ExpectGainChanged(uint64_t gain_token);

  uint32_t GainFlagsFromBools(bool cur_mute, bool cur_agc, bool can_mute, bool can_agc);
  uint32_t SetFlagsFromBools(bool set_gain, bool set_mute, bool set_agc);

  void RetrieveDefaultDevInfoUsingGetDevices(bool get_input);
  void RetrieveGainInfoUsingGetDevices(uint64_t token);
  void RetrieveGainInfoUsingGetDeviceGain(uint64_t token, bool valid_token = true);
  void RetrieveTokenUsingGetDefault(bool is_input);
  void RetrievePreExistingDevices();
  bool HasPreExistingDevices();

  // These are set the first time RetrievePreExistingDevices is called.
  static uint16_t initial_input_device_count_;
  static uint16_t initial_output_device_count_;
  static uint64_t initial_input_default_;
  static uint64_t initial_output_default_;
  static float initial_input_gain_db_;
  static float initial_output_gain_db_;
  static uint32_t initial_input_gain_flags_;
  static uint32_t initial_output_gain_flags_;

  fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum_;

  // The six "what we received" state variables listed below are reset to these
  // default values, upon each call to ExpectCallback().
  //
  // Set by GetDevices and OnDeviceAdded.
  fuchsia::media::AudioDeviceInfo received_device_ = kInvalidDeviceInfo;

  // Set by OnDeviceRemoved.
  uint64_t received_removed_token_ = kInvalidDeviceToken;

  // Set by GetDeviceGain and OnDeviceGainChanged.
  uint64_t received_gain_token_ = kInvalidDeviceToken;

  // Set by GetDeviceGain, OnDeviceGainChanged and some usages of GetDevices.
  fuchsia::media::AudioGainInfo received_gain_info_ = kInvalidGainInfo;

  // Set by GetDefaultInputDevice, GetDefaultOutputDevice,
  // OnDefaultDeviceChanged and some usages of GetDevices.
  uint64_t received_default_token_ = kInvalidDeviceToken;

  // Set by OnDefaultDeviceChanged.
  uint64_t received_old_token_ = kInvalidDeviceToken;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_DEVICE_AUDIO_DEVICE_TEST_H_
