// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <lib/ddk/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
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
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* input,
                           size_t input_size, void* result, size_t result_buff_size,
                           size_t* actual) override;
  virtual bool SupportsFeature(enum ec_feature_code feature) const override;

 private:
  RealEmbeddedController() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(RealEmbeddedController);

  fbl::Mutex io_lock_;
  struct ec_response_get_features features_;
};

zx_status_t RealEmbeddedController::Create(fbl::RefPtr<EmbeddedController>* out) {
  // Enable access to the ranges of IO ports required for communication with the EC.
  //
  // This list is not available via ACPI, so we need to hard-code it.
  struct PortRange {
    uint16_t base;
    uint16_t size;
  };
  for (const auto& region : (PortRange[]){
           {EC_HOST_CMD_REGION0, EC_HOST_CMD_REGION_SIZE},
           {EC_HOST_CMD_REGION1, EC_HOST_CMD_REGION_SIZE},
           {EC_LPC_ADDR_ACPI_DATA, 4},
           {EC_LPC_ADDR_ACPI_CMD, 4},
           {EC_LPC_ADDR_HOST_DATA, 4},
           {EC_LPC_ADDR_HOST_CMD, 4},
           {EC_LPC_ADDR_MEMMAP, EC_MEMMAP_SIZE},
       }) {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx_status_t status = zx_ioports_request(get_root_resource(), region.base, region.size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "acpi-cros-ec-core: ioports request for range %d--%d failed: %s", region.base,
             region.base + region.size - 1, zx_status_get_string(status));
      return status;
    }
  }

  // Ensure we have a supported EC.
  if (!CrOsEc::IsLpc3Supported()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Create the controller.
  fbl::AllocChecker ac;
  fbl::RefPtr<RealEmbeddedController> dev = fbl::AdoptRef(new (&ac) RealEmbeddedController());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Cache the feature set.
  zx_status_t status =
      dev->EmbeddedController::IssueCommand(EC_CMD_GET_FEATURES, 0, &dev->features_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-cros-ec-core: get features failed: %d", status);
    return status;
  }

  *out = std::move(dev);
  return ZX_OK;
}

zx_status_t RealEmbeddedController::IssueCommand(uint16_t command, uint8_t command_version,
                                                 const void* input, size_t input_size, void* result,
                                                 size_t result_buff_size, size_t* actual) {
  fbl::AutoLock guard(&io_lock_);
  return CrOsEc::CommandLpc3(command, command_version, input, input_size, result, result_buff_size,
                             actual);
}

bool RealEmbeddedController::SupportsFeature(enum ec_feature_code feature) const {
  return features_.flags[0] & EC_FEATURE_MASK_0(feature);
}

zx_status_t GetECVersion(EmbeddedController* controller, ec_response_get_version* version) {
  // Fetch the version strings.
  zx_status_t status = controller->IssueCommand(EC_CMD_GET_VERSION, 0, version);
  if (status != ZX_OK) {
    return status;
  }

  // The spec states that returned strings should be NUL terminated, but we re-write
  // the final NUL to avoid undefined behaviour if the EC is out of spec.
  version->version_string_ro[sizeof(version->version_string_ro) - 1] = 0;
  version->version_string_rw[sizeof(version->version_string_rw) - 1] = 0;
  return ZX_OK;
}

zx_status_t InitDevices(fbl::RefPtr<EmbeddedController> controller, zx_device_t* parent,
                        ACPI_HANDLE acpi_handle) {
  // Get EC version.
  ec_response_get_version version;
  if (zx_status_t status = GetECVersion(controller.get(), &version); status != ZX_OK) {
    zxlogf(DEBUG, "acpi-cros-ec-core: failed to get EC version details.");
    return status;
  }
  zxlogf(INFO, "acpi-cros-ec-core: Detected EC firmware version %s (RO), %s (RW).",
         version.version_string_ro, version.version_string_rw);

  // Initialize MotionSense driver.
  if (controller->SupportsFeature(EC_FEATURE_MOTION_SENSE)) {
    zxlogf(DEBUG, "acpi-cros-ec-motion: init");
    zx_status_t status =
        AcpiCrOsEcMotionDevice::Bind(parent, controller, CreateAcpiHandle(acpi_handle), nullptr);
    if (status != ZX_OK) {
      zxlogf(INFO, "acpi-cros-ec-motion: failed to initialize: %s", zx_status_get_string(status));
    } else {
      zxlogf(INFO, "acpi-cros-ec-motion: initialized.");
    }
  }

  zxlogf(INFO, "acpi-cros-ec-core: initialized");
  return ZX_OK;
}

}  // namespace cros_ec

zx_status_t cros_ec_lpc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  zxlogf(DEBUG, "acpi-cros-ec-core: init");

  fbl::RefPtr<cros_ec::EmbeddedController> ec;
  zx_status_t status = cros_ec::RealEmbeddedController::Create(&ec);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-cros-ec-core: Failed to initialise EC: %s", zx_status_get_string(status));
    return status;
  }

  return cros_ec::InitDevices(std::move(ec), parent, acpi_handle);
}
