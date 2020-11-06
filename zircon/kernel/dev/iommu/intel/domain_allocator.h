// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_IOMMU_INTEL_DOMAIN_ALLOCATOR_H_
#define ZIRCON_KERNEL_DEV_IOMMU_INTEL_DOMAIN_ALLOCATOR_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/macros.h>

namespace intel_iommu {

// Manages the domain ID space for a given IOMMU.  This is not thread-safe.
class DomainAllocator {
 public:
  DomainAllocator();

  // Get an unused domain ID.
  // Returns ZX_ERR_NO_RESOURCES if one cannot be found.
  zx_status_t Allocate(uint32_t* domain_id);

  // Set the number of domain IDs this instance manages.  Panics if this call
  // would reduce the max domain ID to below the current highest allocated one.
  void set_num_domains(uint32_t num);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(DomainAllocator);

  uint32_t num_domains_;
  uint32_t next_domain_id_;
};

}  // namespace intel_iommu

#endif  // ZIRCON_KERNEL_DEV_IOMMU_INTEL_DOMAIN_ALLOCATOR_H_
