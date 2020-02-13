// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_SHARED_MEMORY_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_SHARED_MEMORY_H_

#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>

#include <climits>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <fbl/intrusive_double_list.h>
#include <region-alloc/region-alloc.h>

namespace optee {

// OP-TEE Shared Memory Management
//
// Inter world memory is provided by the Secure OS. During driver bind, the OpteeController will
// query OP-TEE to discover the physical start address and size of the memory to be used for inter
// world communication. It can then create a SharedMemoryManager to manage that address space.
//
// The SharedMemoryManager will divide the shared address space into two pools: driver and client.
// The driver pool is for the allocation of driver messages, such as an OP-TEE message for opening
// a session. The driver messages are used entirely in-proc and do not require a VMO object for
// lifetime management. The client pool is for usage by client apps, which requires VMO objects for
// sharing between processes. As such, the client pool objects must all be page aligned. The
// benefits of splitting these different memory usages into distinct pools include preventing the
// client app allocations from starving the driver message usages and grouping similarly aligned
// objects together to reduce pool fragmentation.
//
// The SharedMemoryPool uses the region-alloc library to divide the provided address space into
// allocations for use. It provides region objects that will return to the allocator upon
// destruction. There's also a template trait class parameter that can be used to provide different
// traits for the different pools. This has the added benefit of creating distinct types for the
// driver and client pools, so we can restrict which messages can be allocated from which pool. For
// example, an open session message must be constructed from the driver pool.
//
// The SharedMemory object is essentially just a wrapper around the region object that was allocated
// by the SharedMemoryPool. The region object represents the offset and size within the memory pool
// that is allocated to this object. It is important to note that the destructor for the RegionPtr
// will recycle the region back to the RegionAllocator, eliminating the need for us to explicitly
// free it.
//

class SharedMemoryView;

class SharedMemoryRangeTraits {
 public:
  constexpr zx_vaddr_t vaddr() const { return vaddr_; }
  constexpr zx_paddr_t paddr() const { return paddr_; }
  constexpr size_t size() const { return size_; }

  constexpr bool ContainsVaddr(zx_vaddr_t vaddr) const {
    return vaddr >= this->vaddr() && vaddr < this->vaddr() + size();
  }

  constexpr bool ContainsPaddr(zx_paddr_t paddr) const {
    return paddr >= this->paddr() && paddr < this->paddr() + size();
  }

  // Gets a subslice of the memory spanning a specified range of virtual addresses.
  //
  // Parameters:
  //  * start: The starting virtual address of the subslice (inclusive).
  //  * end:   The ending virtual address of the subslice (exclusive).
  //
  // Returns:
  //  * If the given range is valid, an initialized `std::optional` containing the
  //    `SharedMemoryView` subslice.
  //  * Otherwise, an uninitialized `std::optional`.
  std::optional<SharedMemoryView> SliceByVaddr(zx_vaddr_t start, zx_vaddr_t end) const;

  // Gets a subslice of the memory spanning a specified range of physical addresses.
  //
  // Parameters:
  //  * start: The starting physical address of the subslice (inclusive).
  //  * end:   The ending physical address of the subslice (exclusive).
  //
  // Returns:
  //  * If the given range is valid, an initialized `std::optional` containing the
  //    `SharedMemoryView` subslice.
  //  * Otherwise, an uninitialized `std::optional`.
  std::optional<SharedMemoryView> SliceByPaddr(zx_paddr_t start, zx_paddr_t end) const;

 protected:
  constexpr explicit SharedMemoryRangeTraits(zx_vaddr_t vaddr, zx_paddr_t paddr, size_t size)
      : vaddr_(vaddr), paddr_(paddr), size_(size) {}

