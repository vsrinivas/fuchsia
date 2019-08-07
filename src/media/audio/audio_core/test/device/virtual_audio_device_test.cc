// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/device/virtual_audio_device_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/clock.h>

#include <cmath>
#include <cstring>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/test/device/audio_device_test.h"

namespace media::audio::test {

//
// VirtualAudioDeviceTest static members
//
fuchsia::virtualaudio::ControlSyncPtr VirtualAudioDeviceTest::control_sync_;
AtomicDeviceId VirtualAudioDeviceTest::sequential_devices_;

// static
// Generate a unique id array for each virtual device created during the
// lifetime of this binary. In the MSB (byte [0]), place F0 for output device or
// F1 for input device. In bytes [8] thru [15], place a monotonically
// incrementing atomic value, split into bytes. Thus, the very first device, if
// an input, would have a unique_id of F1000000 00000000 00000000 00000001.
void VirtualAudioDeviceTest::PopulateUniqueIdArr(bool is_input, uint8_t* unique_id_arr) {
  uint64_t sequential_value = sequential_devices_.Next();

  unique_id_arr[0] = (is_input ? 0xF1 : 0xF0);

  unique_id_arr[8] = (sequential_value >> 56) & 0x0FF;
  unique_id_arr[9] = (sequential_value >> 48) & 0x0FF;
  unique_id_arr[10] = (sequential_value >> 40) & 0x0FF;
  unique_id_arr[11] = (sequential_value >> 32) & 0x0FF;
  unique_id_arr[12] = (sequential_value >> 24) & 0x0FF;
  unique_id_arr[13] = (sequential_value >> 16) & 0x0FF;
  unique_id_arr[14] = (sequential_value >> 8) & 0x0FF;
  unique_id_arr[15] = sequential_value & 0x0FF;
}

void VirtualAudioDeviceTest::SetUpTestSuite() {
  AudioDeviceTest::SetUpTestSuite();
  environment()->ConnectToService(control_sync_.NewRequest());
  control_sync_->Enable();
}

void VirtualAudioDeviceTest::TearDownTestSuite() {
  ASSERT_TRUE(control_sync_.is_bound());
  control_sync_->Disable();
  AudioDeviceTest::TearDownTestSuite();
}

//
// VirtualAudioDeviceTest implementation
//
void VirtualAudioDeviceTest::SetUp() {
  AudioDeviceTest::SetUp();

  environment()->ConnectToService(input_.NewRequest());
  input_.set_error_handler(ErrorHandler());
  environment()->ConnectToService(input_2_.NewRequest());
  input_2_.set_error_handler(ErrorHandler());

  environment()->ConnectToService(output_.NewRequest());
  output_.set_error_handler(ErrorHandler());
  environment()->ConnectToService(output_2_.NewRequest());
  output_2_.set_error_handler(ErrorHandler());

  // Enable virtual devices and assert there are none that already exist. Any virtual devices that
  // may exist here would indicate a failure to cleanup from a previous test case.
  ASSERT_FALSE(HasPreExistingDevices());
}

void VirtualAudioDeviceTest::TearDown() {
  EXPECT_TRUE(input_.is_bound());
  EXPECT_TRUE(input_2_.is_bound());
  EXPECT_TRUE(output_.is_bound());
  EXPECT_TRUE(output_2_.is_bound());

  input_.Unbind();
  input_2_.Unbind();
  output_.Unbind();
  output_2_.Unbind();

  WaitForVirtualDeviceDepartures();
  AudioDeviceTest::TearDown();
}

void VirtualAudioDeviceTest::WaitForVirtualDeviceDepartures() {
  audio_dev_enum_.events().OnDeviceRemoved =
      CompletionCallback([this](uint64_t token_id) { virtual_device_tokens_.erase(token_id); });

  ExpectCondition([this]() { return error_occurred_ || virtual_device_tokens_.empty(); });
}

void VirtualAudioDeviceTest::ExpectDeviceAdded(const std::array<uint8_t, 16>& unique_id_arr) {
  AudioDeviceTest::ExpectDeviceAdded(unique_id_arr);

  virtual_device_tokens_.insert(received_device_.token_id);
}

void VirtualAudioDeviceTest::ExpectDeviceRemoved(uint64_t remove_token) {
  AudioDeviceTest::ExpectDeviceRemoved(remove_token);

  virtual_device_tokens_.erase(received_removed_token_);
}

// Using virtualaudio, validate that device list matches what was added.
// Note: presently, just being Added doesn't necessarily make you the default!
void VirtualAudioDeviceTest::TestGetDevicesAfterAdd(bool is_input) {
  std::string mfr = "Gemstone Testing";
  std::string product = "Virtual Delight";

  std::array<uint8_t, 16> unique_id{0};
  for (uint8_t i = 0; i < 16; ++i) {
    unique_id[i] = i * 0x11 + (is_input ? 1 : 0);
  }

  float min_gain_db = -68.0f, max_gain_db = 1.0f, gain_step_db = 0.25f;
  float cur_gain_db = -10.0f;
  bool can_mute = false, cur_mute = true, can_agc = false, cur_agc = true;

  SetOnDeviceAddedEvent();
  if (is_input) {
    input_->SetManufacturer(mfr);
    input_->SetProduct(product);
    input_->SetUniqueId(unique_id);

    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);
    input_->Add();
  } else {
    output_->SetManufacturer(mfr);
    output_->SetProduct(product);
    output_->SetUniqueId(unique_id);

    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);
    output_->Add();

