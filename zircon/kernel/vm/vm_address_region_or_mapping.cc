// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <vm/vm.h>
#include <vm/vm_aspace.h>

#include "vm/vm_address_region.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

VmAddressRegionOrMapping::VmAddressRegionOrMapping(vaddr_t base, size_t size, uint32_t flags,
                                                   VmAspace* aspace, VmAddressRegion* parent,
                                                   bool is_mapping)
    : is_mapping_(is_mapping),
      state_(LifeCycleState::NOT_READY),
      base_(base),
      size_(size),
      flags_(flags),
      aspace_(aspace),
      parent_(parent) {
  LTRACEF("%p\n", this);
}

zx_status_t VmAddressRegionOrMapping::Destroy() {
  canary_.Assert();

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  return DestroyLocked();
}

VmAddressRegionOrMapping::~VmAddressRegionOrMapping() {
  LTRACEF("%p\n", this);

  if (state_ == LifeCycleState::ALIVE) {
    Destroy();
  }

  DEBUG_ASSERT(!this->in_subregion_tree());
}

VmObject::AttributionCounts VmAddressRegionOrMapping::AllocatedPages() const {
  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return AttributionCounts{};
  }
  return AllocatedPagesLocked();
}
