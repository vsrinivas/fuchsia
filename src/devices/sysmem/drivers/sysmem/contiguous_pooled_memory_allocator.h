// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_

#include <lib/async/wait.h>
#include <lib/zx/bti.h>
#include <lib/zx/event.h>
#include <zircon/limits.h>

#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "allocator.h"

namespace sysmem_driver {

class ContiguousPooledMemoryAllocator : public MemoryAllocator {
 public:
  ContiguousPooledMemoryAllocator(Owner* parent_device, const char* allocation_name,
                                  uint64_t pool_id, uint64_t size, bool is_cpu_accessible,
                                  bool is_ready, async_dispatcher_t* dispatcher = nullptr);

  ~ContiguousPooledMemoryAllocator();

  // Default to page alignment.
  zx_status_t Init(uint32_t alignment_log2 = ZX_PAGE_SHIFT);

  // TODO(MTWN-329): Use this for VDEC.
  //
  // This uses a physical VMO as the parent VMO.
  zx_status_t InitPhysical(zx_paddr_t paddr);

  zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                       zx::vmo* parent_vmo) override;
  zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) override;
  void Delete(zx::vmo parent_vmo) override;

  bool CoherencyDomainIsInaccessible() override { return !is_cpu_accessible_; }

  zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) override {
    *base = start_;
    *size = size_;
    return ZX_OK;
  }

  void set_ready() override;
  bool is_ready() override;

  const zx::vmo& GetPoolVmoForTest() { return contiguous_vmo_; }

 private:
  zx_status_t InitCommon(zx::vmo local_contiguous_vmo);
  void TraceObserverCallback(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal);
  void DumpPoolStats();
  void TracePoolSize();
  Owner* const parent_device_{};
  const char* const allocation_name_{};
  const uint64_t pool_id_{};
  char child_name_[ZX_MAX_NAME_LEN] = {};
  zx::vmo contiguous_vmo_;
  RegionAllocator region_allocator_;
  // From parent_vmo handle to std::unique_ptr<>
  std::map<zx_handle_t, std::pair<std::string /*name*/, RegionAllocator::Region::UPtr>> regions_;
  uint64_t start_{};
  uint64_t size_{};
  bool is_cpu_accessible_{};
  bool is_ready_{};

  zx::event trace_observer_event_;
  async::WaitMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::TraceObserverCallback>
      wait_{this};
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_
