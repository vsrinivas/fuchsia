// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "device_context.h"

#include <align.h>
#include <trace.h>

#include <new>

#include <fbl/auto_call.h>
#include <kernel/range_check.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>

#include "hw.h"
#include "iommu_impl.h"

#define LOCAL_TRACE 0

namespace intel_iommu {

DeviceContext::DeviceContext(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                             volatile ds::ExtendedContextEntry* context_entry)
    : parent_(parent),
      extended_context_entry_(context_entry),
      second_level_pt_(parent, this),
      region_alloc_(),
      bdf_(bdf),
      extended_(true),
      domain_id_(domain_id) {}

DeviceContext::DeviceContext(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                             volatile ds::ContextEntry* context_entry)
    : parent_(parent),
      context_entry_(context_entry),
      second_level_pt_(parent, this),
      region_alloc_(),
      bdf_(bdf),
      extended_(false),
      domain_id_(domain_id) {}

DeviceContext::~DeviceContext() {
  bool was_present;
  if (extended_) {
    ds::ExtendedContextEntry entry;
    entry.ReadFrom(extended_context_entry_);
    was_present = entry.present();
    entry.set_present(0);
    entry.WriteTo(extended_context_entry_);
  } else {
    ds::ContextEntry entry;
    entry.ReadFrom(context_entry_);
    was_present = entry.present();
    entry.set_present(0);
    entry.WriteTo(context_entry_);
  }

  if (was_present) {
    // When modifying a present (extended) context entry, we must serially
    // invalidate the context-cache, the PASID-cache, then the IOTLB (see
    // 6.2.2.1 "Context-Entry Programming Considerations" in the VT-d spec,
    // Oct 2014 rev).
    parent_->InvalidateContextCacheDomain(domain_id_);
    // TODO(teisenbe): Invalidate the PASID cache once we support those
    parent_->InvalidateIotlbDomainAll(domain_id_);
  }

  second_level_pt_.Destroy();
}

zx_status_t DeviceContext::InitCommon() {
  // TODO(teisenbe): don't hardcode PML4_L
  DEBUG_ASSERT(parent_->caps()->supports_48_bit_agaw());
  zx_status_t status = second_level_pt_.Init(PML4_L);
  if (status != ZX_OK) {
    return status;
  }

  constexpr size_t kMaxAllocatorMemoryUsage = 16 * PAGE_SIZE;
  fbl::RefPtr<RegionAllocator::RegionPool> region_pool =
      RegionAllocator::RegionPool::Create(kMaxAllocatorMemoryUsage);
  if (region_pool == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  status = region_alloc_.SetRegionPool(ktl::move(region_pool));
  if (status != ZX_OK) {
    return status;
  }

  // Start the allocations at 1MB to handle the equivalent of nullptr
  // dereferences.
  uint64_t base = 1ull << 20;
  uint64_t size = aspace_size() - base;
  region_alloc_.AddRegion({.base = 1ull << 20, .size = size});
  return ZX_OK;
}

zx_status_t DeviceContext::Create(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                                  volatile ds::ContextEntry* context_entry,
                                  ktl::unique_ptr<DeviceContext>* device) {
  ds::ContextEntry entry;
  entry.ReadFrom(context_entry);

  // It's a bug if we're trying to re-initialize an existing entry
  ASSERT(!entry.present());

  fbl::AllocChecker ac;
  ktl::unique_ptr<DeviceContext> dev(new (&ac)
                                         DeviceContext(bdf, domain_id, parent, context_entry));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = dev->InitCommon();
  if (status != ZX_OK) {
    return status;
  }

  entry.set_present(1);
  entry.set_fault_processing_disable(0);
  entry.set_translation_type(ds::ContextEntry::kDeviceTlbDisabled);
  // TODO(teisenbe): don't hardcode this
  entry.set_address_width(ds::ContextEntry::k48Bit);
  entry.set_domain_id(domain_id);
  entry.set_second_level_pt_ptr(dev->second_level_pt_.phys() >> 12);

  entry.WriteTo(context_entry);

  *device = ktl::move(dev);
  return ZX_OK;
}

zx_status_t DeviceContext::Create(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                                  volatile ds::ExtendedContextEntry* context_entry,
                                  ktl::unique_ptr<DeviceContext>* device) {
  ds::ExtendedContextEntry entry;
  entry.ReadFrom(context_entry);

  // It's a bug if we're trying to re-initialize an existing entry
  ASSERT(!entry.present());

  fbl::AllocChecker ac;
  ktl::unique_ptr<DeviceContext> dev(new (&ac)
                                         DeviceContext(bdf, domain_id, parent, context_entry));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = dev->InitCommon();
  if (status != ZX_OK) {
    return status;
  }

  entry.set_present(1);
  entry.set_fault_processing_disable(0);
  entry.set_translation_type(ds::ExtendedContextEntry::kHostModeWithDeviceTlbDisabled);
  entry.set_deferred_invld_enable(0);
  entry.set_page_request_enable(0);
  entry.set_nested_translation_enable(0);
  entry.set_pasid_enable(0);
  entry.set_global_page_enable(0);
  // TODO(teisenbe): don't hardcode this
  entry.set_address_width(ds::ExtendedContextEntry::k48Bit);
  entry.set_no_exec_enable(1);
  entry.set_write_protect_enable(1);
  entry.set_cache_disable(0);
  entry.set_extended_mem_type_enable(0);
  entry.set_domain_id(domain_id);
  entry.set_smep_enable(1);
  entry.set_extended_accessed_flag_enable(0);
  entry.set_execute_requests_enable(0);
  entry.set_second_level_execute_bit_enable(0);
  entry.set_second_level_pt_ptr(dev->second_level_pt_.phys() >> 12);

  entry.WriteTo(context_entry);

  *device = ktl::move(dev);
  return ZX_OK;
}

namespace {

uint perms_to_arch_mmu_flags(uint32_t perms) {
  uint flags = 0;
  if (perms & IOMMU_FLAG_PERM_READ) {
    flags |= ARCH_MMU_FLAG_PERM_READ;
  }
  if (perms & IOMMU_FLAG_PERM_WRITE) {
    flags |= ARCH_MMU_FLAG_PERM_WRITE;
  }
  if (perms & IOMMU_FLAG_PERM_EXECUTE) {
    flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
  }
  return flags;
}

}  // namespace

zx_status_t DeviceContext::SecondLevelMap(const fbl::RefPtr<VmObject>& vmo, uint64_t offset,
                                          size_t size, uint32_t perms, bool map_contiguous,
                                          paddr_t* virt_paddr, size_t* mapped_len) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));

  uint flags = perms_to_arch_mmu_flags(perms);

  if (vmo->LookupContiguous(offset, size, nullptr) == ZX_OK) {
    return SecondLevelMapDiscontiguous(vmo, offset, size, flags, map_contiguous, virt_paddr,
                                       mapped_len);
  }
  return SecondLevelMapContiguous(vmo, offset, size, flags, virt_paddr, mapped_len);
}

