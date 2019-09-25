// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/testing/fake_object_registry.h"

namespace media::audio::testing {

class FakeAudioDevice : public AudioDevice {
 public:
  FakeAudioDevice(AudioDevice::Type type, ThreadingModel* threading_model, ObjectRegistry* registry)
      : AudioDevice(type, threading_model, registry) {}

  // |media::audio::AudioDevice|
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override {}
  void OnWakeup() override {}
};

class FakeAudioInput : public FakeAudioDevice {
 public:
  FakeAudioInput(ThreadingModel* threading_model, ObjectRegistry* registry)
      : FakeAudioDevice(Type::Input, threading_model, registry) {}
};

class FakeAudioOutput : public FakeAudioDevice {
 public:
  FakeAudioOutput(ThreadingModel* threading_model, ObjectRegistry* registry)
      : FakeAudioDevice(Type::Output, threading_model, registry) {}
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DEVICE_H_