    // Output AGC is not supported on output devices; can_agc and cur_agc will
    // always be false. System mute is enabled on all output devices, even those
    // that don't support hardware-based mute. Finally, all new output devices
    // (those without a settings file) are set to unmuted -12dB.
    can_agc = false;
    cur_agc = false;
    can_mute = true;
    cur_mute = false;
    cur_gain_db = -12.0f;
  }
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  uint16_t num_devs = kInvalidDeviceCount;
  audio_dev_enum_->GetDevices(CompletionCallback(
      [this, added_token, &num_devs](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        num_devs = devices.size();

        for (auto& dev : devices) {
          if (added_token == dev.token_id) {
            received_device_ = dev;
            return;
          }
        }
      }));

  ExpectDeviceAdded(unique_id);
  EXPECT_NE(num_devs, kInvalidDeviceCount);

  // Compare every piece of the AudioDeviceInfo that we retrieved.
  EXPECT_NE(received_device_.token_id, ZX_KOID_INVALID);
  EXPECT_EQ(received_device_.name, mfr + " " + product);

  EXPECT_EQ(received_device_.is_input, is_input);
  EXPECT_EQ(received_device_.gain_info.gain_db, cur_gain_db);
  EXPECT_EQ(received_device_.gain_info.flags,
            GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc));

  // We may have preexisting devices (real hardware), so we can't just assert
  // that there is now one device in the list.
  // Our device count should now be exactly one more than our initial count.
  size_t preexisting_device_count =
      AudioDeviceTest::initial_input_device_count_ + AudioDeviceTest::initial_output_device_count_;
  size_t expected_num_devs = preexisting_device_count + 1;
  EXPECT_EQ(num_devs, expected_num_devs);
}

// Upon exit, received_default_token_ contains the newest device, and
// received_old_token_ contains the second-newest device.
void VirtualAudioDeviceTest::AddTwoDevices(bool is_input, bool is_plugged) {
  auto now = zx::clock::get_monotonic().get();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  // Add the devices
  SetOnDeviceAddedEvent();
  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->SetPlugProperties(now - ZX_NSEC(3), false, false, true);

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->SetPlugProperties(now - ZX_NSEC(3), false, false, true);

    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  // Save this for later
  auto added_first_token = received_device_.token_id;

  PopulateUniqueIdArr(is_input, unique_id.data());

  if (is_input) {
    input_2_->SetUniqueId(unique_id);
    input_2_->SetPlugProperties(now - ZX_NSEC(2), false, false, true);

    input_2_->Add();
  } else {
    output_2_->SetUniqueId(unique_id);
    output_2_->SetPlugProperties(now - ZX_NSEC(2), false, false, true);

    output_2_->Add();
  }
  ExpectDeviceAdded(unique_id);

  // Save this for later
  auto added_second_token = received_device_.token_id;

  if (is_plugged) {
    // Make sure the default order is correct
    SetOnDefaultDeviceChangedEvent();
    if (is_input) {
      input_->ChangePlugState(now - ZX_NSEC(1), true);
    } else {
      output_->ChangePlugState(now - ZX_NSEC(1), true);
    }
    ExpectDefaultChanged(added_first_token);

    if (is_input) {
      input_2_->ChangePlugState(now, true);
    } else {
      output_2_->ChangePlugState(now, true);
    }
    ExpectDefaultChanged(added_second_token);
    ASSERT_EQ(received_old_token_, added_first_token);
  } else {
    received_default_token_ = added_second_token;
    received_old_token_ = added_first_token;
  }
}

// To test GetDevices after a device removal, we first add two devices, then
// remove one (and see if GetDevices reflects the removal). Why? Certain error
// modes emerge when the removed-device is NOT the final remaining device.
void VirtualAudioDeviceTest::TestGetDevicesAfterRemove(bool is_input, bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t expect_remove_token = (most_recent ? received_default_token_ : received_old_token_);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  SetOnDeviceRemovedEvent();
  if (most_recent) {
    if (is_input) {
      input_2_->Remove();
    } else {
      output_2_->Remove();
    }
  } else {
    if (is_input) {
      input_->Remove();
    } else {
      output_->Remove();
    }
  }
  // At this point, we've added two devices, then removed one.

  ExpectDeviceRemoved(expect_remove_token);

  uint16_t num_devs = kInvalidDeviceCount;
  received_device_ = kInvalidDeviceInfo;
  audio_dev_enum_->GetDevices(
      CompletionCallback([this, expect_remove_token,
                          &num_devs](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        num_devs = devices.size();

        for (auto& dev : devices) {
          if (dev.is_default) {
            received_default_token_ = dev.token_id;
          }

          // We should never enter this IF statement
          if (expect_remove_token == dev.token_id) {
            received_device_ = dev;
            return;
          }
        }
      }));

  // We should receive a GetDevices callback, but the device we just removed
  // should not be in the list.
  ExpectDefaultChanged(expect_default_token);
  EXPECT_NE(num_devs, kInvalidDeviceCount);
  EXPECT_EQ(received_device_.token_id, kInvalidDeviceToken);

  // We may have preexisting devices (real hardware), so we can't just assert
  // that there is now one device in the list.
  // Our device count should now be exactly one more than our initial count.
  size_t preexisting_device_count =
      AudioDeviceTest::initial_input_device_count_ + AudioDeviceTest::initial_output_device_count_;
  size_t expected_num_devs = preexisting_device_count + 1;
  EXPECT_EQ(num_devs, expected_num_devs);
}

