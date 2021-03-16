// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEFAULTS_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEFAULTS_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/ddk/device.h>

namespace internal {

extern const zx_protocol_device_t kDeviceDefaultOps;
extern const device_power_state_info_t kDeviceDefaultPowerStates[2];
extern const device_performance_state_info_t kDeviceDefaultPerfStates[1];
extern const std::array<fuchsia_device::wire::SystemPowerStateInfo,
                        fuchsia_hardware_power_statecontrol::wire::MAX_SYSTEM_POWER_STATES>
    kDeviceDefaultStateMapping;

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEFAULTS_H_
