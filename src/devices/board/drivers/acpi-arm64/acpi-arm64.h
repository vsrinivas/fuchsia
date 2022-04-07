// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>

#include "src/devices/board/lib/acpi/acpi-impl.h"
#include "src/devices/board/lib/acpi/manager.h"

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
  std::optional<acpi::Manager> manager_;
  acpi::AcpiImpl acpi_;

  std::thread init_thread_;
};

}  // namespace acpi_arm64