void VirtualAudioDeviceTest::TestGetDevicesAfterUnplug(bool is_input, bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t to_unplug_token = (most_recent ? received_default_token_ : received_old_token_);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  SetOnDefaultDeviceChangedEvent();
  auto now = zx::clock::get_monotonic().get();
  if (most_recent) {
    if (is_input) {
      input_2_->ChangePlugState(now, false);
    } else {
      output_2_->ChangePlugState(now, false);
    }

    ExpectDefaultChanged(expect_default_token);
  } else {
    if (is_input) {
      input_->ChangePlugState(now, false);
    } else {
      output_->ChangePlugState(now, false);
    }
  }
  // At this point, we've added two devices, then unplugged one.

  uint16_t num_devs = kInvalidDeviceCount;
  received_device_ = kInvalidDeviceInfo;
  audio_dev_enum_->GetDevices(
      CompletionCallback([this, to_unplug_token,
                          &num_devs](const std::vector<fuchsia::media::AudioDeviceInfo>& devices) {
        num_devs = devices.size();

        for (auto& dev : devices) {
          if (dev.is_default) {
            received_default_token_ = dev.token_id;
          }

          if (to_unplug_token == dev.token_id) {
            received_device_ = dev;
          }
        }
      }));

  // We should receive callback, but a different device should be default.
  ExpectDefaultChanged(expect_default_token);

  EXPECT_EQ(received_device_.token_id, to_unplug_token);
  EXPECT_EQ(received_device_.is_input, is_input);
  EXPECT_EQ(received_device_.is_default, false);

  // We may have preexisting devices (real hardware), so we can't just assert
  // that there are now two devices in the list.
  // Our device count should now be exactly two more than our initial count.
  // Yes, the unplugged device should still show up in the list!
  size_t preexisting_device_count =
      AudioDeviceTest::initial_input_device_count_ + AudioDeviceTest::initial_output_device_count_;
  size_t expected_num_devs = preexisting_device_count + 2;
  EXPECT_EQ(num_devs, expected_num_devs);
}

void VirtualAudioDeviceTest::TestGetDefaultDeviceUsingAddGetDevices(bool is_input) {
  SetOnDeviceAddedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  RetrieveDefaultDevInfoUsingGetDevices(is_input);
  ASSERT_EQ(received_device_.token_id, added_token);

  RetrieveTokenUsingGetDefault(is_input);
  EXPECT_EQ(received_default_token_, added_token);
}

// validate callbacks received and default updated.
// TODO(mpuryear): test policy conditions: first Add, last Remove, subsequent
// Add, important Remove, unimportant Remove, Add(unplugged), plug change.
// Does plug status matter at all?
//
// From no-devices, GetDefault should recognize an added device as new default.
void VirtualAudioDeviceTest::TestGetDefaultDeviceAfterAdd(bool is_input) {
  SetOnDefaultDeviceChangedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  auto added_token = received_default_token_;

  RetrieveTokenUsingGetDefault(is_input);
  EXPECT_EQ(received_default_token_, added_token);
}

// From no-devices, adding an unplugged device should not make it the new
// default.
void VirtualAudioDeviceTest::TestGetDefaultDeviceAfterUnpluggedAdd(bool is_input) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  auto now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->SetPlugProperties(now, false, false, true);

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->SetPlugProperties(now, false, false, true);

    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  RetrieveTokenUsingGetDefault(is_input);
  EXPECT_NE(received_default_token_, added_token);
  EXPECT_EQ(received_default_token_, ZX_KOID_INVALID);
}

void VirtualAudioDeviceTest::TestGetDefaultDeviceAfterRemove(bool is_input, bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t expect_remove_token = (most_recent ? received_default_token_ : received_old_token_);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  SetOnDeviceRemovedEvent();
  if (most_recent) {
    SetOnDefaultDeviceChangedEvent();

    if (is_input) {
      input_2_->Remove();
    } else {
      output_2_->Remove();
    }

    ExpectDefaultChanged(expect_default_token);
    ASSERT_EQ(received_old_token_, expect_remove_token);

    ExpectDeviceRemoved(expect_remove_token);
  } else {
    if (is_input) {
      input_->Remove();
    } else {
      output_->Remove();
    }

    ExpectDeviceRemoved(expect_remove_token);
  }

  RetrieveTokenUsingGetDefault(is_input);
  EXPECT_EQ(received_default_token_, expect_default_token);
}

void VirtualAudioDeviceTest::TestGetDefaultDeviceAfterUnplug(bool is_input, bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  auto now = zx::clock::get_monotonic().get();
  SetOnDefaultDeviceChangedEvent();
  if (most_recent) {
    if (is_input) {
      input_2_->ChangePlugState(now, false);
    } else {
      output_2_->ChangePlugState(now, false);
    }

    ExpectDefaultChanged(expect_default_token);
  } else {
    if (is_input) {
      input_->ChangePlugState(now, false);
    } else {
      output_->ChangePlugState(now, false);
    }
  }

  RetrieveTokenUsingGetDefault(is_input);
  EXPECT_EQ(received_default_token_, expect_default_token);
}

