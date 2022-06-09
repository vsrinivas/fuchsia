// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/iommu/iommu.h"

#ifndef SRC_DEVICES_LIB_IOMMU_IOMMU_ARM_H_
#define SRC_DEVICES_LIB_IOMMU_IOMMU_ARM_H_

namespace iommu {

// IOMMU manager implementation for ARM.
// This class does not yet do anything, but one day it should read from the IORT to determine IOMMU
// information.
class ArmIommuManager : public iommu::IommuManagerInterface {
 public:
  zx_status_t Init(zx::unowned_resource root_resource);

  zx::unowned_iommu IommuForPciDevice(uint32_t bdf) override { return dummy_iommu_.borrow(); }
  zx::unowned_iommu IommuForAcpiDevice(std::string_view path) override {
    return dummy_iommu_.borrow();
  }

 private:
  zx::iommu dummy_iommu_;
};

}  // namespace iommu

#endif
