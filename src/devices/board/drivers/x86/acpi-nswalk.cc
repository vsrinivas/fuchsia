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

// TODO(fxbug.dev/81684): remove these hacks once we have a proper solution for managing power of
// ACPI devices.
void acpi_apply_workarounds(acpi::Acpi* acpi, ACPI_HANDLE object, ACPI_DEVICE_INFO* info) {
  // Slate workaround: Turn on the HID controller.
  if (!memcmp(&info->Name, "I2C0", 4)) {
    auto acpi_status = acpi->EvaluateObject(object, "H00A._PR0", std::nullopt);
    if (acpi_status.is_ok()) {
      acpi::UniquePtr<ACPI_OBJECT> pkg = std::move(acpi_status.value());
      for (unsigned i = 0; i < pkg->Package.Count; i++) {
        ACPI_OBJECT* ref = &pkg->Package.Elements[i];
        if (ref->Type != ACPI_TYPE_LOCAL_REFERENCE) {
          zxlogf(DEBUG, "acpi: Ignoring wrong type 0x%x", ref->Type);
        } else {
          zxlogf(DEBUG, "acpi: Enabling HID controller at I2C0.H00A._PR0[%u]", i);
          acpi_status = acpi->EvaluateObject(ref->Reference.Handle, "_ON", std::nullopt);
          if (acpi_status.is_error()) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C0._PR0._ON", acpi_status.error_value());
          }
        }
      }
    }

    // Atlas workaround: Turn on the HID controller.
    acpi_status = acpi->EvaluateObject(object, "H049._PR0", std::nullopt);
    if (acpi_status.is_ok()) {
      acpi::UniquePtr<ACPI_OBJECT> pkg = std::move(acpi_status.value());
      for (unsigned i = 0; i < pkg->Package.Count; i++) {
        ACPI_OBJECT* ref = &pkg->Package.Elements[i];
        if (ref->Type != ACPI_TYPE_LOCAL_REFERENCE) {
          zxlogf(DEBUG, "acpi: Ignoring wrong type 0x%x", ref->Type);
        } else {
          zxlogf(DEBUG, "acpi: Enabling HID controller at I2C0.H049._PR0[%u]", i);
          acpi_status = acpi->EvaluateObject(ref->Reference.Handle, "_ON", std::nullopt);
          if (acpi_status.is_error()) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C0._PR0._ON", acpi_status.error_value());
          }
        }
      }
    }

  }
  // Acer workaround: Turn on the HID controller.
  else if (!memcmp(&info->Name, "I2C1", 4)) {
    zxlogf(DEBUG, "acpi: Enabling HID controller at I2C1");
    auto acpi_status = acpi->EvaluateObject(object, "_PS0", std::nullopt);
    if (acpi_status.is_error()) {
      zxlogf(ERROR, "acpi: acpi error in I2C1._PS0: 0x%x", acpi_status.error_value());
    }
#ifdef ENABLE_ATLAS_CAMERA
  } else if (!memcmp(&info->Name, "CAM0", 4)) {
    // Atlas workaround: turn on the camera.
    auto acpi_status = acpi->EvaluateObject(object, "_PR0", std::nullopt);
    if (acpi_status.is_ok()) {
      acpi::UniquePtr<ACPI_OBJECT> pkg = std::move(acpi_status.value());
      for (unsigned i = 0; i < pkg->Package.Count; i++) {
        ACPI_OBJECT* ref = &pkg->Package.Elements[i];
        if (ref->Type != ACPI_TYPE_LOCAL_REFERENCE) {
          zxlogf(DEBUG, "acpi: Ignoring wrong type 0x%x", ref->Type);
        } else {
          zxlogf(DEBUG, "acpi: Enabling camera at CAM0._PR0[%u]", i);
          acpi_status = acpi->EvaluateObject(ref->Reference.Handle, "_ON", std::nullopt);
          if (acpi_status.is_error()) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in CAM0._PR0._ON", acpi_status.error_value());
          }
        }
      }
    }
#endif
  }
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
  // TODO(fxbug.dev/81684): remove this once we have a proper solution for managing power of ACPI
  // devices.
  acpi::status<> acpi_status = acpi->WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
      [acpi](ACPI_HANDLE object, uint32_t level, acpi::WalkDirection dir) -> acpi::status<> {
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

        // Apply any workarounds for quirks.
        acpi_apply_workarounds(acpi, object, info.get());

        return acpi::ok();
      });

  if (acpi_status.is_error()) {
    zxlogf(WARNING, "acpi: Error (%d) during fixup and metadata pass", acpi_status.error_value());
  }

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
  acpi_status = acpi->WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
      [acpi_root](ACPI_HANDLE object, uint32_t level, acpi::WalkDirection dir) -> acpi::status<> {
        // We don't have anything useful to do during the ascent phase.  Just
        // skip it.
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

        // Extract pointers to the hardware ID and the compatible ID if present.
        // If there is no hardware ID, just skip the device.
        const std::string_view hid = hid_from_acpi_devinfo(*info);
        if (hid.empty()) {
          return acpi::ok();
        }

        // Now, if we recognize the HID, go ahead and deal with publishing the
        // device.
        if (hid == LID_HID_STRING) {
          lid_init(acpi_root, object);
        } else if (hid == EC_HID_STRING) {
          ec_init(acpi_root, object);
        } else if (hid == GOOGLE_TBMC_HID_STRING) {
          tbmc_init(acpi_root, object);
        }
        return acpi::ok();
      });

  if (acpi_status.is_error()) {
    return ZX_ERR_BAD_STATE;
  } else {
    return ZX_OK;
  }
}
