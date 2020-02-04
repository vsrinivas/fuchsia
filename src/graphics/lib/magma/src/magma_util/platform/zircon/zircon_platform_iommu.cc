// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_iommu.h"

namespace magma {

class ZirconPlatformIommu : public PlatformIommu {
 public:
  bool Map(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) override {
    return DRETF(false, "Map not implemented");
  }

  bool Unmap(uint64_t gpu_addr, PlatformBusMapper::BusMapping* bus_mapping) override {
    return DRETF(false, "Unmap not implemented");
  }
};

std::unique_ptr<PlatformIommu> PlatformIommu::Create(
    std::unique_ptr<PlatformHandle> iommu_connector) {
  return std::make_unique<ZirconPlatformIommu>();
}

}  // namespace magma
