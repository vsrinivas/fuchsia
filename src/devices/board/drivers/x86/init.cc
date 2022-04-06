// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <limits.h>

#include <acpica/acpi.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"
#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/util.h"
#include "x86.h"

#define ACPI_MAX_INIT_TABLES 32

namespace {

/* @brief Switch interrupts to APIC model (controls IRQ routing) */
ACPI_STATUS set_apic_irq_mode(acpi::Acpi* acpi) {
  ACPI_OBJECT selector = acpi::MakeAcpiObject(1);  // 1 means APIC mode according to ACPI v5 5.8.1
  return acpi->EvaluateObject(nullptr, "\\_PIC", std::vector({selector})).status_value();
}

int is_gpe_device(ACPI_HANDLE object) {
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;
  if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
    return 0;
  } else {
    info = std::move(res.value());
  }

  // These length fields count the trailing NUL.
  if ((info->Valid & ACPI_VALID_HID) && info->HardwareId.Length <= HID_LENGTH + 1) {
    if (!strncmp(info->HardwareId.String, GPE_HID_STRING, HID_LENGTH)) {
      return 1;
    }
  }
  if ((info->Valid & ACPI_VALID_CID) && info->CompatibleIdList.Count > 0) {
    ACPI_PNP_DEVICE_ID* id = &info->CompatibleIdList.Ids[0];
    if (!strncmp(id->String, GPE_CID_STRING, CID_LENGTH)) {
      return 1;
    }
  }

  return 0;
}

acpi::status<> acpi_prw_walk(ACPI_HANDLE obj, uint32_t level, acpi::WalkDirection dir,
                             acpi::Acpi* acpi) {
  if (dir == acpi::WalkDirection::Ascending) {
    return acpi::ok();
  }

  auto status = acpi->EvaluateObject(obj, (char*)"_PRW", std::nullopt);
  if (status.is_error()) {
    return acpi::ok();  // Keep walking the tree
  }
  acpi::UniquePtr<ACPI_OBJECT> prw_res = std::move(status.value());

  // _PRW returns a package with >= 2 entries. The first entry indicates what type of
  // event it is. If it's a GPE event, the first entry is either an integer indicating
  // the bit within the FADT GPE enable register or it is a package containing a handle
  // to a GPE block device and the bit index on that device. There are other event
  // types with (handle, int) packages, so check that the handle is a GPE device by
  // checking against the CID/HID required by the ACPI spec.
  if (prw_res->Type != ACPI_TYPE_PACKAGE || prw_res->Package.Count < 2) {
    return acpi::ok();  // Keep walking the tree
  }

  ACPI_HANDLE gpe_block;
  UINT32 gpe_bit;
  ACPI_OBJECT* event_info = &prw_res->Package.Elements[0];
  if (event_info->Type == ACPI_TYPE_INTEGER) {
    gpe_block = NULL;
    gpe_bit = static_cast<UINT32>(prw_res->Package.Elements[0].Integer.Value);
  } else if (event_info->Type == ACPI_TYPE_PACKAGE) {
    if (event_info->Package.Count != 2) {
      return acpi::ok();
      ;
    }
    ACPI_OBJECT* handle_obj = &event_info->Package.Elements[0];
    ACPI_OBJECT* gpe_num_obj = &event_info->Package.Elements[1];
    if (handle_obj->Type != ACPI_TYPE_LOCAL_REFERENCE ||
        !is_gpe_device(handle_obj->Reference.Handle)) {
      return acpi::ok();
      ;
    }
    if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
      return acpi::ok();
      ;
    }
    gpe_block = handle_obj->Reference.Handle;
    gpe_bit = static_cast<UINT32>(gpe_num_obj->Integer.Value);
  } else {
    return acpi::ok();
    ;
  }
  if (AcpiSetupGpeForWake(obj, gpe_block, gpe_bit) != AE_OK) {
    zxlogf(INFO, "Acpi failed to setup wake GPE");
  }

  return acpi::ok();
}

ACPI_STATUS acpi_sub_init(acpi::Acpi* acpi) {
  // This sequence is described in section 10.1.2.1 (Full ACPICA Initialization)
  // of the ACPICA developer's reference.
  ACPI_STATUS status = AcpiInitializeSubsystem();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI: %s", zx_status_get_string(status));
    return status;
  }

  status = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, FALSE);
  if (status == AE_NOT_FOUND) {
    zxlogf(WARNING, "Could not find ACPI tables");
    return status;
  } else if (status == AE_NO_MEMORY) {
    zxlogf(WARNING, "Could not initialize ACPI tables");
    return status;
  } else if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI tables for unknown reason");
    return status;
  }

  status = AcpiLoadTables();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not load ACPI tables: %s", zx_status_get_string(status));
    return status;
  }

  status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not enable ACPI: %s", zx_status_get_string(status));
    return status;
  }

  status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI objects: %s", zx_status_get_string(status));
    return status;
  }

  status = set_apic_irq_mode(acpi);
  if (status == AE_NOT_FOUND) {
    zxlogf(WARNING, "Could not find ACPI IRQ mode switch");
  } else if (status != AE_OK) {
    zxlogf(WARNING, "Failed to set APIC IRQ mode: %s", zx_status_get_string(status));
    return status;
  }

  (void)acpi->WalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, INT_MAX,
                            [acpi](ACPI_HANDLE obj, uint32_t depth, acpi::WalkDirection dir) {
                              return acpi_prw_walk(obj, depth, dir, acpi);
                            });

  status = AcpiUpdateAllGpes();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI GPEs: %s", zx_status_get_string(status));
    return status;
  }

  // TODO(teisenbe): Maybe back out of ACPI mode on failure, but we rely on
  // ACPI for some critical things right now, so failure will likely prevent
  // successful boot anyway.
  return AE_OK;
}

}  // namespace

namespace x86 {

zx_status_t X86::EarlyAcpiInit() {
  ZX_DEBUG_ASSERT(!acpica_initialized_);
  // First initialize the ACPI subsystem.
  zx_status_t status = acpi_to_zx_status(acpi_sub_init(acpi_.get()));
  if (status != ZX_OK) {
    return status;
  }
  acpica_initialized_ = true;
  return ZX_OK;
}

zx_status_t X86::EarlyInit() {
  zx_status_t status = EarlyAcpiInit();
  if (status != ZX_OK) {
    return status;
  }
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  // Now initialize the IOMMU manager. Any failures in setting it up we consider non-fatal and do
  // not propagate.
  status = iommu_manager_.Init(std::move(root_resource), false /* force_hardware_iommu */);
  if (status != ZX_OK) {
    zxlogf(INFO, "acpi: Failed to initialize IOMMU manager: %s", zx_status_get_string(status));
  }
  return ZX_OK;
}

}  // namespace x86
