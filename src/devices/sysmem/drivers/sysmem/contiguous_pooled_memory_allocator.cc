// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_memory_allocator.h"

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/ddk/trace/event.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <numeric>

#include <fbl/string_printf.h>

#include "fbl/algorithm.h"
#include "lib/fidl/llcpp/arena.h"
#include "macros.h"
#include "protected_ranges.h"
#include "region-alloc/region-alloc.h"
#include "src/devices/sysmem/metrics/metrics.cb.h"

namespace sysmem_driver {

zx::duration kGuardCheckInterval = zx::sec(5);
namespace {

constexpr uint64_t kMiB = 1024ull * 1024;

fuchsia_sysmem2::wire::HeapProperties BuildHeapProperties(fidl::AnyArena& allocator,
                                                          bool is_cpu_accessible) {
  using fuchsia_sysmem2::wire::CoherencyDomainSupport;
  using fuchsia_sysmem2::wire::HeapProperties;

  auto coherency_domain_support = CoherencyDomainSupport(allocator);
  coherency_domain_support.set_cpu_supported(is_cpu_accessible);
  coherency_domain_support.set_ram_supported(is_cpu_accessible);
  coherency_domain_support.set_inaccessible_supported(true);

  auto heap_properties = HeapProperties(allocator);
  heap_properties.set_coherency_domain_support(allocator, std::move(coherency_domain_support));
  // New buffers do need to be zeroed (regardless of is_ever_cpu_accessible_ and
  // is_always_cpu_accessible_), and we want to do the zeroing in ContiguousPooledMemoryAllocator,
  // either via Zircon's zeroing of reclaimed pages, our own zeroing of just-checked pattern pages,
  // or via the TEE as necessary.  So we set need_clear true and return true from
  // is_already_cleared_on_allocate().  For secure buffers, these are always cleared via the TEE
  // even if some of the pages may have also been cleared by Zircon page reclaim, since any
  // "scramble" HW setting feature would potentially make zeroes look like non-zero to a device
  // reading the buffer.
  heap_properties.set_need_clear(true);

  // is_cpu_accessible true: We don't do (all the) flushing in this class, so caller will help with
  // that.
  //
  // is_cpu_accessible false: The only zeroing that matters re. cache flushing is the last one which
  // is done via the TEE and the TEE flushes after that zeroing.  We shouldn't flush from the REE
  // since it will/could cause HW errors.
  heap_properties.set_need_flush(is_cpu_accessible);

  return heap_properties;
}

// true - a and b have at least one page in common, and the pages in common are returned via out.
// false - a and b don't have any pages in common and out is unmodified.
bool Intersect(const ralloc_region_t& a, const ralloc_region_t& b, ralloc_region_t* out) {
  uint64_t a_base = a.base;
  uint64_t a_end = a.base + a.size;
  uint64_t b_base = b.base;
  uint64_t b_end = b.base + b.size;
  uint64_t intersected_base = std::max(a_base, b_base);
  uint64_t intersected_end = std::min(a_end, b_end);
  if (intersected_end <= intersected_base) {
    return false;
  }
  if (out) {
    out->base = intersected_base;
    out->size = intersected_end - intersected_base;
  }
  return true;
}

}  // namespace

ContiguousPooledMemoryAllocator::ContiguousPooledMemoryAllocator(
    Owner* parent_device, const char* allocation_name, inspect::Node* parent_node, uint64_t pool_id,
    uint64_t size, bool is_always_cpu_accessible, bool is_ever_cpu_accessible, bool is_ready,
    bool can_be_torn_down, async_dispatcher_t* dispatcher)
    : MemoryAllocator(
          parent_device->table_set(),
          BuildHeapProperties(parent_device->table_set().allocator(), is_always_cpu_accessible)),
      parent_device_(parent_device),
      dispatcher_(dispatcher),
      allocation_name_(allocation_name),
      pool_id_(pool_id),
      region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())),
      size_(size),
      is_always_cpu_accessible_(is_always_cpu_accessible),
      is_ever_cpu_accessible_(is_ever_cpu_accessible),
      is_ready_(is_ready),
      can_be_torn_down_(can_be_torn_down),
      metrics_(parent_device->metrics()) {
  snprintf(child_name_, sizeof(child_name_), "%s-child", allocation_name_);
  // Ensure NUL-terminated.
  child_name_[sizeof(child_name_) - 1] = 0;
  node_ = parent_node->CreateChild(allocation_name);
  node_.CreateUint("size", size, &properties_);
  node_.CreateUint("id", id(), &properties_);
  high_water_mark_property_ = node_.CreateUint("high_water_mark", 0);
  free_at_high_water_mark_property_ = node_.CreateUint("free_at_high_water_mark", size);
  used_size_property_ = node_.CreateUint("used_size", 0);
  allocations_failed_property_ = node_.CreateUint("allocations_failed", 0);
  last_allocation_failed_timestamp_ns_property_ =
      node_.CreateUint("last_allocation_failed_timestamp_ns", 0);
  commits_failed_property_ = node_.CreateUint("commits_failed", 0);
  last_commit_failed_timestamp_ns_property_ = node_.CreateUint("last_commit_failed_timstamp_ns", 0);
  allocations_failed_fragmentation_property_ =
      node_.CreateUint("allocations_failed_fragmentation", 0);
  max_free_at_high_water_property_ = node_.CreateUint("max_free_at_high_water", size);
  is_ready_property_ = node_.CreateBool("is_ready", is_ready_);
  failed_guard_region_checks_property_ =
      node_.CreateUint("failed_guard_region_checks", failed_guard_region_checks_);
  last_failed_guard_region_check_timestamp_ns_property_ =
      node_.CreateUint("last_failed_guard_region_check_timestamp_ns", 0);
  large_contiguous_region_sum_property_ = node_.CreateUint("large_contiguous_region_sum", 0);

  // CMM/PCMM properties - these values aren't quite true yet, but will be soon.
  loanable_efficiency_property_ =
      node_.CreateDouble("loanable_efficiency", is_ever_cpu_accessible_ ? 1.0 : 0.0);
  loanable_ratio_property_ =
      node_.CreateDouble("loanable_ratio", is_ever_cpu_accessible_ ? 1.0 : 0.0);
  loanable_bytes_property_ = node_.CreateUint("loanable_bytes", is_ever_cpu_accessible_ ? size : 0);
  loanable_mebibytes_property_ =
      node_.CreateUint("loanable_mebibytes", is_ever_cpu_accessible_ ? size / kMiB : 0);

  if (dispatcher) {
    zx_status_t status = zx::event::create(0, &trace_observer_event_);
    ZX_ASSERT(status == ZX_OK);
    status = trace_register_observer(trace_observer_event_.get());
    ZX_ASSERT(status == ZX_OK);
    wait_.set_object(trace_observer_event_.get());
    wait_.set_trigger(ZX_EVENT_SIGNALED);

    status = wait_.Begin(dispatcher);
    ZX_ASSERT(status == ZX_OK);
  }
}