// gain/mute/agc matches what was received by OnDeviceAdded?
void VirtualAudioDeviceTest::TestGetDeviceGainAfterAdd(bool is_input) {
  SetOnDeviceAddedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  float min_gain_db, max_gain_db, gain_step_db, cur_gain_db;
  bool can_mute, cur_mute, can_agc, cur_agc;

  if (is_input) {
    input_->SetUniqueId(unique_id);

    min_gain_db = -24.0f;
    max_gain_db = 0.0f;
    gain_step_db = 0.5f;
    cur_gain_db = -13.5f;
    can_mute = true;
    cur_mute = true;
    can_agc = true;
    cur_agc = false;
    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);
    input_->Add();
    // Our audio device manager allows input devices to expose AGC, and does not
    // automatically add a Mute node, so we don't expect the can_agc or can_mute
    // properties that we set here to be overridden (unlike with output
    // devices). Also, unlike with output devices, there is no System Gain for
    // input, so the device gain value that we set here will not be overridden
    // with a value of -12 dB.
    //
    // Both types of devices (input and output devices), however, will have
    // these values overridden by previously-cached values, if the unique ID
    // matches to one of the settings files found.
  } else {
    output_->SetUniqueId(unique_id);

    min_gain_db = -12.0f;
    max_gain_db = 1.0f;
    gain_step_db = 1.0f;
    cur_gain_db = -6.0f;
    can_mute = true;
    cur_mute = true;
    can_agc = false;
    cur_agc = false;
    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);
    output_->Add();

    // Output AGC is not supported on output devices; can_agc and cur_agc will
    // always be false. System mute is enabled on all output devices, even those
    // that don't support hardware-based mute. Finally, all new output devices
    // (those without a settings file) are set to unmuted -12dB.
    can_agc = false;
    cur_agc = false;
    can_mute = true;
    cur_mute = false;
    cur_gain_db = -12.0f;
  }

  uint32_t gain_flags = GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc);

  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  RetrieveGainInfoUsingGetDevices(added_token);
  EXPECT_EQ(received_gain_info_.gain_db, cur_gain_db);
  EXPECT_EQ(received_gain_info_.flags, gain_flags);

  RetrieveGainInfoUsingGetDeviceGain(added_token);
  EXPECT_EQ(received_gain_info_.gain_db, cur_gain_db);
  EXPECT_EQ(received_gain_info_.flags, gain_flags);
}

// From GetDeviceGain, does gain/mute/agc matches what was set?
void VirtualAudioDeviceTest::TestGetDeviceGainAfterSetDeviceGain(bool is_input) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  float min_gain_db, max_gain_db, gain_step_db, cur_gain_db;
  bool can_mute, cur_mute, can_agc, cur_agc;
  uint32_t gain_flags, set_flags;

  if (is_input) {
    input_->SetUniqueId(unique_id);

    min_gain_db = -24.0f;
    max_gain_db = 0.0f;
    gain_step_db = 0.5f;
    cur_gain_db = -13.5f;
    can_mute = true;
    cur_mute = false;
    can_agc = true;
    cur_agc = false;
    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);

    input_->Add();

    // After Add, we'll set gain to -3.5 dB and enable AGC and Mute.
    cur_gain_db = -3.5f;
    cur_mute = true;
    cur_agc = true;
    set_flags = fuchsia::media::SetAudioGainFlag_GainValid |
                fuchsia::media::SetAudioGainFlag_MuteValid |
                fuchsia::media::SetAudioGainFlag_AgcValid;
  } else {
    output_->SetUniqueId(unique_id);

    min_gain_db = -12.0f;
    max_gain_db = 1.0f;
    gain_step_db = 1.0f;
    cur_gain_db = -6.0f;
    can_mute = true;
    cur_mute = false;
    can_agc = false;
    cur_agc = false;
    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);

    output_->Add();

    // After Add, we'll set gain to -7.0 dB and enable Mute.
    cur_gain_db = -7.0f;
    cur_mute = true;
    set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
  }
  gain_flags = GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc);

  // Receive the OnDeviceAdded callback
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  // SetDeviceGain to the new values
  fuchsia::media::AudioGainInfo gain_info = {.gain_db = cur_gain_db, .flags = gain_flags};
  audio_dev_enum_->SetDeviceGain(added_token, gain_info, set_flags);

  // Receive these changed values through GetDeviceGain
  RetrieveGainInfoUsingGetDeviceGain(added_token);
  EXPECT_EQ(received_gain_info_.gain_db, cur_gain_db);
  EXPECT_EQ(received_gain_info_.flags, gain_flags);
}

// From GetDevices, does gain/mute/agc match what was set?
void VirtualAudioDeviceTest::TestGetDevicesAfterSetDeviceGain(bool is_input) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  float min_gain_db, max_gain_db, gain_step_db, cur_gain_db;
  bool can_mute, cur_mute, can_agc, cur_agc;
  uint32_t gain_flags, set_flags;

  if (is_input) {
    input_->SetUniqueId(unique_id);

    min_gain_db = -24.0f;
    max_gain_db = 0.0f;
    gain_step_db = 0.5f;
    cur_gain_db = -13.5f;
    can_mute = true;
    cur_mute = true;
    can_agc = true;
    cur_agc = false;
    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);

    input_->Add();

    // After Add, we'll set gain to -23.5 dB and enable AGC and disable Mute.
    cur_gain_db = -23.5f;
    cur_mute = false;
    cur_agc = true;
    set_flags = fuchsia::media::SetAudioGainFlag_GainValid |
                fuchsia::media::SetAudioGainFlag_MuteValid |
                fuchsia::media::SetAudioGainFlag_AgcValid;
  } else {
    output_->SetUniqueId(unique_id);

    min_gain_db = -22.0f;
    max_gain_db = 1.0f;
    gain_step_db = 1.0f;
    cur_gain_db = -6.0f;
    can_mute = true;
    cur_mute = true;
    can_agc = false;
    cur_agc = false;
    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);

    output_->Add();

    // After Add, we'll set gain to -17.0 dB and disable Mute.
    cur_gain_db = -17.0f;
    cur_mute = false;
    set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
  }
  gain_flags = GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc);

  // Receive the OnDeviceAdded callback
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  // SetDeviceGain to the new values
  fuchsia::media::AudioGainInfo gain_info = {.gain_db = cur_gain_db, .flags = gain_flags};
  audio_dev_enum_->SetDeviceGain(added_token, gain_info, set_flags);

  // Receive these changed values through GetDevices
  RetrieveGainInfoUsingGetDevices(added_token);
  EXPECT_EQ(received_gain_info_.gain_db, cur_gain_db);
  EXPECT_EQ(received_gain_info_.flags, gain_flags);
}