  zx_vaddr_t vaddr_;
  zx_paddr_t paddr_;
  size_t size_;
};

class SharedMemory : public SharedMemoryRangeTraits,
                     public fbl::DoublyLinkedListable<std::unique_ptr<SharedMemory>> {
 public:
  using RegionPtr = RegionAllocator::Region::UPtr;

  explicit SharedMemory(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr, RegionPtr region)
      : SharedMemoryRangeTraits(base_vaddr + region->base, base_paddr + region->base, region->size),
        region_(std::move(region)) {}

  // Move only type
  SharedMemory(SharedMemory&&) = default;
  SharedMemory& operator=(SharedMemory&&) = default;

 private:
  // Upon destruction of the SharedMemory object, the RegionPtr will be recycled back to the
  // RegionAllocator by it's destructor.
  RegionPtr region_;
};

// SharedMemoryView
//
// A non-owning view of a slice of `SharedMemory`.
class SharedMemoryView : public SharedMemoryRangeTraits {
 public:
  explicit SharedMemoryView(const SharedMemoryView&) = default;
  explicit SharedMemoryView(const SharedMemory& shared_memory)
      : SharedMemoryRangeTraits(shared_memory.vaddr(), shared_memory.paddr(),
                                shared_memory.size()) {}

 private:
  friend class SharedMemoryRangeTraits;

  using SharedMemoryRangeTraits::SharedMemoryRangeTraits;  // Inherit constructors
};

template <typename SharedMemoryPoolTraits>
class SharedMemoryPool {
 public:
  explicit SharedMemoryPool(zx_vaddr_t vaddr, zx_paddr_t paddr, size_t size)
      : vaddr_(vaddr),
        paddr_(paddr),
        region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())) {
    region_allocator_.AddRegion({.base = 0, .size = size});
  }

  zx_status_t Allocate(size_t size, std::unique_ptr<SharedMemory>* out_shared_memory) {
    // The RegionAllocator provides thread safety around allocations, so we currently don't
    // require any additional locking.

    // Let's try to carve off a region first.
    auto region = region_allocator_.GetRegion(size, kAlignment);
    if (!region) {
      return ZX_ERR_NO_RESOURCES;
    }

    auto shared_memory = std::make_unique<SharedMemory>(vaddr_, paddr_, std::move(region));

    *out_shared_memory = std::move(shared_memory);
    return ZX_OK;
  }

 private:
  static constexpr uint64_t kAlignment = SharedMemoryPoolTraits::kAlignment;

  const zx_vaddr_t vaddr_;
  const zx_paddr_t paddr_;
  RegionAllocator region_allocator_;
};

class SharedMemoryManager {
 public:
  struct DriverPoolTraits {
    static constexpr uint64_t kAlignment = 8;
  };

  struct ClientPoolTraits {
    static constexpr uint64_t kAlignment = PAGE_SIZE;
  };

  using DriverMemoryPool = SharedMemoryPool<DriverPoolTraits>;
  using ClientMemoryPool = SharedMemoryPool<ClientPoolTraits>;

  static zx_status_t Create(zx_paddr_t shared_mem_start, size_t shared_mem_size,
                            ddk::MmioBuffer secure_world_memory, zx::bti bti,
                            std::unique_ptr<SharedMemoryManager>* out_manager);
  ~SharedMemoryManager() = default;

  DriverMemoryPool* driver_pool() { return &driver_pool_; }
  ClientMemoryPool* client_pool() { return &client_pool_; }

 private:
  static constexpr size_t kNumDriverSharedMemoryPages = 4;
  static constexpr size_t kDriverPoolSize = 4 * PAGE_SIZE;

  explicit SharedMemoryManager(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr, size_t total_size,
                               ddk::MmioBuffer secure_world_memory,
                               ddk::MmioPinnedBuffer secure_world_memory_pin);

  ddk::MmioBuffer secure_world_memory_;
  ddk::MmioPinnedBuffer secure_world_memory_pin_;
  DriverMemoryPool driver_pool_;
  ClientMemoryPool client_pool_;
};

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_SHARED_MEMORY_H_