void ContiguousPooledMemoryAllocator::InitGuardRegion(size_t guard_region_size,
                                                      bool unused_pages_guarded,
                                                      zx::duration unused_page_check_cycle_period,
                                                      bool internal_guard_regions,
                                                      bool crash_on_guard_failure,
                                                      async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(!regions_.size());
  ZX_DEBUG_ASSERT(!guard_region_size_);
  ZX_DEBUG_ASSERT(!guard_region_data_.size());
  ZX_DEBUG_ASSERT(contiguous_vmo_.get());
  ZX_DEBUG_ASSERT(!unused_pages_guarded_);
  ZX_DEBUG_ASSERT(is_ever_cpu_accessible_);
  zx_status_t status;
  uint64_t min_guard_data_size = guard_region_size;
  if (unused_pages_guarded) {
    ZX_DEBUG_ASSERT(is_ever_cpu_accessible_);
    unused_pages_guarded_ = true;
    unused_page_check_cycle_period_ = unused_page_check_cycle_period;
    ZX_DEBUG_ASSERT(mapping_);
    unused_checker_.PostDelayed(dispatcher,
                                unused_page_check_cycle_period_ / kUnusedCheckPartialCount);
    unused_recently_checker_.Post(dispatcher);
    min_guard_data_size = std::max(min_guard_data_size, unused_guard_data_size_);
    deleted_regions_.resize(kNumDeletedRegions);
  }
  ZX_DEBUG_ASSERT(guard_region_size % zx_system_get_page_size() == 0);
  ZX_DEBUG_ASSERT(min_guard_data_size % zx_system_get_page_size() == 0);
  guard_region_data_.resize(min_guard_data_size);
  for (size_t i = 0; i < min_guard_data_size; i++) {
    guard_region_data_[i] = ((i + 1) % 256);
  }
  if (!guard_region_size) {
    return;
  }
  guard_region_size_ = guard_region_size;
  // Internal guard regions expect pages to be CPU accessible always.  Internal guard regions for
  // part-time protected memory would be severely limited anyway due to granularity of protection
  // ranges and limited number of HW protection ranges.
  has_internal_guard_regions_ = internal_guard_regions && is_always_cpu_accessible_;
  guard_region_copy_.resize(guard_region_size);
  crash_on_guard_failure_ = crash_on_guard_failure;

  // Initialize external guard regions.
  ralloc_region_t regions[] = {{.base = 0, .size = guard_region_size},
                               {.base = size_ - guard_region_size, .size = guard_region_size}};

  for (auto& region : regions) {
    status = region_allocator_.SubtractRegion(region);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    status = contiguous_vmo_.write(guard_region_data_.data(), region.base, guard_region_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  status = guard_checker_.PostDelayed(dispatcher, kGuardCheckInterval);
  ZX_ASSERT(status == ZX_OK);
}

void ContiguousPooledMemoryAllocator::SetupUnusedPages() {
  ZX_DEBUG_ASSERT(is_ever_cpu_accessible_);
  ZX_DEBUG_ASSERT((is_always_cpu_accessible_ && is_ready_ && !protected_ranges_) ||
                  protected_ranges_->ranges().size() == 0);
  ZX_DEBUG_ASSERT(!is_setup_unused_pages_called_);
  is_setup_unused_pages_called_ = true;
  std::vector<ralloc_region_t> todo;
  region_allocator_.WalkAvailableRegions([&todo](const ralloc_region_t* region) {
    todo.emplace_back(*region);
    return true;
  });
  for (auto& region : todo) {
    OnRegionUnused(region);
  }
}

void ContiguousPooledMemoryAllocator::FillUnusedRangeWithGuard(uint64_t start_offset,
                                                               uint64_t size) {
  ZX_DEBUG_ASSERT(mapping_);
  ZX_DEBUG_ASSERT(start_offset % zx_system_get_page_size() == 0);
  ZX_DEBUG_ASSERT(size % zx_system_get_page_size() == 0);
  ZX_DEBUG_ASSERT(unused_guard_pattern_period_bytes_ % zx_system_get_page_size() == 0);
  uint64_t end = start_offset + size;
  uint64_t to_copy_size;
  for (uint64_t offset = start_offset; offset < end; offset += to_copy_size) {
    to_copy_size = std::min(unused_guard_data_size_, end - offset);
    memcpy(&mapping_[offset], guard_region_data_.data(), to_copy_size);
    zx_cache_flush(&mapping_[offset], to_copy_size, ZX_CACHE_FLUSH_DATA);
    // zx_cache_flush() takes care of dsb sy when __aarch64__.
  }
}

ContiguousPooledMemoryAllocator::~ContiguousPooledMemoryAllocator() {
  ZX_DEBUG_ASSERT(is_empty());
  wait_.Cancel();
  if (trace_observer_event_) {
    trace_unregister_observer(trace_observer_event_.get());
  }
  step_toward_optimal_protected_ranges_.Cancel();
  guard_checker_.Cancel();
  unused_checker_.Cancel();
  unused_recently_checker_.Cancel();
  if (mapping_) {
    zx_status_t status = zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(mapping_), size_);
    ZX_ASSERT(status == ZX_OK);
  }
}

zx_status_t ContiguousPooledMemoryAllocator::Init(uint32_t alignment_log2) {
  zx::vmo local_contiguous_vmo;
  const long system_page_alignment = __builtin_ctzl(zx_system_get_page_size());
  if (alignment_log2 < system_page_alignment) {
    alignment_log2 = system_page_alignment;
  }
  zx_status_t status = zx::vmo::create_contiguous(parent_device_->bti(), size_, alignment_log2,
                                                  &local_contiguous_vmo);
  if (status != ZX_OK) {
    LOG(ERROR, "Could not allocate contiguous memory, status %d allocation_name_: %s", status,
        allocation_name_);
    return status;
  }

  return InitCommon(std::move(local_contiguous_vmo));
}

zx_status_t ContiguousPooledMemoryAllocator::InitPhysical(zx_paddr_t paddr) {
  zx::vmo local_contiguous_vmo;
  zx_status_t status = parent_device_->CreatePhysicalVmo(paddr, size_, &local_contiguous_vmo);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to create physical VMO: %d allocation_name_: %s", status, allocation_name_);
    return status;
  }

  return InitCommon(std::move(local_contiguous_vmo));
}

zx_status_t ContiguousPooledMemoryAllocator::InitCommon(zx::vmo local_contiguous_vmo) {
  zx_status_t status = zx::vmo::create(zero_page_vmo_size_, 0, &zero_page_vmo_);
  if (status != ZX_OK) {
    LOG(ERROR, "Couldn't create the zero_page_vmo_");
    return status;
  }
  status = zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, zero_page_vmo_, 0, zero_page_vmo_size_,
                                      reinterpret_cast<zx_vaddr_t*>(&zero_page_vmo_base_));
  if (status != ZX_OK) {
    LOG(ERROR, "Unable to map zero_page_vmo_");
    return status;
  }
  // This may be unnecessary, but in case Zircon needs a hint that we really mean for this to use
  // the shared zero page...
  status = zero_page_vmo_.op_range(ZX_VMO_OP_ZERO, 0, zero_page_vmo_size_, nullptr, 0);
  if (status != ZX_OK) {
    LOG(ERROR, "Coundn't zero zero_page_vmo_");
    return status;
  }

  status =
      local_contiguous_vmo.set_property(ZX_PROP_NAME, allocation_name_, strlen(allocation_name_));
  if (status != ZX_OK) {
    LOG(ERROR, "Failed vmo.set_property(ZX_PROP_NAME, ...): %d", status);
    return status;
  }

  zx_info_vmo_t info;
  status = local_contiguous_vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed local_contiguous_vmo.get_info(ZX_INFO_VMO, ...) - status: %d", status);
    return status;
  }
  // Only secure/protected RAM ever uses a physical VMO.  Not all secure/protected RAM uses a
  // physical VMO.
  ZX_DEBUG_ASSERT(ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED ||
                  !is_ever_cpu_accessible_);
  // Paged VMOs are cached by default.  Physical VMOs are uncached by default.
  ZX_DEBUG_ASSERT((ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED) ==
                  (info.cache_policy == ZX_CACHE_POLICY_CACHED));
  // We'd have this assert, except it doesn't work with fake-bti, so for now we trust that when not
  // running a unit test, we have a VMO with info.flags & ZX_INFO_VMO_CONTIGUOUS.
  // ZX_DEBUG_ASSERT(info.flags & ZX_INFO_VMO_CONTIGUOUS);

  // Regardless of CPU or RAM domain, and regardless of contig VMO or physical VMO, if we use the
  // CPU to access the RAM, we want to use the CPU cache, if we can do so safely.
  //
  // Why we want cached when is_always_cpu_accessible_:
  //
  // Without setting cached, in addition to presumably being slower, memcpy tends to fail with
  // non-aligned access faults / syscalls that are trying to copy directly to the VMO can fail
  // without it being obvious that it's an underlying non-aligned access fault triggered by memcpy.
  //
  // Why we want uncached when !is_always_cpu_accessible_:
  //
  // If the memory is sometimes protected, we can't use the CPU cache safely, since speculative
  // prefetching may occur and interact badly (but not necessarily in immedidately-obvious ways)
  // with the HW-protected ranges (on aarch64, this causes SErrors of type DECERR).  A non-cached
  // mapping doesn't do any speculative prefetching so doesn't trigger errors as long as we don't
  // access a page while it's HW-protected.
  //
  // An alternative would be to only map pages of the VMO that are known to not be under a
  // HW-protected range while mapped, but since a non-cached mapping seems fast enough, this is
  // simpler.
  uint32_t desired_cache_policy =
      is_always_cpu_accessible_ ? ZX_CACHE_POLICY_CACHED : ZX_CACHE_POLICY_UNCACHED;
  if (info.cache_policy != desired_cache_policy) {
    status = local_contiguous_vmo.set_cache_policy(desired_cache_policy);
    if (status != ZX_OK) {
      if (ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED) {
        LOG(ERROR, "Failed to set_cache_policy() (contig paged VMO) - status: %d", status);
      } else {
        LOG(ERROR, "Failed to set_cache_policy() (not paged VMO) - status: %d", status);
      }
      return status;
    }
  }
  zx_paddr_t addrs;
  // When running a unit test, the src/devices/testing/fake-bti provides a fake zx_bti_pin() that
  // should tolerate ZX_BTI_CONTIGUOUS here despite the local_contiguous_vmo not actually having
  // info.flags ZX_INFO_VMO_CONTIGUOUS.
  status = parent_device_->bti().pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                                     local_contiguous_vmo, 0, size_, &addrs, 1, &pool_pmt_);
  if (status != ZX_OK) {
    LOG(ERROR, "Could not pin memory, status %d", status);
    return status;
  }
  phys_start_ = addrs;

  // Since the VMO is contiguous or physical, we don't need to keep the VMO pinned for it to remain
  // physically contiguous.  A physical VMO can't have any pages decommitted, while a contiguous
  // VMO can.  In order to decommit pages from a contiguous VMO, we can't have the decommitting
  // pages pinned (from user mode, ignoring any pinning internal to Zircon).
  status = pool_pmt_.unpin();
  // All possible failures are bugs in how we called zx_pmt_unpin().
  ZX_DEBUG_ASSERT(status == ZX_OK);

  // We map contiguous_vmo_ as cached only if is_always_cpu_accessible_, to avoid SError(s) that can
  // result from speculative prefetch from a physical page under a HW-protected range.  A non-cached
  // mapping prevents speculative prefetch.
  //
  // TODO(fxbug.dev/96853): Currently Zircon's physmap has !is_always_cpu_accessible_ pages mapped
  // cached, which we believe is likely the cause of some SError(s) related to
  // protected_memory_size.  One way to fix would be to change the physmap mapping to non-cached
  // when a contiguous VMO
  //
  // So far, when !is_always_cpu_accessible_, a non-cached mapping seems fast enough; we only read
  // or write using the mapping for a low % of pages.
  status = zx::vmar::root_self()->map(
      /*options=*/(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE),
      /*vmar_offset=*/0, local_contiguous_vmo, /*vmo_offset=*/0, /*len=*/size_,
      reinterpret_cast<zx_vaddr_t*>(&mapping_));
  if (status != ZX_OK) {
    LOG(ERROR, "mapping contiguous_vmo_ failed - fatal - status: %d", status);
    return status;
  }

  contiguous_vmo_ = std::move(local_contiguous_vmo);
  can_decommit_ = (ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED);

  ralloc_region_t region = {0, size_};
  region_allocator_.AddRegion(region);
  return ZX_OK;
}

