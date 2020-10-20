// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_memory_allocator.h"

#include <fuchsia/sysmem2/llcpp/fidl.h>

#include <ddk/trace/event.h>
#include <fbl/string_printf.h>

#include "macros.h"

namespace sysmem_driver {

namespace {

llcpp::fuchsia::sysmem2::HeapProperties BuildHeapProperties(bool is_cpu_accessible) {
  using llcpp::fuchsia::sysmem2::CoherencyDomainSupport;
  using llcpp::fuchsia::sysmem2::HeapProperties;

  auto coherency_domain_support = std::make_unique<CoherencyDomainSupport>();
  *coherency_domain_support =
      CoherencyDomainSupport::Builder(std::make_unique<CoherencyDomainSupport::Frame>())
          .set_cpu_supported(std::make_unique<bool>(is_cpu_accessible))
          .set_ram_supported(std::make_unique<bool>(is_cpu_accessible))
          .set_inaccessible_supported(std::make_unique<bool>(true))
          .build();

  return HeapProperties::Builder(std::make_unique<HeapProperties::Frame>())
      .set_coherency_domain_support(std::move(coherency_domain_support))
      // Contiguous non-protected VMOs need to be cleared.
      .set_need_clear(std::make_unique<bool>(is_cpu_accessible))
      .build();
}

}  // namespace

ContiguousPooledMemoryAllocator::ContiguousPooledMemoryAllocator(
    Owner* parent_device, const char* allocation_name, inspect::Node* parent_node, uint64_t pool_id,
    uint64_t size, bool is_cpu_accessible, bool is_ready, async_dispatcher_t* dispatcher)
    : MemoryAllocator(BuildHeapProperties(is_cpu_accessible)),
      parent_device_(parent_device),
      allocation_name_(allocation_name),
      pool_id_(pool_id),
      region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())),
      size_(size),
      is_cpu_accessible_(is_cpu_accessible),
      is_ready_(is_ready) {
  snprintf(child_name_, sizeof(child_name_), "%s-child", allocation_name_);
  // Ensure NUL-terminated.
  child_name_[sizeof(child_name_) - 1] = 0;
  node_ = parent_node->CreateChild(allocation_name);
  size_property_ = node_.CreateUint("size", size);
  high_water_mark_property_ = node_.CreateUint("high_water_mark", 0);
  used_size_property_ = node_.CreateUint("used_size", 0);
  allocations_failed_property_ = node_.CreateUint("allocations_failed", 0);
  allocations_failed_fragmentation_property_ =
      node_.CreateUint("allocations_failed_fragmentation", 0);
  max_free_at_high_water_property_ = node_.CreateUint("max_free_at_high_water", size);

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

ContiguousPooledMemoryAllocator::~ContiguousPooledMemoryAllocator() {
  wait_.Cancel();
  if (trace_observer_event_) {
    trace_unregister_observer(trace_observer_event_.get());
  }
}

zx_status_t ContiguousPooledMemoryAllocator::Init(uint32_t alignment_log2) {
  zx::vmo local_contiguous_vmo;
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
    LOG(ERROR, "Failed to create physical VMO: %d allocation_name_: %s", status,
                 allocation_name_);
    return status;
  }

  return InitCommon(std::move(local_contiguous_vmo));
}

