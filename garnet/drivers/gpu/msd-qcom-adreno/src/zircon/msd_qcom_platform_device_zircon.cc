// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magma_util/macros.h>
#include <magma_util/platform/zircon/zircon_platform_device.h>

#include "../msd_qcom_platform_device.h"

class MsdQcomPlatformDeviceZircon : public MsdQcomPlatformDevice {
 public:
  MsdQcomPlatformDeviceZircon(std::unique_ptr<magma::PlatformDevice> platform_device)
      : MsdQcomPlatformDevice(std::move(platform_device)) {}

  uint32_t GetChipId() const override {
    DMESSAGE("GetChipId not implemented");
    return 0;
  }

  uint32_t GetGmemSize() const override {
    DMESSAGE("GetGmemSize not implemented");
    return 0;
  }

  void ResetGmu() override { DMESSAGE("ResetGmu not implemented"); }
};

std::unique_ptr<MsdQcomPlatformDevice> MsdQcomPlatformDevice::Create(void* platform_device_handle) {
  auto platform_device = magma::PlatformDevice::Create(platform_device_handle);
  if (!platform_device)
    return DRETP(nullptr, "Couldn't create PlatformDevice");

  return std::unique_ptr<MsdQcomPlatformDevice>(
      new MsdQcomPlatformDeviceZircon(std::move(platform_device)));
}