// Using virtual device, validate event is appropriately received/accurate.
// TODO(mpuryear): set (or reset) AGC when it isn't supported. Callback?
// ...also, do other requested changes succeed?
// gain_info (gain, flags) matches what we set? (all our changes, no more)
// Callback if no change?
// Callback if 1 invalid set_flag?
// Callback if partial success (1 valid and 1 invalid set flag, or NAN)?
// Only one callback even if multiple set_flags?

// Using virtual device, validate event is appropriately received and
// accurate. Info matches the virtual device we added? (name, id, token,
// input, gain, flags) is_default TRUE? (and does plug status matter at all?)
// Can Add only partially succeed -- if so, is callback received?
void VirtualAudioDeviceTest::TestOnDeviceAddedAfterAdd(bool is_input, bool is_plugged) {
  SetOnDeviceAddedEvent();

  std::string mfr = "Royal Testing";
  std::string product = "Frobazz";
  std::string expected_name = mfr + " " + product;

  float min_gain_db = -42.0f, max_gain_db = 2.5f, gain_step_db = 0.5f;
  float cur_gain_db = -13.5f;
  bool can_mute = true, cur_mute = true, can_agc = true, cur_agc = true;
  uint32_t expect_flags = GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc);

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  auto now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->SetManufacturer(mfr);
    input_->SetProduct(product);
    input_->SetUniqueId(unique_id);

    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);
    input_->SetPlugProperties(now, is_plugged, false, true);

    input_->Add();
  } else {
    output_->SetManufacturer(mfr);
    output_->SetProduct(product);
    output_->SetUniqueId(unique_id);

    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);
    output_->SetPlugProperties(now, is_plugged, false, true);

    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  // Compare every piece of AudioDeviceInfo retrieved.
  EXPECT_EQ(received_device_.name, mfr + " " + product);
  EXPECT_NE(received_device_.token_id, kInvalidDeviceToken);
  EXPECT_EQ(received_device_.is_input, is_input);

  if (is_input) {
    EXPECT_EQ(received_device_.gain_info.gain_db, cur_gain_db);
    EXPECT_EQ(received_device_.gain_info.flags, expect_flags);
  }
  if (!is_plugged) {
    EXPECT_EQ(received_device_.is_default, false);
  }
}

void VirtualAudioDeviceTest::TestOnDeviceAddedAfterPlug(bool is_input) {
  SetOnDeviceAddedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  auto now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->SetPlugProperties(now - ZX_NSEC(1), false, false, true);

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->SetPlugProperties(now - ZX_NSEC(1), false, false, true);

    output_->Add();
  }
  ExpectDeviceAdded(unique_id);

  if (is_input) {
    input_->ChangePlugState(now, true);
  } else {
    output_->ChangePlugState(now, true);
  }

  RunLoopUntilIdle();
}

void VirtualAudioDeviceTest::TestOnDeviceRemovedAfterRemove(bool is_input, bool is_plugged) {
  SetOnDeviceAddedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  auto now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->SetUniqueId(unique_id);
    if (!is_plugged) {
      input_->SetPlugProperties(now, false, false, true);
    }

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    if (!is_plugged) {
      output_->SetPlugProperties(now, false, false, true);
    }

    output_->Add();
  }

  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;
  ASSERT_NE(added_token, ZX_KOID_INVALID);

  SetOnDeviceRemovedEvent();

  if (is_input) {
    input_->Remove();
  } else {
    output_->Remove();
  }

  ExpectDeviceRemoved(added_token);
}

void VirtualAudioDeviceTest::TestOnDeviceRemovedAfterUnplug(bool is_input) {
  SetOnDeviceAddedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  auto now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->SetUniqueId(unique_id);
    input_->SetPlugProperties(now, true, false, true);

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);
    output_->SetPlugProperties(now, true, false, true);

    output_->Add();
  }

  ExpectDeviceAdded(unique_id);

  SetOnDeviceRemovedEvent();

  now = zx::clock::get_monotonic().get();
  if (is_input) {
    input_->ChangePlugState(now, false);
  } else {
    output_->ChangePlugState(now, false);
  }

  RunLoopUntilIdle();
}

// Using virtual device, validate event is appropriately received and
// accurate. Previous default matches what we did get from GetDevices Previous
// default matches what we did get from GetDefault New default matches what we
// now get from GetDevices New default matches what we now get from GetDefault
// Conditions: first Add, last Remove, subsequent Add, important Remove,
// unimportant Remove, Add(unplugged), plug change
void VirtualAudioDeviceTest::TestOnDefaultDeviceChangedAfterAdd(bool is_input) {
  SetOnDeviceAddedEvent();
  SetOnDefaultDeviceChangedEvent();

  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  if (is_input) {
    input_->SetUniqueId(unique_id);

    input_->Add();
  } else {
    output_->SetUniqueId(unique_id);

    output_->Add();
  }

  ExpectDeviceAdded(unique_id);
  auto added_token = received_device_.token_id;

  ExpectDefaultChanged(added_token);
  EXPECT_EQ(received_default_token_, added_token);
  EXPECT_EQ((is_input ? AudioDeviceTest::initial_input_default_
                      : AudioDeviceTest::initial_output_default_),
            received_old_token_);
}

