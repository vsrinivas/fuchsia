// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DEVICE_ENUMERATOR_STUB_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DEVICE_ENUMERATOR_STUB_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>

namespace media::audio::drivers::test {

class AudioDeviceEnumeratorStub : public fuchsia::media::AudioDeviceEnumerator,
                                  public component_testing::LocalComponentImpl {
 public:
  void OnStart() override;

  // fuchsia::media::AudioDeviceEnumerator impl
  void GetDevices(GetDevicesCallback get_devices_callback) final;
  void GetDeviceGain(uint64_t id, GetDeviceGainCallback get_device_gain_callback) final;
  void SetDeviceGain(uint64_t id, fuchsia::media::AudioGainInfo gain_info,
                     fuchsia::media::AudioGainValidFlags flags) final;
  void GetDefaultInputDevice(GetDefaultInputDeviceCallback get_default_input_callback) final;
  void GetDefaultOutputDevice(GetDefaultOutputDeviceCallback get_default_output_callback) final;
  void AddDeviceByChannel(
      std::string device_name, bool is_input,
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel) final;

  fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> TakeChannel();
  bool channel_available() const { return channel_.is_valid(); }

 private:
  // The set of AudioDeviceEnumerator clients we are currently tending to.
  fidl::BindingSet<fuchsia::media::AudioDeviceEnumerator> audio_device_enumerator_bindings_;

  fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel_;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DEVICE_ENUMERATOR_STUB_H_
