// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "domain_allocator.h"

#include <assert.h>

namespace intel_iommu {

DomainAllocator::DomainAllocator()
    // Note that next_domain_id_ starts at 1, since under some conditions 0 is
    // an invalid domain ID (i.e. if CM is set in the capability register).
    : num_domains_(0), next_domain_id_(1) {}

zx_status_t DomainAllocator::Allocate(uint32_t* domain_id) {
  if (next_domain_id_ >= num_domains_) {
    return ZX_ERR_NO_RESOURCES;
  }

  // This allocator should be enough, since the hardware should have enough
  // domain IDs for each device hanging off of it. If we start deallocating
  // Context Entries, we'll need to make this allocator more sophisticated to
  // manage the ID reuse.
  *domain_id = next_domain_id_++;
  return ZX_OK;
}

void DomainAllocator::set_num_domains(uint32_t num) {
  ASSERT(num >= next_domain_id_);
  num_domains_ = num;
}

}  // namespace intel_iommu
