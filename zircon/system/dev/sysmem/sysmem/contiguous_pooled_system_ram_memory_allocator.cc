// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_system_ram_memory_allocator.h"

#include "macros.h"

ContiguousPooledSystemRamMemoryAllocator::ContiguousPooledSystemRamMemoryAllocator(
    Owner* parent_device, const char* allocation_name, uint64_t size,
    bool is_cpu_accessible)
    : parent_device_(parent_device),
      allocation_name_(allocation_name),
      region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())),
      size_(size),
      is_cpu_accessible_(is_cpu_accessible) {}

zx_status_t ContiguousPooledSystemRamMemoryAllocator::Init(uint32_t alignment_log2) {
  zx_status_t status =
      zx::vmo::create_contiguous(parent_device_->bti(), size_, alignment_log2, &contiguous_vmo_);
  if (status != ZX_OK) {
    DRIVER_ERROR("Could allocate contiguous memory, status %d\n", status);
    return status;
  }
  contiguous_vmo_.set_property(ZX_PROP_NAME, allocation_name_, strlen(allocation_name_));

  // TODO(ZX-4807): Ideally we'd set ZX_CACHE_POLICY_UNCACHED when !is_cpu_accessible_, since
  // IIUC on aarch64 it's possible for a cached mapping to secure/protected memory + speculative
  // execution to cause random faults, while an uncached mapping only faults if the uncached mapping
  // is actually touched.  However, currently for a VMO created with zx::vmo::create_contiguous(),
  // the .set_cache_policy() doesn't work because the VMO already has pages.  For now use this
  // private member var since we're very likely to need it again.
  (void)is_cpu_accessible_;

  zx_paddr_t addrs;
  zx::pmt pmt;
  status = parent_device_->bti().pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                                     contiguous_vmo_, 0, size_, &addrs, 1, &pmt);
  if (status != ZX_OK) {
    DRIVER_ERROR("Could not pin memory, status %d\n", status);
    return status;
  }

  start_ = addrs;
  ralloc_region_t region = {0, size_};
  region_allocator_.AddRegion(region);
  return ZX_OK;
}

zx_status_t ContiguousPooledSystemRamMemoryAllocator::Allocate(uint64_t size, zx::vmo* parent_vmo) {
  RegionAllocator::Region::UPtr region;
  zx::vmo result_parent_vmo;

  // TODO: Use a fragmentation-reducing allocator (such as best fit).
  //
  // The "region" param is an out ref.
  zx_status_t status = region_allocator_.GetRegion(size, ZX_PAGE_SIZE, region);
  if (status != ZX_OK) {
    DRIVER_INFO("GetRegion failed (out of space?) - size: %zu status: %d\n", size, status);
    DumpPoolStats();
    return status;
  }

  // The result_parent_vmo created here is a VMO window to a sub-region of contiguous_vmo_.
  status = contiguous_vmo_.create_child(ZX_VMO_CHILD_SLICE, region->base, size, &result_parent_vmo);
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed vmo.create_child(ZX_VMO_CHILD_SLICE, ...): %d\n", status);
    return status;
  }

  // If you see a sysmem-contig VMO you should know that it doesn't actually
  // take up any space, because the same memory is backed by contiguous_vmo_.
  const char* kSysmemContig = "sysmem-contig";
  status = result_parent_vmo.set_property(ZX_PROP_NAME, kSysmemContig, strlen(kSysmemContig));
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed vmo.set_property(ZX_PROP_NAME, ...): %d\n", status);
    return status;
  }

  regions_.emplace(std::make_pair(result_parent_vmo.get(), std::move(region)));
  *parent_vmo = std::move(result_parent_vmo);
  return ZX_OK;
}

zx_status_t ContiguousPooledSystemRamMemoryAllocator::SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) {
  // nothing to do here
  return ZX_OK;
}

void ContiguousPooledSystemRamMemoryAllocator::Delete(zx::vmo parent_vmo) {
  auto it = regions_.find(parent_vmo.get());
  ZX_ASSERT(it != regions_.end());
  regions_.erase(it);
  // ~parent_vmo
}

void ContiguousPooledSystemRamMemoryAllocator::DumpPoolStats() {
  uint64_t unused_size = 0;
  uint64_t max_free_size = 0;
  region_allocator_.WalkAvailableRegions(
      [&unused_size, &max_free_size](const ralloc_region_t* r) -> bool {
        unused_size += r->size;
        max_free_size = std::max(max_free_size, r->size);
        return true;
      });

  DRIVER_ERROR("Contiguous pool unused total: %ld bytes, max free size %ld bytes "
               "AllocatedRegionCount(): %zu AvailableRegionCount(): %zu\n",
               unused_size, max_free_size,
               region_allocator_.AllocatedRegionCount(), region_allocator_.AvailableRegionCount());
}
