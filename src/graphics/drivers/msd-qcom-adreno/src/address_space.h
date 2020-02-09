// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include <platform_iommu.h>

#include <magma_util/address_space.h>

#include "gpu_mapping.h"

// This address space is used directly by clients/connections.
// It uses a shared instance of PlatformIommu, and because of current
// limitations with PlatformIommu, all AddressSpace instances
// shared a single shared underlying address space.  To avoid collisions, this means
// that currently only one client address space instance  is supported.
// The address space is assumed to begin at address 0.
class AddressSpace : public magma::AddressSpace<GpuMapping> {
 public:
  explicit AddressSpace(magma::AddressSpaceOwner* owner, uint64_t size,
                        std::shared_ptr<magma::PlatformIommu> iommu)
      : magma::AddressSpace<GpuMapping>(owner), iommu_(std::move(iommu)), size_(size) {}

  uint64_t Size() const override { return size_; }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    DASSERT(iommu_);
    if (addr >= size_)
      return DRETF(false, "addr 0x%lx out of range", addr);
    return iommu_->Map(addr, bus_mapping);
  }

  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    DASSERT(iommu_);
    if (addr >= size_)
      return DRETF(false, "addr 0x%lx out of range", addr);
    return iommu_->Unmap(addr, bus_mapping);
  }

 private:
  std::shared_ptr<magma::PlatformIommu> iommu_;
  uint64_t size_;
};

#endif  // ADDRESS_SPACE_H
