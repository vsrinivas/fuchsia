// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_
#define ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_

#include <fbl/auto_call.h>
#include <fbl/mutex.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

namespace fbl {

// Growable Peristant Arena (GPArena) is an arena that allows for fast allocation and deallocation
// of a single kind of object. Compared to other arena style allocators it additionally guarantees
// that a portion of the objects memory will be preserved between calls to Free+Alloc.
template <size_t PersistSize, size_t ObjectSize>
class GPArena {
 public:
  GPArena() = default;
  ~GPArena() {
    DEBUG_ASSERT(count_ == 0);
    if (vmar_ != nullptr) {
      // Unmap all of our memory and free our resources.
      vmar_->Destroy();
      vmar_.reset();
    }
  }

  zx_status_t Init(const char* name, size_t max_count) {
    if (!max_count)
      return ZX_ERR_INVALID_ARGS;

    // Carve out some memory from the kernel root VMAR
    const size_t mem_sz = ROUNDUP(max_count * ObjectSize, PAGE_SIZE);

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, mem_sz, &vmo);
    if (status != ZX_OK) {
      return status;
    }

    auto kspace = VmAspace::kernel_aspace();
    DEBUG_ASSERT(kspace != nullptr);
    auto root_vmar = kspace->RootVmar();
    DEBUG_ASSERT(root_vmar != nullptr);

    char vname[32];
    snprintf(vname, sizeof(vname), "gparena:%s", name);
    vmo->set_name(vname, sizeof(vname));

    zx_status_t st = root_vmar->CreateSubVmar(
        0,             // offset (ignored)
        mem_sz, false, /*align_pow2=*/
        VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE | VMAR_FLAG_CAN_MAP_SPECIFIC, vname,
        &vmar_);
    if (st != ZX_OK || vmar_ == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    // The VMAR's parent holds a ref, so it won't be destroyed
    // automatically when we return.
    auto destroy_vmar = fbl::MakeAutoCall([this]() { vmar_->Destroy(); });

    st = vmar_->CreateVmMapping(0,  // mapping_offset
                                mem_sz,
                                false,  // align_pow2
                                VMAR_FLAG_SPECIFIC, vmo,
                                0,  // vmo_offset
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, "gparena",
                                &mapping_);
    if (st != ZX_OK || mapping_ == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }

    top_ = committed_ = start_ = mapping_->base();
    end_ = start_ + mem_sz;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(end_));

    destroy_vmar.cancel();

    return ZX_OK;
  }

  // Returns a raw pointer and not a reference to an object of type T so that the memory can be
  // inspected prior to construction taking place.
  void* Alloc() {
    // Need one acquire to match with the release in Free
    FreeNode* head = free_head_.load(ktl::memory_order_acquire);
    while (head && !free_head_.compare_exchange_strong(head, head->next, ktl::memory_order_relaxed,
                                                       ktl::memory_order_relaxed)) {
      // There is no pause here as we don't need to wait for anyone before trying again,
      // rather the sooner we retry the *more* likely we are to succeed given that we just
      // received the most up to date copy of head.
    }

    if (head == nullptr) {
      uintptr_t top = top_.load(ktl::memory_order_relaxed);
      uintptr_t next_top;
      do {
        // Every time the compare_exchange below fails top becomes the current value and so
        // we recalculate our potential next_top every iteration from it.
        next_top = top + ObjectSize;
        // See if we need to commit more memory.
        if (next_top > committed_.load(ktl::memory_order_relaxed)) {
          if (!Grow(next_top)) {
            return nullptr;
          }
        }
      } while (!top_.compare_exchange_strong(top, next_top, ktl::memory_order_relaxed,
                                             ktl::memory_order_relaxed));
      head = reinterpret_cast<FreeNode*>(top);
    }
    count_.fetch_add(1, ktl::memory_order_relaxed);
    return reinterpret_cast<void*>(head);
  }

  // Takes a raw pointer as the destructor is expected to have already been run.
  void Free(void* node) {
    FreeNode* free_node = reinterpret_cast<FreeNode*>(node);
    FreeNode* head = free_head_.load(ktl::memory_order_relaxed);
    do {
      // Every time the compare_exchange below fails head becomes the current value and so
      // we need to reset our intended next pointer every iteration.
      free_node->next = head;
      // Use release semantics so that any writes to the Persist area are visible before the
      // node can be reused.
    } while (!free_head_.compare_exchange_strong(head, free_node, ktl::memory_order_release,
                                                 ktl::memory_order_relaxed));
    count_.fetch_sub(1, ktl::memory_order_relaxed);
  }

