// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_TEST_NULL_IOMMU_MANAGER_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_TEST_NULL_IOMMU_MANAGER_H_

#include <lib/zx/iommu.h>

#include "src/devices/lib/iommu/iommu.h"

class NullIommuManager : public iommu::IommuManagerInterface {
  zx::unowned_iommu IommuForPciDevice(uint32_t bdf) override { return zx::unowned_iommu(); }
  zx::unowned_iommu IommuForAcpiDevice(std::string_view absolute_path) override {
    return zx::unowned_iommu();
  }
};

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_TEST_NULL_IOMMU_MANAGER_H_