zx_status_t ContiguousPooledMemoryAllocator::Allocate(uint64_t size,
                                                      std::optional<std::string> name,
                                                      zx::vmo* parent_vmo) {
  if (!is_ready_) {
    LOG(ERROR, "allocation_name_: %s is not ready_, failing", allocation_name_);
    return ZX_ERR_BAD_STATE;
  }
  RegionAllocator::Region::UPtr region;
  zx::vmo result_parent_vmo;

  const uint64_t guard_region_size = has_internal_guard_regions_ ? guard_region_size_ : 0;
  uint64_t allocation_size = size + guard_region_size_ * 2;
  // TODO(fxbug.dev/43184): Use a fragmentation-reducing allocator (such as best fit).
  //
  // The "region" param is an out ref.
  zx_status_t status =
      region_allocator_.GetRegion(allocation_size, zx_system_get_page_size(), region);
  if (status != ZX_OK) {
    LOG(WARNING, "GetRegion failed (out of space?) - size: %zu status: %d", size, status);
    DumpPoolStats();
    allocations_failed_property_.Add(1);
    last_allocation_failed_timestamp_ns_property_.Set(zx::clock::get_monotonic().get());
    uint64_t unused_size = 0;
    region_allocator_.WalkAvailableRegions([&unused_size](const ralloc_region_t* r) -> bool {
      unused_size += r->size;
      return true;
    });
    if (unused_size >= size) {
      // There's enough unused memory total, so the allocation must have failed due to
      // fragmentation.
      allocations_failed_fragmentation_property_.Add(1);
    }
    return status;
  }

  // We rely on this commit not destroying existing unused region guard pages (since we never
  // decommitted those), and not touching any protected pages (since those aren't decommitted).
  // This commit will commit the gaps between guard pages, if any of those pages are decommitted
  // currently.  These gaps are what we may have previously decommitted if the pages weren't
  // protected.  In contrast to decommitting, when we commit we don't need to separately commit only
  // the gaps, since a commit range that also overlaps the unused range guard pages doesn't change
  // the contents of the already-committed guard pages, and doesn't touch any already-committed
  // protected pages.  This CommitRegion() can also overlap (spatially not temporally) with a
  // (possibly-larger) CommitRegion() requested by protected_ranges_ via UseRange() (if we're using
  // protected_ranges_).
  status = CommitRegion(*region.get());
  if (status != ZX_OK) {
    LOG(WARNING, "CommitRegion() failed (OOM?) - size: %zu status %d", size, status);
    commits_failed_property_.Add(1);
    last_commit_failed_timestamp_ns_property_.Set(zx::clock::get_monotonic().get());
    return status;
  }

  // If !is_always_cpu_accessible_, no point in doing any zeroing other than the zeroing later via
  // the TEE once the region is fully protected.  This is because zeroing via memset() before the
  // range is protected isn't really necessarily making the protected range appear to be zero to
  // protected mode devices that read the just-protected range, due to any potential HW "scramble".
  CheckUnusedRange(region->base, region->size, /*and_also_zero=*/is_always_cpu_accessible_);

  ZX_DEBUG_ASSERT(!!protected_ranges_ == (!is_always_cpu_accessible_ && is_ever_cpu_accessible_));
  if (!is_always_cpu_accessible_) {
    const auto new_range = protected_ranges::Range::BeginLength(region->base, region->size);
    if (protected_ranges_) {
      ZX_DEBUG_ASSERT(protected_ranges_);
      protected_ranges_->AddRange(new_range);
      EnsureSteppingTowardOptimalProtectedRanges();
    } else {
      ZX_DEBUG_ASSERT(!is_ever_cpu_accessible_);
      // The covering range is VDEC (or similar), which is not an explicitly-created range, but
      // rather an implicit range.  In some cases this range may still be checked against by layers
      // above the TEE, but it's not a range that was created via Range
      //
      // If we're running with new FW, IsDynamic() is true.  If we're not, then we can't call
      // ZeroProtectedSubRange() because the FW doesn't have it, in which case we can't zero the
      // protected buffer.
      if (protected_ranges_control_->IsDynamic()) {
        protected_ranges_control_->ZeroProtectedSubRange(false, new_range);
      }
    }
  }

  TracePoolSize(false);

  // We don't attempt to have guard regions on either side of a !is_cpu_accessible_ buffer (aka
  // "internal" guard regions), since a guard page could already be under a protected range and
  // since deprotecting a page is expected to change its contents (not necessarily to zero, but
  // change the contents to ensure that no protected data can be read / un-scrambled).
  ZX_DEBUG_ASSERT(is_always_cpu_accessible_ || !guard_region_size);
  if (guard_region_size) {
    status = contiguous_vmo_.write(guard_region_data_.data(), region->base, guard_region_size_);
    if (status != ZX_OK) {
      LOG(ERROR, "Failed to write pre-guard region.");
      return status;
    }
    status = contiguous_vmo_.write(guard_region_data_.data(),
                                   region->base + guard_region_size + size, guard_region_size_);
    if (status != ZX_OK) {
      LOG(ERROR, "Failed to write post-guard region.");
      return status;
    }
  }

  // The result_parent_vmo created here is a VMO window to a sub-region of contiguous_vmo_.
  status = contiguous_vmo_.create_child(ZX_VMO_CHILD_SLICE, region->base + guard_region_size, size,
                                        &result_parent_vmo);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed vmo.create_child(ZX_VMO_CHILD_SLICE, ...): %d", status);
    return status;
  }

  // If you see a Sysmem*-child VMO you should know that it doesn't actually
  // take up any space, because the same memory is backed by contiguous_vmo_.
  status = result_parent_vmo.set_property(ZX_PROP_NAME, child_name_, strlen(child_name_));
  if (status != ZX_OK) {
    LOG(ERROR, "Failed vmo.set_property(ZX_PROP_NAME, ...): %d", status);
    return status;
  }

  if (!name) {
    name = "Unknown";
  }

  RegionData data;
  data.name = std::move(*name);
  zx_info_handle_basic_t handle_info;
  status = result_parent_vmo.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info),
                                      nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  data.node = node_.CreateChild(fbl::StringPrintf("vmo-%ld", handle_info.koid).c_str());
  data.size_property = data.node.CreateUint("size", size);
  data.koid = handle_info.koid;
  data.koid_property = data.node.CreateUint("koid", handle_info.koid);
  allocated_bytes_ += region->size;
  data.ptr = std::move(region);
  regions_.emplace(std::make_pair(result_parent_vmo.get(), std::move(data)));

  UpdateLoanableMetrics();
  LOG(DEBUG, "Allocate() - loaned ratio: %g loaning efficiency: %g", GetLoanableRatio(),
      GetLoanableEfficiency());

  *parent_vmo = std::move(result_parent_vmo);
  return ZX_OK;
}

zx_status_t ContiguousPooledMemoryAllocator::SetupChildVmo(
    const zx::vmo& parent_vmo, const zx::vmo& child_vmo,
    fuchsia_sysmem2::wire::SingleBufferSettings buffer_settings) {
  // nothing to do here
  return ZX_OK;
}

