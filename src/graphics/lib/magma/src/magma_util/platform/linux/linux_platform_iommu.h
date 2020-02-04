// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_IOMMU_H
#define LINUX_PLATFORM_IOMMU_H

#include "linux_platform_handle.h"
#include "platform_iommu.h"

namespace magma {

class LinuxPlatformIommu : public PlatformIommu {
 public:
  LinuxPlatformIommu(LinuxPlatformHandle handle) : handle_(handle.release()) {}

  bool Map(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) override;
  bool Unmap(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) override;

 private:
  LinuxPlatformHandle handle_;
};

}  // namespace magma

#endif  // LINUX_PLATFORM_IOMMU_H
