// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/iommu.h>
#include <lib/zx/status.h>

#ifndef SRC_DEVICES_LIB_IOMMU_IOMMU_H_
#define SRC_DEVICES_LIB_IOMMU_IOMMU_H_

namespace iommu {

// Base class for IOMMU manager implementation, regardless of architecture.
// On x86, this is backed by the DMAR table.
// On ARM it's backed by the IORT table, or some other platform-specific information.
class IommuManagerBase {
 public:
  // Return the IOMMU for the given PCI device.
  virtual zx::unowned_iommu IommuForBdf(uint32_t bdf) = 0;

  // Return the IOMMU for the given ACPI device.
  virtual zx::unowned_iommu IommuForAcpiDevice(const char *absolute_path) = 0;
};

}  // namespace iommu

#endif
