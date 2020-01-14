// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_REGISTRY_H_

#include <lib/zx/time.h>

#include <memory>

namespace media::audio {

class AudioDevice;

class DeviceRegistry {
 public:
  virtual ~DeviceRegistry() = default;

  // Begin initializing a device and add it to the set of devices waiting to be initialized.
  //
  // Called from the plug detector when a new stream device first shows up.
  virtual void AddDevice(const std::shared_ptr<AudioDevice>& device) = 0;

  // Move device from pending-init list to active-devices list. Notify users and re-evaluate policy.
  virtual void ActivateDevice(const std::shared_ptr<AudioDevice>& device) = 0;

  // Shutdown this device; remove it from the appropriate set of active devices.
  virtual void RemoveDevice(const std::shared_ptr<AudioDevice>& device) = 0;

  // Handles a plugged/unplugged state change for the supplied audio device.
  virtual void OnPlugStateChanged(const std::shared_ptr<AudioDevice>& device, bool plugged,
                                  zx::time plug_time) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_REGISTRY_H_
