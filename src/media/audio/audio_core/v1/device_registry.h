// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_REGISTRY_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/media/audio/audio_core/shared/audio_policy.h"
#include "src/media/audio/audio_core/shared/device_lister.h"

namespace media::audio {

class AudioDevice;

class DeviceRegistry : public DeviceLister {
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

// An interface by which |DeviceRegistry| reports immediately before, and immediately after, the
// RouteGraph has added/removed a target device.
class DeviceRouter {
 public:
  virtual ~DeviceRouter() = default;

  // To be overridden by child implementations
  virtual void SetIdlePowerOptionsFromPolicy(AudioPolicy::IdlePowerOptions) = 0;

  // A device is ready to be routed -- add it to the route graph as appropriate.
  virtual void AddDeviceToRoutes(AudioDevice* device) = 0;

  // A device can no longer be routed -- remove it from the route graph as appropriate.
  virtual void RemoveDeviceFromRoutes(AudioDevice* device) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_REGISTRY_H_
