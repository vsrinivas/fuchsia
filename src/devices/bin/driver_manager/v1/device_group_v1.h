// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_

#include "src/devices/bin/driver_manager/composite_device.h"
#include "src/devices/bin/driver_manager/coordinator.h"

// Wrapper struct for a fbl::RefPtr<Device>. This allows the device_group code
// to refer to this without any dependencies on the DFv1 code.
// TODO(fxb/106479): Move this struct and the rest of the device group code
// under the namespace.
struct DeviceV1Wrapper {
  const fbl::RefPtr<Device> device;
};

namespace device_group {

// DFv1 implementation for DeviceGroup. DeviceGroupV1 creates and manages a
// CompositeDevice object underneath the interface.
class DeviceGroupV1 : public DeviceGroup {
 public:
  static zx::status<std::unique_ptr<DeviceGroupV1>> Create(
      DeviceGroupCreateInfo create_info, fuchsia_driver_index::MatchedCompositeInfo driver,
      Coordinator* coordinator);

  // Must only be called by Create() to ensure the objects are verified.
  DeviceGroupV1(DeviceGroupCreateInfo create_info,
                std::unique_ptr<CompositeDevice> composite_device);

 protected:
  zx::status<std::optional<DeviceOrNode>> BindNodeImpl(uint32_t node_index,
                                                       const DeviceOrNode& device_or_node) override;

 private:
  std::unique_ptr<CompositeDevice> composite_device_;
};

}  // namespace device_group

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_
