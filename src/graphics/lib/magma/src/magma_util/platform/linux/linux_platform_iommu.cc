// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_iommu.h"

#include "linux_platform_bus_mapper.h"
#include "linux_platform_device.h"

namespace magma {

bool LinuxPlatformIommu::Map(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) {
  if (!LinuxPlatformDevice::MagmaMapGpu(
          handle_.get(), true, gpu_addr,
          static_cast<LinuxPlatformBusMapper::BusMapping*>(bus_mapping)->token()))
    return DRETF(false, "MagmaMapGpu failed");
  return true;
}

bool LinuxPlatformIommu::Unmap(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) {
  if (!LinuxPlatformDevice::MagmaMapGpu(
          handle_.get(), false, gpu_addr,
          static_cast<LinuxPlatformBusMapper::BusMapping*>(bus_mapping)->token()))
    return DRETF(false, "MagmaMapGpu failed");
  return true;
}

std::unique_ptr<PlatformIommu> PlatformIommu::Create(
    std::unique_ptr<PlatformHandle> iommu_connector) {
  return std::make_unique<LinuxPlatformIommu>(iommu_connector->release());
}

}  // namespace magma
