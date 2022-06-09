// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_
#define SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_

#include <ddktl/device.h>

#include "src/devices/board/lib/acpi/acpi-impl.h"
#include "src/devices/board/lib/acpi/manager-fuchsia.h"
#include "src/devices/lib/iommu/iommu-arm.h"

namespace acpi_arm64 {

class AcpiArm64;
using DeviceType = ddk::Device<AcpiArm64, ddk::Initializable>;

class AcpiArm64 : public DeviceType {
 public:
  explicit AcpiArm64(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() { delete this; }

 private:
  std::optional<acpi::FuchsiaManager> manager_;
  acpi::AcpiImpl acpi_;
  iommu::ArmIommuManager iommu_manager_;

  std::thread init_thread_;
};

}  // namespace acpi_arm64

#endif  // SRC_DEVICES_BOARD_DRIVERS_ACPI_ARM64_ACPI_ARM64_H_
