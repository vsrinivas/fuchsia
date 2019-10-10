// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include <platform_iommu.h>

#include <magma_util/address_space.h>

#include "gpu_mapping.h"

class AddressSpace : public magma::AddressSpace<GpuMapping> {
 public:
  explicit AddressSpace(magma::AddressSpaceOwner* owner) : magma::AddressSpace<GpuMapping>(owner) {}

  void Init(std::unique_ptr<magma::PlatformIommu> iommu) { iommu_ = std::move(iommu); }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    DASSERT(iommu_);
    return iommu_->Map(addr, bus_mapping);
  }

  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    DASSERT(iommu_);
    return iommu_->Unmap(addr, bus_mapping);
  }

 private:
  std::unique_ptr<magma::PlatformIommu> iommu_;
};

#endif  // ADDRESS_SPACE_H