// Test the OnDefaultDeviceChanged event, after a device is Plugged. We do this
// using two virtual devices -- after adding the first device (with a certain
// plugged-time), we Plug the second one and see how things change.
//
// The most_recent flag indicates whether the device to be plugged will report a
// plugged-time that makes it most-recently-plugged (and thus should become the
// new default). If most_recent is false, then we make the plugged-time for this
// second device _immediately_ before the plugged-time for the first device.
void VirtualAudioDeviceTest::TestOnDefaultDeviceChangedAfterPlug(bool is_input, bool most_recent) {
  AddTwoDevices(is_input, false);
  uint64_t token1 = received_old_token_;
  uint64_t token2 = received_default_token_;

  RetrieveTokenUsingGetDefault(is_input);
  uint64_t default_token = received_default_token_;

  auto now = zx::clock::get_monotonic().get();
  SetOnDefaultDeviceChangedEvent();

  // We'll say that this first device was plugged just 1 ns ago...
  if (is_input) {
    input_->ChangePlugState(now - ZX_NSEC(1), true);
  } else {
    output_->ChangePlugState(now - ZX_NSEC(1), true);
  }
  if (default_token != token1) {
    ExpectDefaultChanged(token1);
  }

  // If this second device is to be Most-Recently-Plugged, make its plugged-time
  // 1 ns after the first -- otherwise make it 1 ns BEFORE the first.
  zx_time_t plug_time = (most_recent ? now : (now - ZX_NSEC(2)));
  if (is_input) {
    input_2_->ChangePlugState(plug_time, true);
  } else {
    output_2_->ChangePlugState(plug_time, true);
  }

  if (most_recent) {
    ExpectDefaultChanged(token2);
    EXPECT_EQ(received_old_token_, token1);
  } else {
    RunLoopUntilIdle();
  }
}

void VirtualAudioDeviceTest::TestOnDefaultDeviceChangedAfterRemove(bool is_input,
                                                                   bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t expect_remove_token = (most_recent ? received_default_token_ : received_old_token_);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  SetOnDefaultDeviceChangedEvent();
  SetOnDeviceRemovedEvent();
  if (most_recent) {
    if (is_input) {
      input_2_->Remove();
    } else {
      output_2_->Remove();
    }

    ExpectDefaultChanged(expect_default_token);
    EXPECT_EQ(received_old_token_, expect_remove_token);
  } else {
    if (is_input) {
      input_->Remove();
    } else {
      output_->Remove();
    }
  }
  ExpectDeviceRemoved(expect_remove_token);

  RunLoopUntilIdle();
}

void VirtualAudioDeviceTest::TestOnDefaultDeviceChangedAfterUnplug(bool is_input,
                                                                   bool most_recent) {
  AddTwoDevices(is_input);
  uint64_t to_unplug_token = (most_recent ? received_default_token_ : received_old_token_);
  uint64_t expect_default_token = (most_recent ? received_old_token_ : received_default_token_);

  auto now = zx::clock::get_monotonic().get();
  SetOnDefaultDeviceChangedEvent();
  if (most_recent) {
    if (is_input) {
      input_2_->ChangePlugState(now, false);
    } else {
      output_2_->ChangePlugState(now, false);
    }

    ExpectDefaultChanged(expect_default_token);
    EXPECT_EQ(received_old_token_, to_unplug_token);
  } else {
    if (is_input) {
      input_->ChangePlugState(now, false);
    } else {
      output_->ChangePlugState(now, false);
    }

    RunLoopUntilIdle();
  }
}

// From GetDevices, does gain/mute/agc match what was set?
void VirtualAudioDeviceTest::TestOnDeviceGainChanged(bool is_input) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(is_input, unique_id.data());

  float min_gain_db, max_gain_db, gain_step_db, cur_gain_db;
  bool can_mute, cur_mute, can_agc, cur_agc;
  uint32_t gain_flags, set_flags;

  if (is_input) {
    input_->SetUniqueId(unique_id);

    min_gain_db = -24.0f;
    max_gain_db = 0.0f;
    gain_step_db = 0.5f;
    cur_gain_db = -13.5f;
    can_mute = true;
    cur_mute = true;
    can_agc = true;
    cur_agc = false;
    input_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                              cur_mute, can_agc, cur_agc);

    input_->Add();

    // After Add, we'll set gain to -23.5 dB and enable AGC and disable Mute.
    cur_gain_db = -23.5f;
    cur_mute = false;
    cur_agc = true;
    set_flags = fuchsia::media::SetAudioGainFlag_GainValid |
                fuchsia::media::SetAudioGainFlag_MuteValid |
                fuchsia::media::SetAudioGainFlag_AgcValid;
  } else {
    output_->SetUniqueId(unique_id);

    min_gain_db = -22.0f;
    max_gain_db = 1.0f;
    gain_step_db = 1.0f;
    cur_gain_db = -6.0f;
    can_mute = true;
    cur_mute = true;
    can_agc = false;
    cur_agc = false;
    output_->SetGainProperties(min_gain_db, max_gain_db, gain_step_db, cur_gain_db, can_mute,
                               cur_mute, can_agc, cur_agc);

    output_->Add();

    // After Add, we'll set gain to -17.0 dB and disable Mute.
    cur_gain_db = -17.0f;
    cur_mute = false;
    set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
  }
  gain_flags = GainFlagsFromBools(can_mute, cur_mute, can_agc, cur_agc);

  // Receive the OnDeviceAdded callback
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  // SetDeviceGain to the new values
  fuchsia::media::AudioGainInfo gain_info = {.gain_db = cur_gain_db, .flags = gain_flags};
  SetOnDeviceGainChangedEvent();
  audio_dev_enum_->SetDeviceGain(added_token, gain_info, set_flags);

  // Receive the OnDeviceGainChanged callback
  ExpectGainChanged(added_token);

  EXPECT_EQ(received_gain_info_.gain_db, cur_gain_db);
  EXPECT_EQ(received_gain_info_.flags, gain_flags);
}

//
// VirtualAudioDeviceTest -- test cases that use the virtualaudio mechanism
//
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesAddInput) { TestGetDevicesAfterAdd(true); }

