// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/acpi-arm64/acpi-arm64.h"

#include <lib/ddk/debug.h>

#include "lib/ddk/driver.h"
#include "lib/fit/defer.h"
#include "src/devices/board/drivers/acpi-arm64/acpi-arm64-bind.h"
#include "src/devices/board/lib/acpi/acpi-impl.h"
#include "src/devices/board/lib/acpi/pci.h"

zx_handle_t root_resource_handle;

namespace acpi_arm64 {

namespace {}  // namespace

zx_status_t AcpiArm64::Create(void *ctx, zx_device_t *parent) {
  zxlogf(INFO, "Hello world!");
  auto device = std::make_unique<AcpiArm64>(parent);

  zx_status_t status =
      device->DdkAdd(ddk::DeviceAddArgs("acpi").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status == ZX_OK) {
    // The DDK now owns the device.
    __UNUSED auto unused = device.release();
  }
  return status;
}

void AcpiArm64::DdkInit(ddk::InitTxn txn) {
  manager_.emplace(&acpi_, zxdev_);

  // TODO(simonshields): add the spiel here.
  root_resource_handle = get_root_resource();

  init_thread_ = std::thread([txn = std::move(txn), this]() mutable {
    auto status = manager_->acpi()->InitializeAcpi();
    if (status.is_error()) {
      txn.Reply(status.zx_status_value());
    } else {
      txn.Reply(ZX_OK);
    }

    auto result = manager_->DiscoverDevices();
    if (result.is_error()) {
      zxlogf(INFO, "discover devices failed");
    }
    result = manager_->ConfigureDiscoveredDevices();
    if (result.is_error()) {
      zxlogf(INFO, "configure failed");
    }
    result = manager_->PublishDevices(parent_);

    return AE_OK;
  });
}

static constexpr zx_driver_ops_t driver_ops{
    .version = DRIVER_OPS_VERSION,
    .bind = AcpiArm64::Create,
};

}  // namespace acpi_arm64

ZIRCON_DRIVER(acpi_arm64, acpi_arm64::driver_ops, "zircon", "0.1");
