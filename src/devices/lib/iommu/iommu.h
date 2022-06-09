// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/iommu.h>
#include <lib/zx/status.h>

#include <string_view>

#ifndef SRC_DEVICES_LIB_IOMMU_IOMMU_H_
#define SRC_DEVICES_LIB_IOMMU_IOMMU_H_

namespace iommu {

// Interface for IOMMU manager implementation, regardless of architecture.
// On x86, a concrete implementation would be backed by the DMAR table.
// On ARM a concrete implementation would be backed by the IORT table, or some other
// platform-specific knowledge.
class IommuManagerInterface {
 public:
  // Return the IOMMU for the given PCI device.
  // The returned IOMMU handle will last as long as the IOMMU manager.
  virtual zx::unowned_iommu IommuForPciDevice(uint32_t bdf) = 0;

  // Return the IOMMU for the given ACPI device.
  // The returned IOMMU handle will last as long as the IOMMU manager.
  virtual zx::unowned_iommu IommuForAcpiDevice(std::string_view absolute_path) = 0;
};

}  // namespace iommu

#endif