void ContiguousPooledMemoryAllocator::Delete(zx::vmo parent_vmo) {
  TRACE_DURATION("gfx", "ContiguousPooledMemoryAllocator::Delete");
  auto it = regions_.find(parent_vmo.get());
  ZX_ASSERT(it != regions_.end());
  auto& region_data = it->second;
  CheckGuardRegionData(region_data);
  StashDeletedRegion(region_data);
  ZX_DEBUG_ASSERT(!!protected_ranges_ == (!is_always_cpu_accessible_ && is_ever_cpu_accessible_));
  if (protected_ranges_) {
    const auto& region = *region_data.ptr;
    const auto old_range = protected_ranges::Range::BeginLength(region.base, region.size);
    protected_ranges_->DeleteRange(old_range);
    EnsureSteppingTowardOptimalProtectedRanges();
  } else {
    OnRegionUnused(*it->second.ptr.get());
  }
  allocated_bytes_ -= region_data.ptr->size;
  regions_.erase(it);
  // region_data now invalid
  parent_vmo.reset();
  TracePoolSize(false);

  UpdateLoanableMetrics();
  LOG(DEBUG, "Delete() - loaned ratio: %g loaning efficiency: %g", GetLoanableRatio(),
      GetLoanableEfficiency());

  if (is_empty()) {
    parent_device_->CheckForUnbind();
  }
}

void ContiguousPooledMemoryAllocator::set_ready() {
  if (!is_always_cpu_accessible_) {
    protected_ranges_control_.emplace(this);
    if (is_ever_cpu_accessible_) {
      protected_ranges_.emplace(&*protected_ranges_control_);
      SetupUnusedPages();
    }
  }
  is_ready_ = true;
  is_ready_property_.Set(is_ready_);
}

void ContiguousPooledMemoryAllocator::CheckGuardRegion(const char* region_name, size_t region_size,
                                                       bool pre, uint64_t start_offset) {
  const uint64_t guard_region_size = guard_region_size_;

  zx_status_t status = contiguous_vmo_.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, start_offset,
                                                guard_region_size, nullptr, 0);
  ZX_ASSERT(status == ZX_OK);
  status = contiguous_vmo_.read(guard_region_copy_.data(), start_offset, guard_region_copy_.size());
  ZX_ASSERT(status == ZX_OK);
  if (memcmp(guard_region_copy_.data(), guard_region_data_.data(), guard_region_size_) != 0) {
    size_t error_start = UINT64_MAX;
    size_t error_end = 0;
    for (size_t i = 0; i < guard_region_copy_.size(); i++) {
      if (guard_region_copy_[i] != guard_region_data_[i]) {
        error_start = std::min(i, error_start);
        error_end = std::max(i, error_end);
      }
    }
    ZX_DEBUG_ASSERT(error_start < guard_region_copy_.size());

    std::string bad_str;
    std::string good_str;
    constexpr uint32_t kRegionSizeToOutput = 16;
    for (uint32_t i = error_start; i < error_start + kRegionSizeToOutput && i < guard_region_size_;
         i++) {
      bad_str += fbl::StringPrintf(" 0x%x", guard_region_copy_[i]).c_str();
      good_str += fbl::StringPrintf(" 0x%x", guard_region_data_[i]).c_str();
    }

    LOG(ERROR,
        "Region %s of size %ld has corruption in %s guard pages between [0x%lx, 0x%lx] - good "
        "\"%s\" bad \"%s\"",
        region_name, region_size, pre ? "pre" : "post", error_start, error_end, good_str.c_str(),
        bad_str.c_str());

    // For now, if unused page checking is enabled, also print the guard region diffs using
    // ReportPatternCheckFailedRange().  While this is mainly intended for printing diffs after
    // pattern check failure on unused pages (in contrast to per-allocation or per-reserved-VMO
    // guard pages), we _might_ find the DeletedRegion info useful, and the diffs may have more
    // info.
    //
    // TODO(dustingreen): In a later CL, integrate anything that's needed from the code above into
    // ReportPatternCheckFailedRange(), and make ReportPatternCheckFailedRange() work even if unused
    // page checking is disabled.
    uint64_t page_aligned_base = fbl::round_down(error_start, zx_system_get_page_size());
    uint64_t page_aligned_end = fbl::round_up(error_end + 1, zx_system_get_page_size());
    ralloc_region_t diff_range{.base = page_aligned_base,
                               .size = page_aligned_end - page_aligned_base};
    ReportPatternCheckFailedRange(diff_range, "guard");

    IncrementGuardRegionFailureInspectData();
  }
}

void ContiguousPooledMemoryAllocator::IncrementGuardRegionFailureInspectData() {
  ZX_ASSERT_MSG(!crash_on_guard_failure_, "Crashing due to guard region failure");
  failed_guard_region_checks_++;
  failed_guard_region_checks_property_.Set(failed_guard_region_checks_);
  last_failed_guard_region_check_timestamp_ns_property_.Set(zx::clock::get_monotonic().get());
}

void ContiguousPooledMemoryAllocator::CheckGuardRegionData(const RegionData& region) {
  const uint64_t guard_region_size = guard_region_size_;
  if (guard_region_size == 0 || !has_internal_guard_regions_)
    return;
  uint64_t size = region.ptr->size;
  uint64_t vmo_size = size - guard_region_size * 2;
  ZX_DEBUG_ASSERT(guard_region_size_ == guard_region_copy_.size());
  for (uint32_t i = 0; i < 2; i++) {
    uint64_t start_offset = region.ptr->base;
    if (i == 1) {
      // Size includes guard regions.
      start_offset += size - guard_region_size;
    }
    CheckGuardRegion(region.name.c_str(), vmo_size, (i == 0), start_offset);
  }
}

void ContiguousPooledMemoryAllocator::CheckExternalGuardRegions() {
  size_t guard_region_size = guard_region_size_;
  if (!guard_region_size)
    return;
  ralloc_region_t regions[] = {{.base = 0, .size = guard_region_size},
                               {.base = size_ - guard_region_size, .size = guard_region_size}};
  for (size_t i = 0; i < std::size(regions); i++) {
    auto& region = regions[i];
    ZX_DEBUG_ASSERT(i < 2);
    ZX_DEBUG_ASSERT(region.size == guard_region_size_);
    CheckGuardRegion("External", 0, (i == 0), region.base);
  }
}

bool ContiguousPooledMemoryAllocator::is_ready() { return is_ready_; }

void ContiguousPooledMemoryAllocator::TraceObserverCallback(async_dispatcher_t* dispatcher,
                                                            async::WaitBase* wait,
                                                            zx_status_t status,
                                                            const zx_packet_signal_t* signal) {
  if (status != ZX_OK)
    return;
  trace_observer_event_.signal(ZX_EVENT_SIGNALED, 0);
  // We don't care if tracing was enabled or disabled - if the category is now disabled, the trace
  // will just be ignored anyway.
  TracePoolSize(true);

  trace_notify_observer_updated(trace_observer_event_.get());
  wait_.Begin(dispatcher);
}

void ContiguousPooledMemoryAllocator::CheckGuardPageCallback(async_dispatcher_t* dispatcher,
                                                             async::TaskBase* task,
                                                             zx_status_t status) {
  if (status != ZX_OK)
    return;
  // Ignore status - if the post fails, that means the driver is being shut down.
  guard_checker_.PostDelayed(dispatcher, kGuardCheckInterval);

  CheckExternalGuardRegions();

  if (!has_internal_guard_regions_)
    return;

  for (auto& region_data : regions_) {
    auto& region = region_data.second;
    CheckGuardRegionData(region);
  }
}

void ContiguousPooledMemoryAllocator::CheckUnusedPagesCallback(async_dispatcher_t* dispatcher,
                                                               async::TaskBase* task,
                                                               zx_status_t status) {
  if (status != ZX_OK) {
    return;
  }
  uint64_t page_size = zx_system_get_page_size();
  uint64_t start =
      fbl::round_down(unused_check_phase_ * size_ / kUnusedCheckPartialCount, page_size);
  uint64_t end =
      fbl::round_down((unused_check_phase_ + 1) * size_ / kUnusedCheckPartialCount, page_size);
  CheckAnyUnusedPages(start, end);
  unused_check_phase_ = (unused_check_phase_ + 1) % kUnusedCheckPartialCount;
  // Ignore status - if the post fails, that means the driver is being shut down.
  unused_checker_.PostDelayed(dispatcher,
                              unused_page_check_cycle_period_ / kUnusedCheckPartialCount);
}

