// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <limits.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/resource.h>
#include <zircon/threads.h>

#include <variant>
#include <vector>

#include <acpica/acpi.h>
#include <fbl/auto_lock.h>

#include "acpi-dev/dev-ec.h"
#include "acpi-private.h"
#include "acpi.h"
#include "dev.h"
#include "errors.h"
#include "methods.h"
#include "power.h"
#include "src/devices/board/lib/acpi/device.h"
#include "src/devices/board/lib/acpi/manager.h"
#include "src/devices/board/lib/acpi/resources.h"
#include "src/devices/board/lib/acpi/status.h"
#include "src/devices/lib/iommu/iommu.h"
#include "sysmem.h"

namespace {

const std::string_view hid_from_acpi_devinfo(const ACPI_DEVICE_INFO& info) {
  if ((info.Valid & ACPI_VALID_HID) && (info.HardwareId.Length > 0) &&
      ((info.HardwareId.Length - 1) <= sizeof(uint64_t))) {
    // ACPICA string lengths include the NULL terminator.
    return std::string_view{info.HardwareId.String, info.HardwareId.Length - 1};
  }

  return std::string_view{};
}

}  // namespace

namespace acpi {

acpi::status<UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj) {
  ACPI_DEVICE_INFO* raw = nullptr;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(obj, &raw);
  UniquePtr<ACPI_DEVICE_INFO> ret{raw};

  if (ACPI_SUCCESS(acpi_status)) {
    return acpi::ok(std::move(ret));
  }

  return acpi::error(acpi_status);
}

}  // namespace acpi

zx_status_t acpi_suspend(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason,
                         uint8_t* out_state) {
  switch (suspend_reason & DEVICE_MASK_SUSPEND_REASON) {
    case DEVICE_SUSPEND_REASON_MEXEC: {
      AcpiTerminate();
      return ZX_OK;
    }
    case DEVICE_SUSPEND_REASON_REBOOT:
      // Don't do anything, we expect the higher layers to execute the reboot.
      return ZX_OK;
    case DEVICE_SUSPEND_REASON_POWEROFF:
      poweroff();
      exit(0);
    case DEVICE_SUSPEND_REASON_SUSPEND_RAM:
      return suspend_to_ram();
    default:
      return ZX_ERR_NOT_SUPPORTED;
  };
}

zx_status_t publish_acpi_devices(acpi::Manager* manager, zx_device_t* platform_bus,
                                 zx_device_t* acpi_root) {
  zx_status_t status = pwrbtn_init(acpi_root);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to initialize pwrbtn device: %d", status);
  }

  acpi::Acpi* acpi = manager->acpi();

  auto result = manager->DiscoverDevices();
  if (result.is_error()) {
    zxlogf(INFO, "discover devices failed");
  }
  result = manager->ConfigureDiscoveredDevices();
  if (result.is_error()) {
    zxlogf(INFO, "configure failed");
  }
  result = manager->PublishDevices(platform_bus);

  // Now walk the ACPI namespace looking for devices we understand, and publish
  // them.  For now, publish only the first PCI bus we encounter.
  // TODO(fxbug.dev/78349): remove this when all drivers are removed from the x86 board driver.
  acpi::status<> acpi_status =
      acpi->WalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
                          [acpi_root, acpi](ACPI_HANDLE object, uint32_t level,
                                            acpi::WalkDirection dir) -> acpi::status<> {
                            // We don't have anything useful to do during the ascent
                            // phase.  Just skip it.
                            if (dir == acpi::WalkDirection::Ascending) {
                              return acpi::ok();
                            }

                            // We are descending.  Grab our object info.
                            acpi::UniquePtr<ACPI_DEVICE_INFO> info;
                            if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
                              return res.take_error();
                            } else {
                              info = std::move(res.value());
                            }

                            // Extract pointers to the hardware ID and the compatible ID
                            // if present. If there is no hardware ID, just skip the
                            // device.
                            const std::string_view hid = hid_from_acpi_devinfo(*info);
                            if (hid.empty()) {
                              return acpi::ok();
                            }

                            // Now, if we recognize the HID, go ahead and deal with
                            // publishing the device.
                            if (hid == EC_HID_STRING) {
                              acpi_ec::EcDevice::Create(acpi_root, acpi, object);
                            }
                            return acpi::ok();
                          });

  if (acpi_status.is_error()) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}
