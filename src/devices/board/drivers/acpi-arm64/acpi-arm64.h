// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_
#define SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_

#include <ddktl/device.h>

namespace acpi_arm64 {

class AcpiArm64;
using DeviceType = ddk::Device<AcpiArm64>;

class AcpiArm64 : public DeviceType {
 public:
  explicit AcpiArm64(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }
};

}  // namespace acpi_arm64

#endif  // SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_
