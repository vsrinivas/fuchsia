// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <magma_util/platform/zircon/zircon_platform_device.h>

#include "msd_vsi_platform_device.h"

class MsdVsiPlatformDeviceZircon : public MsdVsiPlatformDevice {
 public:
  MsdVsiPlatformDeviceZircon(std::unique_ptr<magma::PlatformDevice> platform_device,
                             uint64_t external_sram_phys_base)
      : MsdVsiPlatformDevice(std::move(platform_device)),
        external_sram_phys_base_(external_sram_phys_base) {}

  uint64_t GetExternalSramPhysicalBase() const override { return external_sram_phys_base_; }

 private:
  uint64_t external_sram_phys_base_;
};

std::unique_ptr<MsdVsiPlatformDevice> MsdVsiPlatformDevice::Create(void* platform_device_handle) {
  auto platform_device = magma::PlatformDevice::Create(platform_device_handle);
  if (!platform_device)
    return DRETP(nullptr, "PlatformDevice::Create failed");

  auto zircon_device = static_cast<magma::ZirconPlatformDevice*>(platform_device.get());

  uint64_t external_sram_phys_base;
  uint64_t actual;
  zx_status_t status =
      device_get_metadata(zircon_device->zx_device(), 0 /*type*/, &external_sram_phys_base,
                          sizeof(external_sram_phys_base), &actual);
  if (status != ZX_OK)
    return DRETP(nullptr, "device_get_metadata failed: %d", status);

  DASSERT(actual == sizeof(external_sram_phys_base));

  return std::make_unique<MsdVsiPlatformDeviceZircon>(std::move(platform_device),
                                                      external_sram_phys_base);
}
