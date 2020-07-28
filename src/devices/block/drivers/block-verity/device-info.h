// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_INFO_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_INFO_H_

#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/macros.h>

#include "geometry.h"

namespace block_verity {

// |block_verity::DeviceInfo| bundles block device configuration details passed from the controller
// to the device.
struct DeviceInfo {
  DeviceInfo(zx_device_t* device, Geometry geometry_in, uint64_t upstream_op_size_in,
             uint64_t op_size_in);
  DeviceInfo(DeviceInfo&& other);

  // Disallow copy and assign.  Allow move.
  DeviceInfo(const DeviceInfo&) = delete;
  DeviceInfo& operator=(const DeviceInfo&) = delete;

  ~DeviceInfo() = default;

  // Factory function
  static DeviceInfo CreateFromDevice(zx_device_t* device);

  // Callbacks to the parent's block protocol methods.
  ddk::BlockProtocolClient block_protocol;
  // The parent block device
  zx_device_t* block_device;

  // The device's geometry and allocation
  Geometry geometry;

  // The parent device's required block_op_t size.
  uint64_t upstream_op_size;

  // This device's required block_op_t size.
  uint64_t op_size;

  // Returns true if the block device can be used by block_verity.  This may fail, for example, if
  // the constructor was unable to get a valid block protocol.
  bool IsValid() const;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_INFO_H_
