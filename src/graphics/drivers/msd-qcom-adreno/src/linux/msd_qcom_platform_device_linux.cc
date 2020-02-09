// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magma_util/platform/linux/linux_platform_device.h>

#include "../msd_qcom_platform_device.h"

class MsdQcomPlatformDeviceLinux : public MsdQcomPlatformDevice {
 public:
  MsdQcomPlatformDeviceLinux(std::unique_ptr<magma::PlatformDevice> platform_device,
                             uint32_t chip_id, uint32_t gmem_size)
      : MsdQcomPlatformDevice(std::move(platform_device)),
        chip_id_(chip_id),
        gmem_size_(gmem_size) {}

  uint32_t GetChipId() const override { return chip_id_; }

  uint32_t GetGmemSize() const override { return gmem_size_; }

  void ResetGmu() override {
    magma::LinuxPlatformDevice::MagmaResetGmu(linux_platform_device()->fd());
  }

  magma::LinuxPlatformDevice* linux_platform_device() {
    return reinterpret_cast<magma::LinuxPlatformDevice*>(platform_device_.get());
  }

 private:
  uint32_t chip_id_;
  // Gmem is on-chip graphics memory
  uint32_t gmem_size_;
};

std::unique_ptr<MsdQcomPlatformDevice> MsdQcomPlatformDevice::Create(void* platform_device_handle) {
  auto platform_device = magma::PlatformDevice::Create(platform_device_handle);
  if (!platform_device)
    return DRETP(nullptr, "Couldn't create PlatformDevice");

  auto linux_platform_device = static_cast<magma::LinuxPlatformDevice*>(platform_device.get());

  uint64_t chip_id;
  if (!magma::LinuxPlatformDevice::MagmaGetParam(
          linux_platform_device->fd(), magma::LinuxPlatformDevice::MagmaGetParamKey::kChipId,
          &chip_id))
    return DRETP(nullptr, "Couldn't get chip id");
  DASSERT((chip_id >> 32) == 0);

  uint64_t gmem_size;
  if (!magma::LinuxPlatformDevice::MagmaGetParam(
          linux_platform_device->fd(), magma::LinuxPlatformDevice::MagmaGetParamKey::kGmemSize,
          &gmem_size))
    return DRETP(nullptr, "Couldn't get chip id");
  DASSERT((gmem_size >> 32) == 0);

  return std::unique_ptr<MsdQcomPlatformDevice>(
      new MsdQcomPlatformDeviceLinux(std::move(platform_device), chip_id, gmem_size));
}
