// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_IOMMU_H
#define PLATFORM_IOMMU_H

#include <memory>

#include "platform_bus_mapper.h"

namespace magma {

class PlatformIommu {
 public:
  virtual ~PlatformIommu() = default;

  virtual bool Map(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) = 0;
  virtual bool Unmap(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) = 0;

  static std::unique_ptr<PlatformIommu> Create(std::unique_ptr<PlatformHandle> iommu_connector);

 protected:
  PlatformIommu() = default;
};

}  // namespace magma

#endif  // PLATFORM_IOMMU_H
