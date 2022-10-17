// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_VIRTUAL_ALLOC_INCLUDE_LIB_VIRTUAL_ALLOC_H_
#define ZIRCON_KERNEL_LIB_VIRTUAL_ALLOC_INCLUDE_LIB_VIRTUAL_ALLOC_H_

#include <inttypes.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <bitmap/raw-bitmap.h>
#include <fbl/canary.h>
#ifdef _KERNEL
// for vm_page_state and vm_page_t
#include <vm/page.h>
#else
// vm_page_state is an enum that only has meaning when running in kernel mode. We invent a type for
// this just to avoid excessive #ifdef'ing of the constructor.
using vm_page_state = uint32_t;
// vaddr_t is normally defined by the internal kernel headers.
using vaddr_t = uintptr_t;
#endif

// VirtualAlloc is a page granule allocator that manages a given virtual region and provides
// virtually contiguous allocations inside that region. This allocator explicitly has no dependency
// on the heap and retrieves all its backing memory directly from the pmm. To achieve this it maps
// pages directly into the hardware page tables via the arch aspace, and consequently assumes that
// these operations will only depend on the pmm and not the heap for allocating any intermediate
// page tables.
//
// This class is thread-unsafe.
class VirtualAlloc {
 public:
  // Allocator needs to be told what state to set any allocated pages to. This allows for using the
  // allocator as a heap or generic allocator for an object.
  explicit VirtualAlloc(vm_page_state allocated_page_state);
  ~VirtualAlloc();

  DISALLOW_COPY_ASSIGN_AND_MOVE(VirtualAlloc);

  // Initialize the allocator and make it ready for use. Takes a a virtual address range as a base
  // and size in bytes. These both must be page aligned and there must be no pages mapped into the
  // hardware page tables for this range.
  // |alloc_guard| is the minimum number of virtual pages to place between two allocations and
  // serve as guard pages. Any reads or writes that over or under run an allocation into the padding
  // will trigger an immediate hardware page fault.
  zx_status_t Init(vaddr_t base, size_t size, size_t alloc_guard, size_t align_log2);

  // AllocPages can only be called after after a successful Init call, otherwise it will return an
  // error. Number of pages requested must be non zero.
  zx::result<vaddr_t> AllocPages(size_t pages);

  // Returns a non-zero number of pages at the given virtual address. Partial frees are supported
  // such that if 2 pages were allocated at X it is allowed to FreePages(X, 1) and separately
  // FreePages(X + PAGE_SIZE, 1).
  void FreePages(vaddr_t vaddr, size_t pages);

  // Returns the number of pages allocated to provide the bitmap for tracking allocations. Exposed
  // as a debug named function as there is no promise that it has to be a bitmap for tracking and
  // this interface is likely to change and should only be used for debugging.
  size_t DebugBitmapPages() const { return BitmapPages(); }

  // Frees any allocated pages as if FreePages had been called on every outstanding allocation. This
  // is exposed for testing for circumstances where externally tracking every allocations is
  // burdensome and not the goal of the test.
  void DebugFreeAllAllocations();

  // Force allocates the given range, overriding any padding or alignment requirements. This is used
  // for testing to more precisely control available allocation regions. Panics if range is invalid
  // or already allocated.
  void DebugAllocateVaddrRange(vaddr_t vaddr, size_t pages);

 private:
  // Custom storage for the bitmap class that provides for a statically sized storage that can be
  // initialized after construction.
  class BitmapStorage {
   public:
    BitmapStorage() = default;
    ~BitmapStorage() = default;

    void Init(void* base, size_t size) {
      base_ = base;
      size_ = size;
    }

    zx_status_t Allocate(size_t size) {
      if (size > size_) {
        return ZX_ERR_NO_MEMORY;
      }
      return ZX_OK;
    }
    void* GetData() { return base_; }
    const void* GetData() const { return base_; }
    size_t GetSize() const { return size_; }

   private:
    void* base_ = nullptr;
    size_t size_ = 0;
  };

  zx_status_t AllocMapPages(vaddr_t vaddr, size_t num_pages);
  void UnmapFreePages(vaddr_t vaddr, size_t num_pages);
  void Destroy();
  zx::result<size_t> BitmapAlloc(size_t num_pages);
  zx::result<size_t> BitmapAllocRange(size_t num_pages, size_t start, size_t end);
  void BitmapFree(size_t start, size_t num_pages);
  size_t BitmapPages() const;

  fbl::Canary<fbl::magic("VALC")> canary_;

  // Page state to set allocated pages to.
  const vm_page_state allocated_page_state_;

  // Record of that padding to be applied to every allocation.
  size_t alloc_guard_ = 0;

  // The virtual address of the start of the range managed by bitmap_.
  vaddr_t alloc_base_ = 0;

  // Heuristic used to attempt to begin searching for a free run in bitmap_ at an optimal point.
  size_t next_search_start_ = 0;

  size_t align_log2_ = 0;

  // Bitmap representing allocated pages in the virtual range being managed. This is fully
  // preallocated and reserves for itself a portion of the Init virtual address range.
  bitmap::RawBitmapGeneric<BitmapStorage> bitmap_;
};

#endif  // ZIRCON_KERNEL_LIB_VIRTUAL_ALLOC_INCLUDE_LIB_VIRTUAL_ALLOC_H_