void ContiguousPooledMemoryAllocator::CheckUnusedRecentlyPagesCallback(
    async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status) {
  if (status != ZX_OK) {
    return;
  }
  zx::time now_ish = zx::clock::get_monotonic();
  int32_t deleted_region_index = deleted_regions_next_ - 1;
  for (int32_t i = 0; i < deleted_regions_count_; ++i) {
    if (deleted_region_index == -1) {
      deleted_region_index = kNumDeletedRegions - 1;
    }
    const DeletedRegion& deleted_region = deleted_regions_[deleted_region_index];
    if (now_ish - deleted_region.when_freed > kUnusedRecentlyAgeThreshold) {
      break;
    }
    CheckAnyUnusedPages(deleted_region.region.base,
                        deleted_region.region.base + deleted_region.region.size);
    --deleted_region_index;
  }
  unused_recently_checker_.PostDelayed(dispatcher, kUnusedRecentlyPageCheckPeriod);
}

void ContiguousPooledMemoryAllocator::EnsureSteppingTowardOptimalProtectedRanges() {
  step_toward_optimal_protected_ranges_min_time_ =
      zx::clock::get_monotonic() + kStepTowardOptimalProtectedRangesPeriod;
  zx_status_t post_status = step_toward_optimal_protected_ranges_.PostForTime(
      dispatcher_, step_toward_optimal_protected_ranges_min_time_);
  ZX_ASSERT(post_status == ZX_OK || post_status == ZX_ERR_ALREADY_EXISTS);
}

void ContiguousPooledMemoryAllocator::StepTowardOptimalProtectedRanges(
    async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status) {
  if (status != ZX_OK) {
    return;
  }
  zx::time now = zx::clock::get_monotonic();
  if (now >= step_toward_optimal_protected_ranges_min_time_) {
    bool done = protected_ranges_->StepTowardOptimalRanges();
    UpdateLoanableMetrics();
    if (done) {
      LOG(INFO,
          "StepTowardOptimalProtectedRanges() - done: %d loaned ratio: %g loaning efficiency: %g",
          done, GetLoanableRatio(), GetLoanableEfficiency());
      return;
    } else {
      LOG(DEBUG,
          "StepTowardOptimalProtectedRanges() - done: %d loaned ratio: %g loaning efficiency: %g",
          done, GetLoanableRatio(), GetLoanableEfficiency());
    }
    step_toward_optimal_protected_ranges_min_time_ = now + kStepTowardOptimalProtectedRangesPeriod;
  }
  ZX_DEBUG_ASSERT(!step_toward_optimal_protected_ranges_.is_pending());
  step_toward_optimal_protected_ranges_.PostForTime(dispatcher,
                                                    step_toward_optimal_protected_ranges_min_time_);
}

void ContiguousPooledMemoryAllocator::DumpRanges() const {
  if (protected_ranges_->ranges().empty()) {
    return;
  }
  LOG(INFO, "ContiguousPooledMemoryAllocator::DumpRanges() ###############");
  for (const auto& iter : protected_ranges_->ranges()) {
    LOG(INFO, "DumpRanges() - begin: 0x%" PRIx64 " length: 0x%" PRIx64 " end: 0x%" PRIx64,
        iter.begin(), iter.length(), iter.end());
  }
}

void ContiguousPooledMemoryAllocator::CheckAnyUnusedPages(uint64_t start_offset,
                                                          uint64_t end_offset) {
  // This is a list of non-zero-size portions of unused regions within [start_offset, end_offset).
  std::vector<ralloc_region_t> todo;

  auto process_unused_region = [&todo, start_offset, end_offset](const ralloc_region_t& range) {
    ralloc_region_t r = range;
    if (r.base + r.size <= start_offset) {
      return true;
    }
    if (r.base >= end_offset) {
      return true;
    }
    ZX_DEBUG_ASSERT((r.base < end_offset) && (r.base + r.size > start_offset));

    // make r be the intersection of r and [start, end)
    if (r.base + r.size > end_offset) {
      r.size = end_offset - r.base;
    }
    if (r.base < start_offset) {
      uint64_t delta = start_offset - r.base;
      r.base += delta;
      r.size -= delta;
    }

    todo.push_back(r);
    return true;
  };

  if (!protected_ranges_) {
    // Without protected_ranges_, the unused ranges (in this context, that are check-able) are just
    // the raw unused ranges from region_allocator_.
    region_allocator_.WalkAvailableRegions([&process_unused_region](const ralloc_region_t* region) {
      // struct copy
      ralloc_region_t r = *region;
      return process_unused_region(r);
    });
  } else {
    // With protected_ranges_, the unused ranges that are check-able are only the gaps in between
    // the protected ranges, as we can't check pages that are protected even if they're not in use
    // by an allocated VMO.
    //
    // Any range that is not protected by protected_ranges_ is also not used according to
    // region_allocator_.  Some ranges which are protected by protected_ranges_ are not used
    // according to region_allocator_, but we can't check those unused pages here.
    protected_ranges_->ForUnprotectedRanges(
        [&process_unused_region](const protected_ranges::Range& range) {
          ralloc_region_t r{.base = range.begin(), .size = range.length()};
          return process_unused_region(r);
        });
  }

  for (auto& r : todo) {
    CheckUnusedRange(r.base, r.size, /*and_also_zero=*/false);
  }
}

void ContiguousPooledMemoryAllocator::StashDeletedRegion(const RegionData& region_data) {
  if (deleted_regions_.size() != kNumDeletedRegions) {
    return;
  }
  // Remember basic info regarding up to kNumDeletedRegions regions, for potential reporting of
  // pattern check failures later.
  deleted_regions_[deleted_regions_next_] =
      DeletedRegion{.region = {.base = region_data.ptr->base, .size = region_data.ptr->size},
                    .when_freed = zx::clock::get_monotonic(),
                    .name = region_data.name};
  ++deleted_regions_next_;
  ZX_DEBUG_ASSERT(deleted_regions_next_ <= kNumDeletedRegions);
  if (deleted_regions_next_ == kNumDeletedRegions) {
    deleted_regions_next_ = 0;
  }
  if (deleted_regions_count_ < kNumDeletedRegions) {
    ++deleted_regions_count_;
  }
}

// The data structure for old regions is optimized for limiting the overall size and limting the
// cost of upkeep of the old region info.  It's not optimized for lookup; this lookup can be a bit
// slow, but _should_ never need to happen...
ContiguousPooledMemoryAllocator::DeletedRegion*
ContiguousPooledMemoryAllocator::FindMostRecentDeletedRegion(uint64_t offset) {
  uint64_t page_size = zx_system_get_page_size();
  DeletedRegion* deleted_region = nullptr;
  int32_t index = deleted_regions_next_ - 1;
  int32_t count = 0;
  ralloc_region_t offset_page{.base = static_cast<uint64_t>(offset),
                              .size = static_cast<uint64_t>(page_size)};
  while (count < deleted_regions_count_) {
    if (index < 0) {
      index = kNumDeletedRegions - 1;
    }
    if (Intersect(offset_page, deleted_regions_[index].region, nullptr)) {
      deleted_region = &deleted_regions_[index];
      break;
    }
    --index;
  }
  return deleted_region;
}

