// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>
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

namespace cros_ec {

// An EmbeddedController wired up to real hardware.
class RealEmbeddedController : public EmbeddedController {
 public:
  // Create a RealEmbeddedController, connected to the system's hardware.
  static zx_status_t Create(fbl::RefPtr<EmbeddedController>* out);
  ~RealEmbeddedController() = default;

  // |EmbeddedController| interface implementation.
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* out,
                           size_t outsize, void* in, size_t insize, size_t* actual) override;
  virtual bool SupportsFeature(enum ec_feature_code feature) const override;

 private:
  RealEmbeddedController() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(RealEmbeddedController);

  fbl::Mutex io_lock_;
  struct ec_response_get_features features_;
};

zx_status_t RealEmbeddedController::Create(fbl::RefPtr<EmbeddedController>* out) {
  if (!CrOsEc::IsLpc3Supported()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  fbl::RefPtr<RealEmbeddedController> dev = fbl::AdoptRef(new (&ac) RealEmbeddedController());
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

zx_status_t RealEmbeddedController::IssueCommand(uint16_t command, uint8_t command_version,
                                                 const void* out, size_t outsize, void* in,
                                                 size_t insize, size_t* actual) {
  fbl::AutoLock guard(&io_lock_);
  return CrOsEc::CommandLpc3(command, command_version, out, outsize, in, insize, actual);
}

bool RealEmbeddedController::SupportsFeature(enum ec_feature_code feature) const {
  return features_.flags[0] & EC_FEATURE_MASK_0(feature);
}

zx_status_t InitDevices(fbl::RefPtr<EmbeddedController> controller, zx_device_t* parent,
                        ACPI_HANDLE acpi_handle) {
  // Initialize MotionSense driver.
  if (controller->SupportsFeature(EC_FEATURE_MOTION_SENSE)) {
    zxlogf(TRACE, "acpi-cros-ec-motion: init\n");
    zx_status_t status =
        AcpiCrOsEcMotionDevice::Bind(parent, controller, CreateAcpiHandle(acpi_handle), nullptr);
    if (status != ZX_OK) {
      zxlogf(INFO, "acpi-cros-ec-motion: failed to initialize: %s\n", zx_status_get_string(status));
    } else {
      zxlogf(INFO, "acpi-cros-ec-motion: initialized.\n");
    }
  }

  zxlogf(INFO, "acpi-cros-ec-core: initialized\n");
  return ZX_OK;
}

}  // namespace cros_ec

zx_status_t cros_ec_lpc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  zxlogf(TRACE, "acpi-cros-ec-core: init\n");

  fbl::RefPtr<cros_ec::EmbeddedController> ec;
  zx_status_t status = cros_ec::RealEmbeddedController::Create(&ec);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-cros-ec-core: Failed to initialise EC: %s\n", zx_status_get_string(status));
    return status;
  }

  return cros_ec::InitDevices(std::move(ec), parent, acpi_handle);
}
