// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_

#include "src/devices/bin/driver_manager/composite_device.h"
#include "src/devices/bin/driver_manager/driver_loader.h"

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
      DeviceGroupCreateInfo create_info,
      fuchsia_device_manager::wire::DeviceGroupDescriptor group_desc, DriverLoader* driver_loader);

  DeviceGroupV1(DeviceGroupCreateInfo create_info, fbl::Array<std::unique_ptr<Metadata>> metadata,
                bool spawn_colocated, DriverLoader* driver_loader);

 private:
  // DeviceGroup interface:
  zx::status<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedDeviceGroupInfo info,
      const DeviceOrNode& device_or_node) override;

  // Should only be called when |composite_device_| is null.
  void SetCompositeDevice(fuchsia_driver_index::wire::MatchedDeviceGroupInfo info);

  // Used to create |composite_device_|. Set to empty once |composite_device_| is created.
  fbl::Array<std::unique_ptr<Metadata>> metadata_;

  // Used to create |composite_device_|. The value is received from a DeviceGroupDescriptor,
  // not the driver index.
  bool spawn_colocated_;

  // Set by SetCompositeDevice() after the first BindNodeImpl() call.
  std::unique_ptr<CompositeDevice> composite_device_;

  // Must outlive DeviceGroupV1.
  DriverLoader* driver_loader_;
};

}  // namespace device_group

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DEVICE_GROUP_V1_H_