void ContiguousPooledMemoryAllocator::ReportPatternCheckFailedRange(
    const ralloc_region_t& failed_range, const char* which_type) {
  if (!unused_pages_guarded_) {
    // for now
    //
    // TODO(dustingreen): Remove this restriction.
    LOG(ERROR, "!unused_pages_guarded_ so ReportPatternCheckFailedRange() returning early");
    return;
  }
  ZX_ASSERT(deleted_regions_.size() == kNumDeletedRegions);
  ZX_ASSERT(failed_range.base % zx_system_get_page_size() == 0);
  ZX_ASSERT(failed_range.size % zx_system_get_page_size() == 0);

  LOG(ERROR,
      "########################### SYSMEM DETECTS BAD DMA WRITE (%s) - paddr range start: "
      "0x%" PRIx64 " size: 0x%" PRIx64 " (internal offset: 0x%" PRIx64 ")",
      which_type, phys_start_ + failed_range.base, failed_range.size, failed_range.base);

  std::optional<DeletedRegion*> prev_deleted_region;
  bool skipped_since_last = false;
  LOG(ERROR,
      "DeletedRegion info for failed range expanded by 1 page on either side (... - omitted "
      "entries are same DeletedRegion info):");
  int32_t page_size = zx_system_get_page_size();
  auto handle_skip_since_last = [&skipped_since_last] {
    if (!skipped_since_last) {
      return;
    }
    LOG(ERROR, "...");
    skipped_since_last = false;
  };
  for (int64_t offset = static_cast<int64_t>(failed_range.base) - page_size;
       offset < static_cast<int64_t>(failed_range.base + failed_range.size) + page_size;
       offset += page_size) {
    ZX_ASSERT(offset >= -page_size);
    if (offset == -page_size) {
      LOG(ERROR, "offset -page_size (out of bounds)");
      continue;
    }
    ZX_ASSERT(offset <= static_cast<int64_t>(size_));
    if (offset == static_cast<int64_t>(size_)) {
      LOG(ERROR, "offset == size_ (out of bounds)");
      continue;
    }
    DeletedRegion* deleted_region = FindMostRecentDeletedRegion(offset);
    // This can sometimes be comparing nullptr and nullptr, or nullptr and not nullptr, and that's
    // fine/expected.
    if (prev_deleted_region && deleted_region == prev_deleted_region.value()) {
      skipped_since_last = true;
      continue;
    }
    prev_deleted_region.emplace(deleted_region);
    handle_skip_since_last();
    if (deleted_region) {
      zx::time now = zx::clock::get_monotonic();
      zx::duration deleted_ago = now - deleted_region->when_freed;
      uint64_t deleted_micros_ago = deleted_ago.to_usecs();
      LOG(ERROR,
          "paddr: 0x%" PRIx64 " previous region: 0x%p - paddr base: 0x%" PRIx64
          " reserved-relative offset: 0x%" PRIx64 " size: 0x%" PRIx64 " freed micros ago: %" PRIu64
          " name: %s",
          phys_start_ + offset, deleted_region, phys_start_ + deleted_region->region.base,
          deleted_region->region.base, deleted_region->region.size, deleted_micros_ago,
          deleted_region->name.c_str());
    } else {
      LOG(ERROR, "paddr: 0x%" PRIx64 " no previous region found within history window",
          phys_start_ + offset);
    }
  }
  // Indicate that the same deleted region was repeated more times at the end, as appropriate.
  handle_skip_since_last();
  LOG(ERROR, "END DeletedRangeInfo");

  LOG(ERROR, "Data not matching pattern (... - no diffs, ...... - skipping middle even if diffs):");
  constexpr uint32_t kBytesPerLine = 32;
  ZX_ASSERT(zx_system_get_page_size() % kBytesPerLine == 0);
  // 2 per byte for hex digits + '!' or '=', not counting terminating \000
  constexpr uint32_t kCharsPerByte = 3;
  constexpr uint32_t kMaxStrLen = kCharsPerByte * kBytesPerLine;
  char str[kMaxStrLen + 1];
  constexpr uint32_t kMaxDiffBytes = 1024;
  static_assert((kMaxDiffBytes / 2) % kBytesPerLine == 0);
  uint32_t diff_bytes = 0;
  static_assert(kMaxDiffBytes % 2 == 0);
  ZX_ASSERT(failed_range.size % kBytesPerLine == 0);
  for (uint64_t offset = failed_range.base; offset < failed_range.base + failed_range.size;
       offset += kBytesPerLine) {
    if (failed_range.size > kMaxDiffBytes && diff_bytes >= kMaxDiffBytes / 2) {
      // skip past the middle to keep total diff bytes <= kMaxDiffBytes
      offset = failed_range.base + failed_range.size - kMaxDiffBytes / 2;
      // indicate per-line skips as appropriate
      handle_skip_since_last();
      LOG(ERROR, "......");
      // The part near the end of the failed_range won't satisfy the enclosing if's condition due to
      // starting kMaxDiffBytes / 2 from the end, so the enclosing loop will stop first.
      diff_bytes = 0;
    }
    bool match = !memcmp(&mapping_[offset], &guard_region_data_[offset % page_size], kBytesPerLine);
    if (!match) {
      handle_skip_since_last();
      LOG(ERROR, "paddr: 0x%" PRIx64 " offset 0x%" PRIx64, phys_start_ + offset, offset);
      for (uint32_t i = 0; i < kBytesPerLine; ++i) {
        bool byte_match = mapping_[offset + i] == guard_region_data_[(offset + i) % page_size];
        // printing 2 hex characters + 1 indicator char + \000
        int result = sprintf(&str[i * kCharsPerByte], "%02x%s", mapping_[offset + i],
                             byte_match ? "=" : "!");
        ZX_DEBUG_ASSERT(result == kCharsPerByte);
      }
      diff_bytes += kBytesPerLine;
      LOG(ERROR, "%s", str);
    } else {
      skipped_since_last = true;
    }
  }
  // Indicate no diffs at end, as appropriate.
  handle_skip_since_last();
  LOG(ERROR, "END data not matching pattern");
}

