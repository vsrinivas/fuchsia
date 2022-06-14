// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/acpi-arm64/acpi-arm64.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <zircon/status.h>

#include "src/devices/board/drivers/acpi-arm64/acpi-arm64-bind.h"
#include "src/devices/board/lib/acpi/acpi-impl.h"
#include "src/devices/board/lib/acpi/pci.h"
#include "src/devices/board/lib/smbios/smbios.h"

// Used by the ACPICA OSL in //zircon/system/ulib/acpica.
zx_handle_t root_resource_handle;

#ifndef ENABLE_USER_PCI
// This is a hack that's only used until ARM switches to userspace PCI.
zx_status_t pci_init(zx_device_t* platform_bus, ACPI_HANDLE object,
                     acpi::UniquePtr<ACPI_DEVICE_INFO> info, acpi::Manager* manager,
                     std::vector<pci_bdf_t> acpi_bdfs) {
  zxlogf(ERROR,
         "Userspace PCI for ACPI on ARM64 is required. Please set platform_enable_user_pci = true "
         "in args.gn");
  return ZX_ERR_NOT_SUPPORTED;
}
#endif

namespace acpi_arm64 {
namespace {
template <size_t N>
static void SetField(const char* label, const std::string& value, char (&out)[N]) {
  if (value.empty()) {
    zxlogf(WARNING, "acpi: smbios %s could not be read", label);
  } else if (value.size() >= N) {
    zxlogf(INFO, "acpi: smbios %s too big for sysinfo: %s", label, value.data());
  } else {
    strlcpy(out, value.data(), N);
  }
}

}  // namespace

zx_status_t AcpiArm64::Create(void* ctx, zx_device_t* parent) {
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
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status = iommu_manager_.Init(zx::unowned_resource(get_root_resource()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init iommu manager: %s", zx_status_get_string(status));
    txn.Reply(status);
    return;
  }
  manager_.emplace(&acpi_, &iommu_manager_, zxdev_);

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  root_resource_handle = get_root_resource();

  auto dispatcher = fdf::Dispatcher::GetCurrent();
  async::PostTask(dispatcher->async_dispatcher(), [txn = std::move(txn), this]() mutable {
    zx::status<> zx_status = SysmemInit();
    if (zx_status.is_error()) {
      zxlogf(ERROR, "Sysmem init failed: %s", zx_status.status_string());
      txn.Reply(zx_status.status_value());
      return;
    }

    zx_status = SmbiosInit();
    if (zx_status.is_error()) {
      zxlogf(ERROR, "SMBIOS init failed: %s", zx_status.status_string());
      txn.Reply(zx_status.status_value());
      return;
    }

    acpi::status<> status = manager_->acpi()->InitializeAcpi();
    if (status.is_error()) {
      txn.Reply(status.zx_status_value());
    } else {
      txn.Reply(ZX_OK);
    }

    status = manager_->DiscoverDevices();
    if (status.is_error()) {
      zxlogf(ERROR, "discover devices failed: %d", status.error_value());
    }
    status = manager_->ConfigureDiscoveredDevices();
    if (status.is_error()) {
      zxlogf(ERROR, "configure failed: %d", status.error_value());
    }
    status = manager_->PublishDevices(parent_);
    if (status.is_error()) {
      zxlogf(ERROR, "publish devices failed: %d", status.error_value());
    }
  });
}

zx::status<> AcpiArm64::SmbiosInit() {
  pbus_board_info_t board_info{.board_name = "arm64", .board_revision = 0};
  pbus_bootloader_info_t bootloader_info{.vendor = "<unknown>"};

  // Load SMBIOS information.
  smbios::SmbiosInfo smbios;
  zx_status_t status = smbios.Load();
  if (status == ZX_OK) {
    SetField("board name", smbios.board_name(), board_info.board_name);
    SetField("vendor", smbios.vendor(), bootloader_info.vendor);
  } else {
    zxlogf(ERROR, "Failed to load smbios: %s", zx_status_get_string(status));
  }

  // Inform the platform bus of our board info.
  status = pbus_.SetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBoardInfo failed: %d", status);
  }

  // Inform the platform bus of our bootloader info.
  status = pbus_.SetBootloaderInfo(&bootloader_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBootloaderInfo failed: %d", status);
  }

  return zx::ok();
}

static constexpr zx_driver_ops_t driver_ops{
    .version = DRIVER_OPS_VERSION,
    .bind = AcpiArm64::Create,
};

}  // namespace acpi_arm64

ZIRCON_DRIVER(acpi_arm64, acpi_arm64::driver_ops, "zircon", "0.1");
