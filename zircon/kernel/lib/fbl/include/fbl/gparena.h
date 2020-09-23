// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_
#define ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_

#include <align.h>
#include <fbl/auto_call.h>
#include <fbl/confine_array_index.h>
#include <kernel/mutex.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

namespace fbl {

// Growable Persistant Arena (GPArena) is an arena that allows for fast allocation and deallocation
// of a single kind of object. Compared to other arena style allocators it additionally guarantees
// that a portion of the objects memory will be preserved between calls to Free+Alloc.
template <size_t PersistSize, size_t ObjectSize>
class __OWNER(void) GPArena {
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

    fbl::RefPtr<VmObjectPaged> vmo;
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
    // Take a local copy/snapshot of the current head node.
    // Use an acquire to match with the release in Free.
    HeadNode head_node = head_node_.load(ktl::memory_order_acquire);
    while (head_node.head) {
      const HeadNode next_head_node(head_node.head->next, head_node.gen + 1);
      if (head_node_.compare_exchange_strong(head_node, next_head_node, ktl::memory_order_acquire,
                                             ktl::memory_order_acquire)) {
        count_.fetch_add(1, ktl::memory_order_relaxed);
        return reinterpret_cast<void*>(head_node.head);
      }
      // There is no pause here as we don't need to wait for anyone before trying again,
      // rather the sooner we retry the *more* likely we are to succeed given that we just
      // received the most up to date copy of head_node.
    }

    // Nothing in the free list, we need to grow.
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
    count_.fetch_add(1, ktl::memory_order_relaxed);
    return reinterpret_cast<void*>(top);
  }

  // Takes a raw pointer as the destructor is expected to have already been run.
  void Free(void* node) {
    FreeNode* free_node = reinterpret_cast<FreeNode*>(node);
    // Take a local copy/snapshot of the current head node.
    HeadNode head_node = head_node_.load(ktl::memory_order_relaxed);
    HeadNode next_head_node;
    do {
      // Every time the compare_exchange below fails head_node becomes the current value and so
      // we need to reset our intended next pointer every iteration.
      free_node->next = head_node.head;
      // Build our candidate next head node.
      next_head_node = HeadNode(free_node, head_node.gen + 1);
      // Use release semantics so that any writes to the Persist area, and our write to
      // free_node->next, are visible before the node can be seen in the free list and reused.
    } while (!head_node_.compare_exchange_strong(
        head_node, next_head_node, ktl::memory_order_release, ktl::memory_order_relaxed));
    count_.fetch_sub(1, ktl::memory_order_relaxed);
  }

  size_t DiagnosticCount() const { return count_.load(ktl::memory_order_relaxed); }

  bool Committed(void* node) const {
    uintptr_t n = reinterpret_cast<uintptr_t>(node);
    return n >= start_ && n < top_.load(ktl::memory_order_relaxed);
  }

  // Return |address| if it is within the valid range of the arena or the base of the arena if not.
  // Hardened against Spectre V1 / bounds check bypass speculation attacks - it always returns a
  // safe value, even under speculation.
  uintptr_t Confine(uintptr_t address) const {
    const size_t size = top_.load(ktl::memory_order_relaxed) - start_;
    uintptr_t offset = address - start_;
    offset = fbl::confine_array_index(offset, /*size=*/size);
    return start_ + offset;
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
    // As a result we *may* preserve more than PersistSize, but that is fine. This is not an atomic
    // as reads and writes will be serialized with our updates to head_node_, which acts like a
    // lock.
    FreeNode* next;
  };
  static_assert(sizeof(FreeNode) <= ObjectSize, "Not enough free space in object");
  static_assert((ObjectSize % alignof(FreeNode)) == 0,
                "ObjectSize must be common alignment multiple");
  fbl::RefPtr<VmAddressRegion> vmar_;
  fbl::RefPtr<VmMapping> mapping_;

  uintptr_t start_ = 0;
  // top_ is the address of the next object to be allocated from the arena.
  ktl::atomic<uintptr_t> top_ = 0;
  // start_ .. committed_ represents the committed and mapped portion of the arena.
  ktl::atomic<uintptr_t> committed_ = 0;
  // start_ .. end_ represent the total virtual address reservation for the arena, and committed_
  // may not grow past end_.
  uintptr_t end_ = 0;

  DECLARE_MUTEX(GPArena) mapping_lock_;

  ktl::atomic<size_t> count_ = 0;

  // Stores the current head pointer and a generation count. The generation count prevents races
  // where one thread is modifying the list whilst another thread rapidly adds and removes. Every
  // time a HeadNode is modified the generation count should be incremented to generate a unique
  // value.
  //
  // It is important that the count not wrap past an existing value that is still in use. The
  // generation is currently 64-bit number and shouldn't ever wrap back to 0 to begin with. Although
  // even if it should it is incredibly unlikely a thread was stalled for 2^64 operations to cause a
  // generation collision.
  struct alignas(16) HeadNode {
    FreeNode* head = nullptr;
    uint64_t gen = 0;
    HeadNode() = default;
    HeadNode(FreeNode* h, uint64_t g) : head(h), gen(g) {}
  };
  ktl::atomic<HeadNode> head_node_{};
#ifdef __clang__
  static_assert(ktl::atomic<HeadNode>::is_always_lock_free, "atomic<HeadNode> not lock free");
#else
  // For gcc we have a custom libatomic implementation that *is* lock free, but the compiler doesn't
  // know that at this point. Instead we ensure HeadNode is 16bytes so ensure our atomics will be
  // the ones used.
  static_assert(sizeof(HeadNode) == 16, "HeadNode must be 16 bytes");
#endif
};

}  // namespace fbl

#endif  // ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_GPARENA_H_
