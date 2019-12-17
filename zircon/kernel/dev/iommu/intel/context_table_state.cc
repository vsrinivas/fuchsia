// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "context_table_state.h"

#include <new>

#include <ktl/move.h>
#include <ktl/unique_ptr.h>

#include "device_context.h"
#include "hw.h"
#include "iommu_impl.h"

namespace intel_iommu {

ContextTableState::ContextTableState(uint8_t bus, bool extended, bool upper, IommuImpl* parent,
                                     volatile ds::RootEntrySubentry* root_entry, IommuPage page)
    : parent_(parent),
      root_entry_(root_entry),
      page_(ktl::move(page)),
      bus_(bus),
      extended_(extended),
      upper_(upper) {}

ContextTableState::~ContextTableState() {
  ds::RootEntrySubentry entry;
  entry.ReadFrom(root_entry_);
  entry.set_present(0);
  entry.WriteTo(root_entry_);

  // When modifying a present (extended) root entry, we must serially
  // invalidate the context-cache, the PASID-cache, then the IOTLB (see
  // 6.2.2.1 "Context-Entry Programming Considerations" in the VT-d spec,
  // Oct 2014 rev).
  parent_->InvalidateContextCacheGlobal();
  // TODO(teisenbe): Invalidate the PASID cache once we support those
  parent_->InvalidateIotlbGlobal();
}

zx_status_t ContextTableState::Create(uint8_t bus, bool extended, bool upper, IommuImpl* parent,
                                      volatile ds::RootEntrySubentry* root_entry,
                                      ktl::unique_ptr<ContextTableState>* table) {
  ds::RootEntrySubentry entry;
  entry.ReadFrom(root_entry);
  DEBUG_ASSERT(!entry.present());

  IommuPage page;
  zx_status_t status = IommuPage::AllocatePage(&page);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<ContextTableState> tbl(
      new (&ac) ContextTableState(bus, extended, upper, parent, root_entry, ktl::move(page)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  entry.set_present(1);
  entry.set_context_table(tbl->page_.paddr() >> 12);
  entry.WriteTo(root_entry);

  *table = ktl::move(tbl);
  return ZX_OK;
}

zx_status_t ContextTableState::CreateDeviceContext(ds::Bdf bdf, uint32_t domain_id,
                                                   DeviceContext** context) {
  DEBUG_ASSERT(bus_ == bdf.bus());

  ktl::unique_ptr<DeviceContext> dev;
  zx_status_t status;
  if (extended_) {
    DEBUG_ASSERT(upper_ == (bdf.dev() >= 16));
    volatile ds::ExtendedContextTable* tbl = extended_table();
    volatile ds::ExtendedContextEntry* entry = &tbl->entry[bdf.packed_dev_and_func() & 0x7f];
    status = DeviceContext::Create(bdf, domain_id, parent_, entry, &dev);
  } else {
    volatile ds::ContextTable* tbl = table();
    volatile ds::ContextEntry* entry = &tbl->entry[bdf.packed_dev_and_func()];
    status = DeviceContext::Create(bdf, domain_id, parent_, entry, &dev);
  }
  if (status != ZX_OK) {
    return status;
  }

  *context = dev.get();
  devices_.push_back(ktl::move(dev));
  return ZX_OK;
}

zx_status_t ContextTableState::GetDeviceContext(ds::Bdf bdf, DeviceContext** context) {
  for (auto& dev : devices_) {
    if (dev.is_bdf(bdf)) {
      *context = &dev;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void ContextTableState::UnmapAllFromDeviceContextsLocked() {
  for (auto& dev : devices_) {
    dev.SecondLevelUnmapAllLocked();
  }
}

}  // namespace intel_iommu