zx_status_t ContiguousPooledMemoryAllocator::InitCommon(zx::vmo local_contiguous_vmo) {
  zx_status_t status =
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
  ZX_DEBUG_ASSERT(ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED || !is_cpu_accessible_);
  // Paged VMOs are cached by default.  Physical VMOs are uncached by default.
  ZX_DEBUG_ASSERT((ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED) ==
                  (info.cache_policy == ZX_CACHE_POLICY_CACHED));
  // We'd have this assert, except it doesn't work with fake-bti, so for now we trust that when not
  // running a unit test, we have a VMO with info.flags & ZX_INFO_VMO_CONTIGUOUS.
  // ZX_DEBUG_ASSERT(info.flags & ZX_INFO_VMO_CONTIGUOUS);

  // Regardless of CPU or RAM domain, and regardless of contig VMO or physical VMO, if we use the
  // CPU to access the RAM, we want to use the CPU cache.  If we can't use the CPU to access the RAM
  // (on REE side), then we don't want to use the CPU cache.
  //
  // Why we want cached when is_cpu_accessible_:
  //
  // Without setting cached, in addition to presumably being slower, memcpy tends to fail with
  // non-aligned access faults / syscalls that are trying to copy directly to the VMO can fail
  // without it being obvious that it's an underlying non-aligned access fault triggered by memcpy.
  //
  // Why we want uncached when !is_cpu_accessible_:
  //
  // IIUC, it's possible on aarch64 for a cached mapping to protected memory + speculative execution
  // to cause random faults, while a non-cached mapping only faults if a non-cached mapping is
  // actually touched.
  uint32_t desired_cache_policy =
      is_cpu_accessible_ ? ZX_CACHE_POLICY_CACHED : ZX_CACHE_POLICY_UNCACHED;
  if (info.cache_policy != desired_cache_policy) {
    status = local_contiguous_vmo.set_cache_policy(desired_cache_policy);
    if (status != ZX_OK) {
      // TODO(fxbug.dev/34580): Ideally we'd set ZX_CACHE_POLICY_UNCACHED when !is_cpu_accessible_,
      // since IIUC on aarch64 it's possible for a cached mapping to secure/protected memory +
      // speculative execution to cause random faults, while an uncached mapping only faults if the
      // uncached mapping is actually touched.  However, currently for a VMO created with
      // zx::vmo::create_contiguous(), the .set_cache_policy() doesn't work because the VMO already
      // has pages.  Cases where !is_cpu_accessible_ include both Init() and InitPhysical(), so we
      // can't rely on local_contiguous_vmo being a physical VMO.
      if (ZX_INFO_VMO_TYPE(info.flags) == ZX_INFO_VMO_TYPE_PAGED) {
        LOG(ERROR,
            "Ignoring failure to set_cache_policy() on contig VMO - see fxbug.dev/34580 - status: "
            "%d",
            status);
        status = ZX_OK;
        goto keepGoing;
      }
      LOG(ERROR, "Failed to set_cache_policy(): %d", status);
      return status;
    }
  }
keepGoing:;

  zx_paddr_t addrs;
  zx::pmt pmt;
  // When running a unit test, the src/devices/testing/fake-bti provides a fake zx_bti_pin() that
  // should tolerate ZX_BTI_CONTIGUOUS here despite the local_contiguous_vmo not actually having
  // info.flags ZX_INFO_VMO_CONTIGUOUS.
  status = parent_device_->bti().pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                                     local_contiguous_vmo, 0, size_, &addrs, 1, &pmt);
  if (status != ZX_OK) {
    LOG(ERROR, "Could not pin memory, status %d", status);
    return status;
  }

  start_ = addrs;
  contiguous_vmo_ = std::move(local_contiguous_vmo);
  ralloc_region_t region = {0, size_};
  region_allocator_.AddRegion(region);
  // It is intentional here that ~pmt doesn't imply zx_pmt_unpin().  If sysmem dies, we'll reboot.
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

  // TODO(fxbug.dev/43184): Use a fragmentation-reducing allocator (such as best fit).
  //
  // The "region" param is an out ref.
  zx_status_t status = region_allocator_.GetRegion(size, ZX_PAGE_SIZE, region);
  if (status != ZX_OK) {
    LOG(WARNING, "GetRegion failed (out of space?) - size: %zu status: %d", size, status);
    DumpPoolStats();
    allocations_failed_property_.Add(1);
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

  TracePoolSize(false);

  // The result_parent_vmo created here is a VMO window to a sub-region of contiguous_vmo_.
  status = contiguous_vmo_.create_child(ZX_VMO_CHILD_SLICE, region->base, size, &result_parent_vmo);
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
  data.ptr = std::move(region);
  regions_.emplace(std::make_pair(result_parent_vmo.get(), std::move(data)));

  *parent_vmo = std::move(result_parent_vmo);
  return ZX_OK;
}

zx_status_t ContiguousPooledMemoryAllocator::SetupChildVmo(
    const zx::vmo& parent_vmo, const zx::vmo& child_vmo,
    llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings) {
  // nothing to do here
  return ZX_OK;
}

void ContiguousPooledMemoryAllocator::Delete(zx::vmo parent_vmo) {
  TRACE_DURATION("gfx", "ContiguousPooledMemoryAllocator::Delete");
  auto it = regions_.find(parent_vmo.get());
  ZX_ASSERT(it != regions_.end());
  regions_.erase(it);
  parent_vmo.reset();
  TracePoolSize(false);
}

void ContiguousPooledMemoryAllocator::set_ready() { is_ready_ = true; }

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

void ContiguousPooledMemoryAllocator::DumpPoolStats() {
  uint64_t unused_size = 0;
  uint64_t max_free_size = 0;
  region_allocator_.WalkAvailableRegions(
      [&unused_size, &max_free_size](const ralloc_region_t* r) -> bool {
        unused_size += r->size;
        max_free_size = std::max(max_free_size, r->size);
        return true;
      });

  LOG(INFO,
      "%s unused total: %ld bytes, max free size %ld bytes "
      "AllocatedRegionCount(): %zu AvailableRegionCount(): %zu",
      allocation_name_, unused_size, max_free_size, region_allocator_.AllocatedRegionCount(),
      region_allocator_.AvailableRegionCount());
  for (auto& [vmo, region] : regions_) {
    LOG(INFO, "Region koid %ld name %s size %zu", region.koid, region.name.c_str(),
                 region.ptr->size);
  }
}

void ContiguousPooledMemoryAllocator::DumpPoolHighWaterMark() {
  LOG(INFO,
      "%s high_water_mark_used_size_: %ld bytes, max_free_size_at_high_water_mark_ %ld bytes "
      "(not including any failed allocations)",
      allocation_name_, high_water_mark_used_size_, max_free_size_at_high_water_mark_);
}

void ContiguousPooledMemoryAllocator::TracePoolSize(bool initial_trace) {
  uint64_t used_size = 0;
  region_allocator_.WalkAllocatedRegions([&used_size](const ralloc_region_t* r) -> bool {
    used_size += r->size;
    return true;
  });
  used_size_property_.Set(used_size);
  TRACE_COUNTER("gfx", "Contiguous pool size", pool_id_, "size", used_size);
  bool trace_high_water_mark = initial_trace;
  if (used_size > high_water_mark_used_size_) {
    high_water_mark_used_size_ = used_size;
    trace_high_water_mark = true;
    high_water_mark_property_.Set(high_water_mark_used_size_);
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

}  // namespace sysmem_driver
