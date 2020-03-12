// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>

#include "../../include/dev.h"
#include "../../include/errors.h"
#include "motion.h"

using cros_ec::AcpiCrOsEc;
using cros_ec::AcpiCrOsEcMotionDevice;

namespace cros_ec {

zx_status_t AcpiCrOsEc::Create(fbl::RefPtr<AcpiCrOsEc>* out) {
  if (!CrOsEc::IsLpc3Supported()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  fbl::RefPtr<AcpiCrOsEc> dev = fbl::AdoptRef(new (&ac) AcpiCrOsEc());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t actual;
  zx_status_t status = CrOsEc::CommandLpc3(EC_CMD_GET_FEATURES, 0, nullptr, 0, &dev->features_,
                                           sizeof(dev->features_), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-cros-ec-core: get features failed: %d\n", status);
    return status;
  }

  if (actual != sizeof(dev->features_)) {
    zxlogf(ERROR, "acpi-cros-ec-core: get features bad read: %zu vs %zu\n", actual,
           sizeof(dev->features_));
    return ZX_ERR_IO;
  }

  *out = std::move(dev);
  return ZX_OK;
}

zx_status_t AcpiCrOsEc::IssueCommand(uint16_t command, uint8_t command_version, const void* out,
                                     size_t outsize, void* in, size_t insize, size_t* actual) {
  fbl::AutoLock guard(&io_lock_);
  return CrOsEc::CommandLpc3(command, command_version, out, outsize, in, insize, actual);
}

AcpiCrOsEc::AcpiCrOsEc() {}

AcpiCrOsEc::~AcpiCrOsEc() {}

bool AcpiCrOsEc::supports_motion_sense() const {
  return features_.flags[0] & EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE);
}

bool AcpiCrOsEc::supports_motion_sense_fifo() const {
  return features_.flags[0] & EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE_FIFO);
}

}  // namespace cros_ec

zx_status_t cros_ec_lpc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  zxlogf(TRACE, "acpi-cros-ec-core: init\n");

  fbl::RefPtr<AcpiCrOsEc> ec;
  zx_status_t status = AcpiCrOsEc::Create(&ec);
  if (status != ZX_OK) {
    return status;
  }

  if (ec->supports_motion_sense()) {
    zxlogf(TRACE, "acpi-cros-ec-motion: init\n");
    // Set up motion device
    std::unique_ptr<AcpiCrOsEcMotionDevice> motion_dev;
    status = AcpiCrOsEcMotionDevice::Create(ec, parent, acpi_handle, &motion_dev);
    if (status == ZX_OK) {
      status = motion_dev->DdkAdd("acpi-cros-ec-motion");
      if (status != ZX_OK) {
        return status;
      }

      // devmgr is now in charge of the memory for motion_dev
      __UNUSED auto ptr = motion_dev.release();
      zxlogf(INFO, "acpi-cros-ec-motion: initialized\n");
    }
  }

  zxlogf(INFO, "acpi-cros-ec-core: initialized\n");
  return ZX_OK;
}