  size_t DiagnosticCount() const { return count_.load(ktl::memory_order_relaxed); }

  bool Committed(void* node) const {
    uintptr_t n = reinterpret_cast<uintptr_t>(node);
    return n >= start_ && n < top_.load(ktl::memory_order_relaxed);
  }

  void* Base() const { return reinterpret_cast<void*>(start_); }

  void Dump() {
    // Take the mapping lock so we can safely dump the vmar_ without mappings being done in
    // parallel.
    Guard<Mutex> guard{&mapping_lock_};

    DEBUG_ASSERT(vmar_ != nullptr);
    printf("GPArena<%#zx,%#zx> %s mappings:\n", PersistSize, ObjectSize, vmar_->name());
    vmar_->Dump(/* depth */ 1, /* verbose */ true);

    printf(" start 0x%#zx\n", start_);
    const size_t nslots = (top_ - start_) / ObjectSize;
    printf(" top 0x%zx (%zu slots allocated)\n", top_.load(ktl::memory_order_relaxed), nslots);
    const size_t np = (committed_.load(ktl::memory_order_relaxed) - start_) / PAGE_SIZE;
    const size_t npmax = (end_ - start_) / PAGE_SIZE;
    printf(" committed 0x%#zx (%zu/%zu pages)\n", committed_.load(ktl::memory_order_relaxed), np,
           npmax);
    const size_t tslots = static_cast<size_t>(end_ - start_) / ObjectSize;
    printf(" end 0x%#zx (%zu slots total)\n", end_, tslots);
    const size_t free_list = nslots - count_;
    printf(" free list length %zu\n", free_list);
  }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(GPArena);

  // Attempts to grow the committed memory range such that next_top is included in the range.
  bool Grow(uintptr_t next_top) {
    // take the mapping lock
    Guard<Mutex> guard{&mapping_lock_};
    // Cache committed_ as only we can change it as we have the lock.
    uintptr_t committed = committed_.load(ktl::memory_order_relaxed);
    // now that we have the lock, double check we need to proceed
    if (next_top > committed) {
      uintptr_t nc = committed + 4 * PAGE_SIZE;
      // Clip our commit attempt to the end of our mapping.
      if (nc > end_) {
        nc = end_;
      }
      if (nc == committed) {
        // If we aren't going to commit more than we already haven then this
        // means we have completely filled the arena.
        return false;
      }
      const size_t offset = reinterpret_cast<vaddr_t>(committed) - start_;
      const size_t len = nc - committed;
      zx_status_t st = mapping_->MapRange(offset, len, /* commit */ true);
      if (st != ZX_OK) {
        // Try to clean up any committed pages, but don't require
        // that it succeeds.
        mapping_->DecommitRange(offset, len);
        return false;
      }
      committed_.store(nc, ktl::memory_order_relaxed);
    }
    return true;
  }

  struct FreeNode {
    char data[PersistSize];
    // This struct is explicitly not packed to allow for the next field to be naturally aligned.
    // As a result we *may* preserve more than PersistSize, but that is fine.
    ktl::atomic<FreeNode*> next;
  };
  static_assert(sizeof(FreeNode) <= ObjectSize, "Not enough free space in object");
  static_assert((ObjectSize % alignof(FreeNode)) == 0,
                "ObjectSize must be common alignment multiple");
  fbl::RefPtr<VmAddressRegion> vmar_;
  fbl::RefPtr<VmMapping> mapping_;

  uintptr_t start_ = 0;
  // top_ is the address of the last allocated object from the arena.
  ktl::atomic<uintptr_t> top_ = 0;
  // start_ .. committed_ represents the committed and mapped portion of the arena.
  ktl::atomic<uintptr_t> committed_ = 0;
  // start_ .. end_ represent the total virtual address reservation for the arena, and committed_
  // may not grow past end_.
  uintptr_t end_ = 0;

  DECLARE_MUTEX(GPArena) mapping_lock_;

  ktl::atomic<size_t> count_ = 0;

  ktl::atomic<FreeNode*> free_head_ = nullptr;
};

}  // namespace fbl

#endif  // ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_