// Remove input (default changed) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesRemoveDefaultInput) {
  TestGetDevicesAfterRemove(true, true);
}

// Remove input (default didn't change) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesRemoveNotDefaultInput) {
  TestGetDevicesAfterRemove(true, false);
}

// Unplug input (default changed) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesUnplugDefaultInput) {
  TestGetDevicesAfterUnplug(true, true);
}

// Unplug input (default didn't change) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesUnplugNotDefaultInput) {
  TestGetDevicesAfterUnplug(true, false);
}

// After SetDeviceGain, GetDevices should reflect the gain change
// Do basic validation that we don't change more than set_flags specifies.
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesSetDeviceGainInput) {
  TestGetDevicesAfterSetDeviceGain(true);
}

// Using virtualaudio, validate that device list matches what was added.
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesAddOutput) { TestGetDevicesAfterAdd(false); }

// Remove output (default changed) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesRemoveDefaultOutput) {
  TestGetDevicesAfterRemove(false, true);
}

// Remove output (default didn't change) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesRemoveNotDefaultOutput) {
  TestGetDevicesAfterRemove(false, false);
}

// Unplug output (default changed) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesUnplugDefaultOutput) {
  TestGetDevicesAfterUnplug(false, true);
}

// Unplug output (default didn't change) then GetDevices
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesUnplugNotDefaultOutput) {
  TestGetDevicesAfterUnplug(false, false);
}

// After SetDeviceGain, GetDevices should reflect the gain change
// Do basic validation that we don't change more than set_flags specifies.
TEST_F(VirtualAudioDeviceTest, GetDevicesMatchesSetDeviceGainOutput) {
  TestGetDevicesAfterSetDeviceGain(false);
}

TEST_F(VirtualAudioDeviceTest, GetDefaultInputDeviceMatchesUnpluggedAdd) {
  TestGetDefaultDeviceAfterUnpluggedAdd(true);
}

// Remove (default changed) then GetDefaultInputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultInputDeviceMatchesRemoveDefault) {
  TestGetDefaultDeviceAfterRemove(true, true);
}

// Remove (default didn't change) then GetDefaultInputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultInputDeviceMatchesRemoveNotDefault) {
  TestGetDefaultDeviceAfterRemove(true, false);
}

// Unplug (default changed) then GetDefaultInputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultInputDeviceMatchesUnplugDefault) {
  TestGetDefaultDeviceAfterUnplug(true, true);
}

// Unplug (default didn't change) then GetDefaultInputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultInputDeviceMatchesUnplugNotDefault) {
  TestGetDefaultDeviceAfterUnplug(true, false);
}

TEST_F(VirtualAudioDeviceTest, GetDefaultOutputDeviceMatchesUnpluggedAdd) {
  TestGetDefaultDeviceAfterUnpluggedAdd(false);
}

// Remove (default changed) then GetDefaultOutputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultOutputDeviceMatchesRemoveDefault) {
  TestGetDefaultDeviceAfterRemove(false, true);
}

// Remove (default didn't change) then GetDefaultOutputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultOutputDeviceMatchesRemoveNotDefault) {
  TestGetDefaultDeviceAfterRemove(false, false);
}

// Unplug (default changed) then GetDefaultOutputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultOutputDeviceMatchesUnplugDefault) {
  TestGetDefaultDeviceAfterUnplug(false, true);
}

// Unplug (default didn't change) then GetDefaultOutputDevice
TEST_F(VirtualAudioDeviceTest, GetDefaultOutputDeviceMatchesUnplugNotDefault) {
  TestGetDefaultDeviceAfterUnplug(false, false);
}

// After SetDeviceGain, GetDeviceGain should reflect the gain change
TEST_F(VirtualAudioDeviceTest, GetDeviceGainMatchesInputSetDeviceGain) {
  TestGetDeviceGainAfterSetDeviceGain(true);
}

// After SetDeviceGain, GetDeviceGain should reflect the gain change
TEST_F(VirtualAudioDeviceTest, GetDeviceGainMatchesOutputSetDeviceGain) {
  TestGetDeviceGainAfterSetDeviceGain(false);
}

TEST_F(VirtualAudioDeviceTest, GetDeviceGainOfRemovedOutput) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(false, unique_id.data());
  output_->SetUniqueId(unique_id);

  output_->Add();
  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  SetOnDeviceRemovedEvent();
  output_->Remove();

  ExpectDeviceRemoved(added_token);

  RetrieveGainInfoUsingGetDeviceGain(received_removed_token_, false);
}

// Given invalid params to SetDeviceGain, FIDL interface should not
// disconnect. These 4 params include device token_id, gain_db, gain flags,
// and set flags.
TEST_F(VirtualAudioDeviceTest, SetDeviceGainOfBadValues) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};

  PopulateUniqueIdArr(true, unique_id.data());
  input_->SetUniqueId(unique_id);
  input_->Add();
  ExpectDeviceAdded(unique_id);

  auto added_in_token = received_device_.token_id;

  PopulateUniqueIdArr(false, unique_id.data());
  output_->SetUniqueId(unique_id);
  output_->Add();
  ExpectDeviceAdded(unique_id);

  auto added_out_token = received_device_.token_id;

  // The explicitly-invalid token_id
  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID, {.gain_db = 0.0f, .flags = kGainFlagMask},
                                 kSetFlagMask);

  // A device token_id that does not correctly refer to a device
  audio_dev_enum_->SetDeviceGain(kInvalidDeviceToken, {.gain_db = 0.0f, .flags = kGainFlagMask},
                                 kSetFlagMask);

  // An invalid gain_db value
  audio_dev_enum_->SetDeviceGain(added_in_token, {.gain_db = NAN, .flags = kGainFlagMask},
                                 kSetFlagMask);
  audio_dev_enum_->SetDeviceGain(added_out_token, {.gain_db = NAN, .flags = kGainFlagMask},
                                 kSetFlagMask);

  // Invalid gain flags (set bits outside the defined ones)
  audio_dev_enum_->SetDeviceGain(added_in_token, {.gain_db = 0.0f, .flags = ~kGainFlagMask},
                                 kSetFlagMask);
  audio_dev_enum_->SetDeviceGain(added_out_token, {.gain_db = 0.0f, .flags = ~kGainFlagMask},
                                 kSetFlagMask);

  // Invalid set flags (set bits outside the defined ones)
  audio_dev_enum_->SetDeviceGain(added_in_token, {.gain_db = 0.0f, .flags = kGainFlagMask},
                                 ~kSetFlagMask);
  audio_dev_enum_->SetDeviceGain(added_out_token, {.gain_db = 0.0f, .flags = kGainFlagMask},
                                 ~kSetFlagMask);

  // We should not disconnect.
  RunLoopUntilIdle();
}

