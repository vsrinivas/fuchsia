// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_device.h"

#include <magma_util/macros.h>

#include "msd_qcom_platform_device.h"

std::unique_ptr<MsdQcomDevice> MsdQcomDevice::Create(void* device_handle) {
  auto device = std::make_unique<MsdQcomDevice>();

  if (!device->Init(device_handle))
    return DRETP(nullptr, "Device init failed");

  return device;
}

bool MsdQcomDevice::Init(void* device_handle) {
  qcom_platform_device_ = MsdQcomPlatformDevice::Create(device_handle);
  if (!qcom_platform_device_)
    return DRETF(false, "Failed to create platform device from handle: %p", device_handle);

  std::unique_ptr<magma::PlatformMmio> mmio(qcom_platform_device_->platform_device()->CpuMapMmio(
      0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
  if (!mmio)
    return DRETF(false, "Failed to map mmio");

  register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));
  if (!register_io_)
    return DRETF(false, "Failed to create register io");

  return true;
}