void ContiguousPooledMemoryAllocator::CheckUnusedRange(uint64_t offset, uint64_t size,
                                                       bool and_also_zero) {
  ZX_DEBUG_ASSERT(mapping_);
  uint32_t succeeded_count = 0;
  uint32_t failed_count = 0;
  uint32_t page_size = zx_system_get_page_size();
  zx_cache_flush(&mapping_[offset], size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  auto nop_loan_range = [and_also_zero](const ralloc_region_t& range) {
    if (!and_also_zero) {
      return;
    }
#if DEBUG_ASSERT_IMPLEMENTED
    // All loan ranges were already zeroed by Zircon, either via decommit or ZX_VMO_OP_ZERO if
    // decommit failed.  No need to zero again.
    uint64_t end = range.base + range.size;
    uint64_t todo_size;
    for (uint64_t iter = range.base; iter != end; iter += todo_size) {
      todo_size = std::min(end - iter, zero_page_vmo_size_);
      ZX_DEBUG_ASSERT(!memcmp(&mapping_[iter], zero_page_vmo_base_, todo_size));
    }
#endif
  };
  auto maybe_zero_range = [this, and_also_zero](const ralloc_region_t& range) {
    if (!and_also_zero) {
      return;
    }
    // We don't have to cache flush here because the logical_buffer_collection.cc caller does that.
    memset(&mapping_[range.base], 0x00, range.size);
  };
  const ralloc_region_t unused_range{.base = offset, .size = size};
  ForUnusedGuardPatternRanges(
      unused_range,
      /*pattern_func=*/
      [this, page_size, and_also_zero, &succeeded_count,
       &failed_count](const ralloc_region_t& range) {
        std::optional<ralloc_region_t> zero_range;
        auto handle_zero_range = [this, &zero_range, and_also_zero] {
          if (!and_also_zero || !zero_range) {
            return;
          }
          // We don't have to cache flush here because the logical_buffer_collection.cc caller does
          // that.
          memset(&mapping_[zero_range->base], 0x00, zero_range->size);
          zero_range.reset();
        };
        auto ensure_handle_zero_range = fit::defer([&handle_zero_range] { handle_zero_range(); });

        std::optional<ralloc_region_t> failed_range;
        auto handle_failed_range = [this, &failed_range, and_also_zero] {
          if (!failed_range) {
            return;
          }
          ReportPatternCheckFailedRange(failed_range.value(), "unused");
          IncrementGuardRegionFailureInspectData();
          failed_range.reset();
          // So we don't keep finding the same corruption over and over.
          if (!and_also_zero) {
            FillUnusedRangeWithGuard(failed_range->base, failed_range->size);
          }
        };
        auto ensure_handle_failed_range =
            fit::defer([&handle_failed_range] { handle_failed_range(); });

        uint64_t end = range.base + range.size;
        uint64_t todo_size;
        for (uint64_t iter = range.base; iter != end; iter += todo_size) {
          todo_size = std::min(unused_guard_data_size_, end - iter);
          ZX_DEBUG_ASSERT(todo_size % page_size == 0);
          if (unlikely(memcmp(guard_region_data_.data(), &mapping_[iter], todo_size))) {
            if (!failed_range) {
              failed_range.emplace(ralloc_region_t{.base = iter, .size = 0});
            }
            failed_range->size += todo_size;
            ++failed_count;
          } else {
            // if any failed range is active, it's ending here, so report it
            handle_failed_range();
            ++succeeded_count;
          }
          // We memset() incrementally for better cache locality (vs. forwarding to
          // maybe_zero_range to zero the whole incoming range).  However, if we have a failed
          // pattern check range in progress, we don't immediately zero because in that case we
          // need to print diffs first.  This is somewhat more complicated than just checking a big
          // range then zeroing a big range, but this should also be quite a bit faster by staying
          // in cache until we're done reading and writing the data.
          if (and_also_zero) {
            if (!zero_range) {
              zero_range.emplace(ralloc_region_t{.base = iter, .size = 0});
            }
            zero_range->size += todo_size;
            if (!failed_range) {
              // Zero immediately if we don't need to keep the data around for failed_range reasons.
              handle_zero_range();
            }
          }
        }
        // ~ensure_handle_failed_range
        // ~ensure_handle_zero_range
      },
      /*loan_func=*/nop_loan_range,
      /*zero_func=*/maybe_zero_range);
  metrics_.LogUnusedPageCheckCounts(succeeded_count, failed_count);
}

uint64_t ContiguousPooledMemoryAllocator::CalculateLargeContiguousRegionSize() {
  constexpr uint32_t kRegionTrackerCount = 10;

  std::array<uint64_t, kRegionTrackerCount> largest_regions{};
  // All elements are identical, so largest_regions is already a heap.
  region_allocator_.WalkAvailableRegions([&](const ralloc_region_t* r) -> bool {
    if (r->size > largest_regions[0]) {
      // Pop the smallest element.
      std::pop_heap(largest_regions.begin(), largest_regions.end(), std::greater<uint64_t>());
      // Push the region size onto the heap.
      largest_regions[kRegionTrackerCount - 1] = r->size;
      std::push_heap(largest_regions.begin(), largest_regions.end(), std::greater<uint64_t>());
    }
    return true;
  });

  uint64_t top_region_sum = std::accumulate(largest_regions.begin(), largest_regions.end(), 0);
  return top_region_sum;
}

void ContiguousPooledMemoryAllocator::DumpPoolStats() {
  uint64_t unused_size = 0;
  uint64_t max_free_size = 0;
  region_allocator_.WalkAvailableRegions(
      [&unused_size, &max_free_size](const ralloc_region_t* r) -> bool {
        unused_size += r->size;
        max_free_size = std::max(max_free_size, r->size);
        return true;
      });

  uint64_t top_region_sum = CalculateLargeContiguousRegionSize();

  LOG(INFO,
      "%s unused total: %ld bytes, max free size %ld bytes "
      "AllocatedRegionCount(): %zu AvailableRegionCount(): %zu, largest 10 regions %zu",
      allocation_name_, unused_size, max_free_size, region_allocator_.AllocatedRegionCount(),
      region_allocator_.AvailableRegionCount(), top_region_sum);
  for (auto& [vmo, region] : regions_) {
    LOG(INFO, "Region koid %ld name %s size %zu", region.koid, region.name.c_str(),
        region.ptr->size);
  }
}

void ContiguousPooledMemoryAllocator::DumpPoolHighWaterMark() {
  LOG(INFO, "%s high_water_mark_used_size_: %ld bytes, max_free_size_at_high_water_mark_ %ld bytes",
      allocation_name_, high_water_mark_used_size_, max_free_size_at_high_water_mark_);
}

void ContiguousPooledMemoryAllocator::TracePoolSize(bool initial_trace) {
  uint64_t used_size = 0;
  region_allocator_.WalkAllocatedRegions([&used_size](const ralloc_region_t* r) -> bool {
    used_size += r->size;
    return true;
  });
  used_size_property_.Set(used_size);
  large_contiguous_region_sum_property_.Set(CalculateLargeContiguousRegionSize());
  TRACE_COUNTER("gfx", "Contiguous pool size", pool_id_, "size", used_size);
  bool trace_high_water_mark = initial_trace;
  if (used_size > high_water_mark_used_size_) {
    high_water_mark_used_size_ = used_size;
    trace_high_water_mark = true;
    high_water_mark_property_.Set(high_water_mark_used_size_);
    free_at_high_water_mark_property_.Set(size_ - high_water_mark_used_size_);
    uint64_t max_free_size = 0;
    region_allocator_.WalkAvailableRegions([&max_free_size](const ralloc_region_t* r) -> bool {
      max_free_size = std::max(max_free_size, r->size);
      return true;
    });
    max_free_size_at_high_water_mark_ = max_free_size;
    max_free_at_high_water_property_.Set(max_free_size_at_high_water_mark_);
    // This can be a bit noisy at first, but then settles down quickly.
    DumpPoolHighWaterMark();
  }
  if (trace_high_water_mark) {
    TRACE_INSTANT("gfx", "Increased high water mark", TRACE_SCOPE_THREAD, "allocation_name",
                  allocation_name_, "size", high_water_mark_used_size_);
  }
}

void ContiguousPooledMemoryAllocator::UpdateLoanableMetrics() {
  double efficiency = GetLoanableEfficiency();
  if (efficiency < min_efficiency_) {
    min_efficiency_ = efficiency;
  }
  loanable_efficiency_property_.Set(efficiency);
  loanable_ratio_property_.Set(GetLoanableRatio());
  uint64_t loanable_bytes = GetLoanableBytes();
  loanable_bytes_property_.Set(loanable_bytes);
  loanable_mebibytes_property_.Set(loanable_bytes / kMiB);
}

uint64_t ContiguousPooledMemoryAllocator::GetVmoRegionOffsetForTest(const zx::vmo& vmo) {
  return regions_[vmo.get()].ptr->base + guard_region_size_;
}

bool ContiguousPooledMemoryAllocator::is_already_cleared_on_allocate() {
  // This is accurate for CPU-accessible, non-VDEC part-time protected, and VDEC full-time
  // protected.
  //
  // We zero these ways:
  //   * Zircon zeroing reclaimed pages
  //   * zeroing just-checked pattern pages
  //   * using the TEE to zero as appropriate
  return true;
}

template <typename F1, typename F2, typename F3>
void ContiguousPooledMemoryAllocator::ForUnusedGuardPatternRanges(const ralloc_region_t& region,
                                                                  F1 pattern_func, F2 loan_func,
                                                                  F3 zero_func) {
  if (!protected_ranges_) {
    ForUnusedGuardPatternRangesInternal(region, pattern_func, loan_func, zero_func);
  } else {
    const protected_ranges::Range unused_range =
        protected_ranges::Range::BeginLength(region.base, region.size);
    protected_ranges_->ForUnprotectedRangesOverlappingRange(
        unused_range,
        /*unprotected_range=*/[this, &pattern_func, &loan_func,
                               &zero_func](const protected_ranges::Range& unprotected_range) {
          // The extent of unprotected_range is already clamped to only include pages that are also
          // in unused_range.
          ralloc_region_t region{.base = unprotected_range.begin(),
                                 .size = unprotected_range.length()};
          ForUnusedGuardPatternRangesInternal(region, pattern_func, loan_func, zero_func);
        });
  }
}

template <typename F1, typename F2, typename F3>
void ContiguousPooledMemoryAllocator::ForUnusedGuardPatternRangesInternal(
    const ralloc_region_t& region, F1 pattern_func, F2 loan_func, F3 zero_func) {
  if (!can_decommit_ && !unused_pages_guarded_) {
    zero_func(region);
    return;
  }
  if (!can_decommit_) {
    pattern_func(region);
    return;
  }
  if (!unused_pages_guarded_) {
    loan_func(region);
    return;
  }
  // We already know that the passed-in region doesn't overlap with any used region.  It may be
  // adjacent to another unused range.
  uint64_t region_base = region.base;
  uint64_t region_end = region.base + region.size;
  ZX_DEBUG_ASSERT(region_end > region_base);
  // The "meta pattern" is just a page aligned to unused_guard_pattern_period_ that's kept for
  // DMA-write-after-free detection purposes, followed by the rest of unused_guard_pattern_period_
  // that's loaned.  The meta pattern repeats through the whole offset space from 0 to size_, but
  // only applies to portions of the space which are not currently used.
  uint64_t meta_pattern_start = fbl::round_down(region_base, unused_guard_pattern_period_bytes_);
  uint64_t meta_pattern_end = fbl::round_up(region_end, unused_guard_pattern_period_bytes_);
  for (uint64_t meta_pattern_base = meta_pattern_start; meta_pattern_base < meta_pattern_end;
       meta_pattern_base += unused_guard_pattern_period_bytes_) {
    ralloc_region_t raw_keep{
        .base = meta_pattern_base,
        .size = unused_to_pattern_bytes_,
    };
    ralloc_region_t keep;
    if (Intersect(raw_keep, region, &keep)) {
      pattern_func(keep);
    }

    ralloc_region_t raw_loan{
        .base = raw_keep.base + raw_keep.size,
        .size = unused_guard_pattern_period_bytes_ - unused_to_pattern_bytes_,
    };
    ralloc_region_t loan;
    if (Intersect(raw_loan, region, &loan)) {
      loan_func(loan);
    }
  }
}

void ContiguousPooledMemoryAllocator::OnRegionUnused(const ralloc_region_t& region) {
  ForUnusedGuardPatternRanges(
      region,
      /*pattern_func=*/
      [this](const ralloc_region_t& pattern_range) {
        ZX_DEBUG_ASSERT(unused_pages_guarded_);
        FillUnusedRangeWithGuard(pattern_range.base, pattern_range.size);
      },
      /*loan_func=*/
      [this](const ralloc_region_t& loan_range) {
        ZX_DEBUG_ASSERT(can_decommit_);
        zx_status_t zero_status = ZX_OK;
        zx_status_t decommit_status = contiguous_vmo_.op_range(ZX_VMO_OP_DECOMMIT, loan_range.base,
                                                               loan_range.size, nullptr, 0);
        if (decommit_status != ZX_OK) {
          // sysmem only calls the current method on one thread
          static zx::time next_log_time = zx::time::infinite_past();
          zx::time now = zx::clock::get_monotonic();
          if (now >= next_log_time) {
            LOG(INFO,
                "(log rate limited) ZX_VMO_OP_DECOMMIT failed on contiguous VMO - decommit_status: "
                "%d base: 0x%" PRIx64 " size: 0x%" PRIx64 " pool_id_: 0x%" PRIx64,
                decommit_status, loan_range.base, loan_range.size, pool_id_);
            next_log_time = now + zx::sec(30);
          }
          // If we can't decommit (unexpected), we try to zero before giving up.  Overall, we rely
          // on all loan_range(s) to be zeroed by a decommit/commit to be able to skip zeroing of
          // previously loaned ranges, so we need to zero here to make it look as if the decommit
          // worked from a zeroing point of view.  If we also can't zero, we assert.  The decommit
          // is not expected to fail unless decommit of contiguous VMO pages is disabled via kernel
          // command line flag.  The zero op is never expected to fail.
          zero_status = contiguous_vmo_.op_range(ZX_VMO_OP_ZERO, loan_range.base, loan_range.size,
                                                 nullptr, 0);
          // We don't expect DECOMMIT or ZERO to ever fail.
          ZX_ASSERT_MSG(zero_status == ZX_OK,
                        "ZX_VMO_OP_DECOMMIT and ZX_VMO_OP_ZERO both failed - zero_status: %d\n",
                        zero_status);
        }
      },
      /*zero_func=*/
      [this](const ralloc_region_t& zero_range) {
        ZX_DEBUG_ASSERT(!can_decommit_);
        ZX_DEBUG_ASSERT(!unused_pages_guarded_);
        // We don't actually need to zero here since this is during delete.  We zero during allocate
        // instead.
      });
}

zx_status_t ContiguousPooledMemoryAllocator::CommitRegion(const ralloc_region_t& region) {
  if (!can_decommit_) {
    return ZX_OK;
  }
  zx_status_t status =
      contiguous_vmo_.op_range(ZX_VMO_OP_COMMIT, region.base, region.size, nullptr, 0);
  return status;
}

// loanable pages / un-used pages
double ContiguousPooledMemoryAllocator::GetLoanableEfficiency() {
  if (protected_ranges_) {
    return protected_ranges_->GetEfficiency();
  } else {
    if (is_ever_cpu_accessible_) {
      return 1.0;
    } else {
      return 0.0;
    }
  }
}

// loanable pages / total pages
double ContiguousPooledMemoryAllocator::GetLoanableRatio() {
  if (protected_ranges_) {
    return protected_ranges_->GetLoanableRatio();
  } else {
    if (is_ever_cpu_accessible_) {
      uint64_t loanable_bytes = size_ - allocated_bytes_;
      return static_cast<double>(loanable_bytes) / static_cast<double>(size_);
    } else {
      return 0.0;
    }
  }
}

uint64_t ContiguousPooledMemoryAllocator::GetLoanableBytes() {
  if (protected_ranges_) {
    return protected_ranges_->GetLoanableBytes();
  } else {
    if (is_ever_cpu_accessible_) {
      return size_ - allocated_bytes_;
    } else {
      return 0;
    }
  }
}

bool ContiguousPooledMemoryAllocator::RangesControl::IsDynamic() {
  return parent_->parent_device_->protected_ranges_core_control(parent_->heap_type()).IsDynamic();
}

uint64_t ContiguousPooledMemoryAllocator::RangesControl::MaxRangeCount() {
  return parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .MaxRangeCount();
}

uint64_t ContiguousPooledMemoryAllocator::RangesControl::GetRangeGranularity() {
  return parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .GetRangeGranularity();
}

bool ContiguousPooledMemoryAllocator::RangesControl::HasModProtectedRange() {
  return parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .HasModProtectedRange();
}

void ContiguousPooledMemoryAllocator::RangesControl::AddProtectedRange(
    const protected_ranges::Range& zero_based_range) {
  // We pin/unpin in AddProtectedRange() / DelProtectedRange() instead of UseRange()/UnUseRange(),
  // because UnUseRange() isn't guaranteed to line up with any previous UseRange(), while the former
  // is required to specify specific tracked ranges.
  //
  // The point of pinning is entirely about preventing Zircon from potentially trying to use
  // HW-protected pages between when sysmem hypothetically crashes and when that sysmem crash
  // triggers a hard reboot.
  //
  // TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
  zx::pmt pmt;
  zx_paddr_t paddr;
  zx_status_t pin_result = parent_->parent_device_->bti().pin(
      ZX_BTI_PERM_READ | ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, parent_->contiguous_vmo_,
      zero_based_range.begin(), zero_based_range.length(), &paddr, 1, &pmt);
  // If sysmem can't pin an already-committed range, do a hard reboot so things work again.  We do
  // not ZX_ASSERT() if UseRange()'s commit fails; that can fail cleanly, but once the pages are
  // committed we expect pin to work here since pages don't need to be allocated by this pin.  This
  // is because Zircon doesn't implicitly decommit pages from contiguous VMOs (and is unlikely to
  // in future given how currently-present pages of contiguous VMOs tend to get pinned again fairly
  // soon anyway, else why did they need to be contiguous).  But if this changes, we'll see this
  // ZX_ASSERT() fire and we'll need to accomodate the possibility of pin failing.
  ZX_ASSERT(pin_result == ZX_OK);
  zero_based_range.SetMutablePmt(std::move(pmt));

  const auto range = protected_ranges::Range::BeginLength(
      parent_->phys_start_ + zero_based_range.begin(), zero_based_range.length());
  parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .AddProtectedRange(range);
}

void ContiguousPooledMemoryAllocator::RangesControl::DelProtectedRange(
    const protected_ranges::Range& zero_based_range) {
  const auto range = protected_ranges::Range::BeginLength(
      parent_->phys_start_ + zero_based_range.begin(), zero_based_range.length());
  parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .DelProtectedRange(range);

  // The pin_count will prevent actual un-pinning for any page that's still covered by a different
  // pin.
  zx::pmt pmt = zero_based_range.TakeMutablePmt();
  zx_status_t unpin_status = pmt.unpin();
  ZX_ASSERT(unpin_status == ZX_OK);
}

void ContiguousPooledMemoryAllocator::RangesControl::ModProtectedRange(
    const protected_ranges::Range& old_zero_based_range,
    const protected_ranges::Range& new_zero_based_range) {
  // pin new
  zx::pmt pmt;
  zx_paddr_t paddr;
  zx_status_t pin_result = parent_->parent_device_->bti().pin(
      ZX_BTI_PERM_READ | ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, parent_->contiguous_vmo_,
      new_zero_based_range.begin(), new_zero_based_range.length(), &paddr, 1, &pmt);
  // If sysmem can't pin an already-committed range, do a hard reboot so things work again.  We do
  // not ZX_ASSERT() if UseRange()'s commit fails; that can fail cleanly, but once the pages are
  // committed we expect pin to work here since pages don't need to be allocated by this pin.  This
  // is because Zircon doesn't implicitly decommit pages from contiguous VMOs (and is unlikely to
  // in future given how currently-present pages of contiguous VMOs tend to get pinned again fairly
  // soon anyway, else why did they need to be contiguous).  But if this changes, we'll see this
  // ZX_ASSERT() fire and we'll need to accomodate the possibility of pin failing.
  ZX_ASSERT(pin_result == ZX_OK);
  new_zero_based_range.SetMutablePmt(std::move(pmt));

  const auto old_range = protected_ranges::Range::BeginLength(
      parent_->phys_start_ + old_zero_based_range.begin(), old_zero_based_range.length());
  const auto new_range = protected_ranges::Range::BeginLength(
      parent_->phys_start_ + new_zero_based_range.begin(), new_zero_based_range.length());
  parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .ModProtectedRange(old_range, new_range);

  // unpin old
  //
  // The pin_count will prevent actual un-pinning for any page that's still covered by a different
  // pin.
  pmt = old_zero_based_range.TakeMutablePmt();
  zx_status_t unpin_status = pmt.unpin();
  ZX_ASSERT(unpin_status == ZX_OK);
}

void ContiguousPooledMemoryAllocator::RangesControl::ZeroProtectedSubRange(
    bool is_covering_range_explicit, const protected_ranges::Range& zero_based_range) {
  const auto range = protected_ranges::Range::BeginLength(
      parent_->phys_start_ + zero_based_range.begin(), zero_based_range.length());
  parent_->parent_device_->protected_ranges_core_control(parent_->heap_type())
      .ZeroProtectedSubRange(is_covering_range_explicit, range);
}

uint64_t ContiguousPooledMemoryAllocator::RangesControl::GetBase() { return 0; }

uint64_t ContiguousPooledMemoryAllocator::RangesControl::GetSize() { return parent_->size_; }

bool ContiguousPooledMemoryAllocator::RangesControl::UseRange(
    const protected_ranges::Range& range) {
  ralloc_region_t region{.base = range.begin(), .size = range.length()};
  bool result = (ZX_OK == parent_->CommitRegion(region));
  return result;
}

void ContiguousPooledMemoryAllocator::RangesControl::UnUseRange(
    const protected_ranges::Range& range) {
  ralloc_region_t region{.base = range.begin(), .size = range.length()};
  parent_->OnRegionUnused(region);
}

}  // namespace sysmem_driver
