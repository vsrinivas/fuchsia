// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_DEVICE_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_DEVICE_REGISTRY_H_

#include "src/media/audio/audio_core/device_registry.h"

namespace media::audio::testing {

class StubDeviceRegistry : public DeviceRegistry {
 public:
  ~StubDeviceRegistry() override = default;

  // |media::audio::DeviceRegistry|
  void AddDevice(const std::shared_ptr<AudioDevice>& device) override {}
  void ActivateDevice(const std::shared_ptr<AudioDevice>& device) override {}
  void RemoveDevice(const std::shared_ptr<AudioDevice>& device) override {}
  void OnPlugStateChanged(const std::shared_ptr<AudioDevice>& device, bool plugged,
                          zx::time plug_time) override {}
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_DEVICE_REGISTRY_H_