zx_status_t DeviceContext::SecondLevelMapDiscontiguous(const fbl::RefPtr<VmObject>& vmo,
                                                       uint64_t offset, size_t size, uint flags,
                                                       bool map_contiguous, paddr_t* virt_paddr,
                                                       size_t* mapped_len) {
  // If we don't need to map everything, don't try to map more than
  // the min contiguity at a time.
  const uint64_t min_contig = minimum_contiguity();
  if (!map_contiguous && size > min_contig) {
    size = min_contig;
  }

  auto lookup_fn = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
    paddr_t* paddr = static_cast<paddr_t*>(ctx);
    paddr[index] = pa;
    return ZX_OK;
  };

  RegionAllocator::Region::UPtr region;
  zx_status_t status = region_alloc_.GetRegion(size, min_contig, region);
  if (status != ZX_OK) {
    return status;
  }

  // Reserve a spot in the allocated regions list, so the extension can't fail
  // after we do the map.
  fbl::AllocChecker ac;
  allocated_regions_.reserve(allocated_regions_.size() + 1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  paddr_t base = region->base;
  size_t remaining = size;

  auto cleanup_partial = fbl::MakeAutoCall([&]() {
    size_t allocated = base - region->base;
    size_t unmapped;
    second_level_pt_.UnmapPages(base, allocated / PAGE_SIZE, &unmapped);
    DEBUG_ASSERT(unmapped == allocated / PAGE_SIZE);
  });

  while (remaining > 0) {
    const size_t kNumEntriesPerLookup = 32;
    size_t chunk_size = ktl::min(remaining, kNumEntriesPerLookup * PAGE_SIZE);
    paddr_t paddrs[kNumEntriesPerLookup] = {};
    status = vmo->Lookup(offset, chunk_size, lookup_fn, &paddrs);
    if (status != ZX_OK) {
      return status;
    }

    size_t map_len = chunk_size / PAGE_SIZE;
    size_t mapped;
    status = second_level_pt_.MapPages(base, paddrs, map_len, flags, &mapped);
    if (status != ZX_OK) {
      return status;
    }
    ASSERT(mapped == map_len);

    base += chunk_size;
    offset += chunk_size;
    remaining -= chunk_size;
  }

  cleanup_partial.cancel();

  *virt_paddr = region->base;
  *mapped_len = size;

  allocated_regions_.push_back(ktl::move(region), &ac);
  // Check shouldn't be able to fail, since we reserved the capacity already
  ASSERT(ac.check());

  LTRACEF("Map(%02x:%02x.%1x): -> [%p, %p) %#x\n", bdf_.bus(), bdf_.dev(), bdf_.func(),
          (void*)*virt_paddr, (void*)(*virt_paddr + *mapped_len), flags);
  return ZX_OK;
}