// SetDeviceGain of previously-valid, removed dev should silently do nothing.
TEST_F(VirtualAudioDeviceTest, SetDeviceGainOfRemovedInput) {
  SetOnDeviceAddedEvent();
  std::array<uint8_t, 16> unique_id{0};
  PopulateUniqueIdArr(true, unique_id.data());
  input_->SetUniqueId(unique_id);

  input_->Add();

  ExpectDeviceAdded(unique_id);

  auto added_token = received_device_.token_id;

  SetOnDeviceRemovedEvent();
  input_->Remove();

  ExpectDeviceRemoved(added_token);

  uint64_t removed_token = received_removed_token_;

  SetOnDeviceGainChangedEvent();
  audio_dev_enum_->SetDeviceGain(removed_token, {.gain_db = 0.0f, .flags = 0}, kSetFlagMask);

  // We should receive neither callback nor disconnect.
  RunLoopUntilIdle();
}

TEST_F(VirtualAudioDeviceTest, OnDeviceAddedNotTriggeredByPlugInput) {
  TestOnDeviceAddedAfterPlug(true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceAddedMatchesAddPluggedOutput) {
  // Add a plugged-in device
  TestOnDeviceAddedAfterAdd(false, true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceAddedMatchesAddUnpluggedOutput) {
  // Add an unplugged device
  TestOnDeviceAddedAfterAdd(false, false);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceAddedNotTriggeredByPlugOutput) {
  TestOnDeviceAddedAfterPlug(false);
}

// Using virtual device, validate event is appropriately received and
// accurate. Token matches the virtual device we removed? Can Remove only
// partially succeed -- if so, is callback received? What if previous Add had
// only partially succeeded?
TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedMatchesRemovePluggedInput) {
  // Remove a plugged input device
  TestOnDeviceRemovedAfterRemove(true, true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedMatchesRemoveUnpluggedInput) {
  // Remove an unplugged input device
  TestOnDeviceRemovedAfterRemove(true, false);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedNotTriggeredByUnplugInput) {
  TestOnDeviceRemovedAfterUnplug(true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedMatchesRemovePluggedOutput) {
  // Remove a plugged output device
  TestOnDeviceRemovedAfterRemove(false, true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedMatchesRemoveUnpluggedOutput) {
  // Remove an unplugged output device
  TestOnDeviceRemovedAfterRemove(false, false);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceRemovedNotTriggeredByUnplugOutput) {
  TestOnDeviceRemovedAfterUnplug(false);
}

// Plug an input at most-recent-timestamp
//
// TODO(mpuryear): When we honor the plug-change timestamp (instead of merely
// treating all plug changes as NOW), test the not-most-recent scenario.
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesPlugDefaultInput) {
  TestOnDefaultDeviceChangedAfterPlug(true, true);
}

// Remove (default changed) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesRemoveDefaultInput) {
  TestOnDefaultDeviceChangedAfterRemove(true, true);
}

// Remove (default didn't change) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesRemoveNotDefaultInput) {
  TestOnDefaultDeviceChangedAfterRemove(true, false);
}

// Unplug (default changed) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesUnplugDefaultInput) {
  TestOnDefaultDeviceChangedAfterUnplug(true, true);
}

// Unplug (default didn't change) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesUnplugNotDefaultInput) {
  TestOnDefaultDeviceChangedAfterUnplug(true, false);
}

// Plug an output at most-recent-timestamp
//
// TODO(mpuryear): When we honor the plug-change timestamp (instead of merely
// treating all plug changes as NOW), test the not-most-recent scenario.
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesPlugDefaultOutput) {
  TestOnDefaultDeviceChangedAfterPlug(false, true);
}

// Remove (default changed) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesRemoveDefaultOutput) {
  TestOnDefaultDeviceChangedAfterRemove(false, true);
}

// Remove (default didn't change) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesRemoveNotDefaultOutput) {
  TestOnDefaultDeviceChangedAfterRemove(false, false);
}

// Unplug (default changed) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesUnplugDefaultOutput) {
  TestOnDefaultDeviceChangedAfterUnplug(false, true);
}

// Unplug (default didn't change) -> OnDefaultDeviceChanged
TEST_F(VirtualAudioDeviceTest, OnDefaultDeviceChangedMatchesUnplugNotDefaultOutput) {
  TestOnDefaultDeviceChangedAfterUnplug(false, false);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceGainChangedMatchesSetDeviceGainInput) {
  TestOnDeviceGainChanged(true);
}

TEST_F(VirtualAudioDeviceTest, OnDeviceGainChangedMatchesSetDeviceGainOutput) {
  TestOnDeviceGainChanged(false);
}

}  // namespace media::audio::test
