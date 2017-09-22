// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_DEVICE_INFO_DEVICE_PROFILE_H_
#define APPS_MODULAR_LIB_DEVICE_INFO_DEVICE_PROFILE_H_

#include "peridot/lib/device_info/device_info.h"

#include "lib/fxl/macros.h"

#include <string>

namespace modular {

// Parses a device's profile JSON. Can be used to parse the current device or a
// remote device's profile from the device map.
class DeviceProfile {
  DeviceProfile();
  ~DeviceProfile() = delete;

  bool Parse(const std::string& jsonProfile);
  bool ParseDefaultProfile();

  // if this device is intended to be a remote presentation device.
  // HACK(zbowling): TBD: We need a better way deciding a device's idioms.
  bool presentation_server{false};

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceProfile);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_DEVICE_INFO_DEVICE_PROFILE_H_