zx_status_t DeviceContext::SecondLevelMapContiguous(const fbl::RefPtr<VmObject>& vmo,
                                                    uint64_t offset, size_t size, uint flags,
                                                    paddr_t* virt_paddr, size_t* mapped_len) {
  paddr_t paddr = UINT64_MAX;
  zx_status_t status = vmo->LookupContiguous(offset, size, &paddr);
  if (status != ZX_OK) {
    return status;
  }
  DEBUG_ASSERT(paddr != UINT64_MAX);

  RegionAllocator::Region::UPtr region;
  uint64_t min_contig = minimum_contiguity();
  status = region_alloc_.GetRegion(size, min_contig, region);
  if (status != ZX_OK) {
    return status;
  }

  // Reserve a spot in the allocated regions list, so the extension can't fail
  // after we do the map.
  fbl::AllocChecker ac;
  allocated_regions_.reserve(allocated_regions_.size() + 1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t map_len = size / PAGE_SIZE;
  size_t mapped;
  status = second_level_pt_.MapPagesContiguous(region->base, paddr, map_len, flags, &mapped);
  if (status != ZX_OK) {
    return status;
  }
  ASSERT(mapped == map_len);

  *virt_paddr = region->base;
  *mapped_len = map_len * PAGE_SIZE;

  allocated_regions_.push_back(ktl::move(region), &ac);
  // Check shouldn't be able to fail, since we reserved the capacity already
  ASSERT(ac.check());

  LTRACEF("Map(%02x:%02x.%1x): [%p, %p) -> %p %#x\n", bdf_.bus(), bdf_.dev(), bdf_.func(),
          (void*)paddr, (void*)(paddr + size), (void*)paddr, flags);
  return ZX_OK;
}

zx_status_t DeviceContext::SecondLevelMapIdentity(paddr_t base, size_t size, uint32_t perms) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

  uint flags = perms_to_arch_mmu_flags(perms);

  RegionAllocator::Region::UPtr region;
  zx_status_t status = region_alloc_.GetRegion({base, size}, region);
  if (status != ZX_OK) {
    return status;
  }

  // Reserve a spot in the allocated regions list, so the extension can't fail
  // after we do the map.
  fbl::AllocChecker ac;
  allocated_regions_.reserve(allocated_regions_.size() + 1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t map_len = size / PAGE_SIZE;
  size_t mapped;
  status = second_level_pt_.MapPagesContiguous(base, base, map_len, flags, &mapped);
  if (status != ZX_OK) {
    return status;
  }
  ASSERT(mapped == map_len);

  allocated_regions_.push_back(ktl::move(region), &ac);
  ASSERT(ac.check());
  return ZX_OK;
}

zx_status_t DeviceContext::SecondLevelUnmap(paddr_t virt_paddr, size_t size) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(virt_paddr));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

  // Check if we're trying to partially unmap a region, and if so fail.
  for (size_t i = 0; i < allocated_regions_.size(); ++i) {
    const auto& region = allocated_regions_[i];

    paddr_t intersect_base;
    size_t intersect_size;
    if (!GetIntersect(virt_paddr, size, region->base, region->size, &intersect_base,
                      &intersect_size)) {
      continue;
    }

    if (intersect_base != region->base || intersect_size != region->size) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  for (size_t i = 0; i < allocated_regions_.size(); ++i) {
    const auto& region = allocated_regions_[i];
    if (region->base < virt_paddr || region->base + region->size > virt_paddr + size) {
      continue;
    }

    size_t unmapped;
    LTRACEF("Unmap(%02x:%02x.%1x): [%p, %p)\n", bdf_.bus(), bdf_.dev(), bdf_.func(),
            (void*)region->base, (void*)(region->base + region->size));
    zx_status_t status =
        second_level_pt_.UnmapPages(region->base, region->size / PAGE_SIZE, &unmapped);
    // Unmap should only be able to fail if an input was invalid
    ASSERT(status == ZX_OK);
    allocated_regions_.erase(i);
    i--;
  }

  return ZX_OK;
}

void DeviceContext::SecondLevelUnmapAllLocked() {
  while (allocated_regions_.size() > 0) {
    auto& region = allocated_regions_[allocated_regions_.size() - 1];
    zx_status_t status = SecondLevelUnmap(region->base, region->size);
    // SecondLevelUnmap only fails on invalid inputs, and our inputs would only be invalid if our
    // internals are corrupt.
    ASSERT(status == ZX_OK);
  }
}

uint64_t DeviceContext::minimum_contiguity() const {
  // TODO(teisenbe): Do not hardcode this.
  return 1ull << 20;
}

uint64_t DeviceContext::aspace_size() const {
  // TODO(teisenbe): Do not hardcode this
  // 2^48 is the size of an address space using 4-levevel translation.
  return 1ull << 48;
}

}  // namespace intel_iommu
