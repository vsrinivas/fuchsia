// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_HANDOFF_PREP_H_
#define ZIRCON_KERNEL_PHYS_HANDOFF_PREP_H_

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/new.h>
#include <lib/trivial-allocator/single-heap-allocator.h>
#include <zircon/boot/image.h>

#include <fbl/alloc_checker.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <phys/handoff-ptr.h>

struct PhysHandoff;
class PhysBootTimes;

class HandoffPrep {
 public:
  // TODO(fxbug.dev/84107): The first argument is the space inside the data ZBI
  // where the ZBI_TYPE_STORAGE_KERNEL was, the only safe space to reuse for
  // now.  Eventually this function will just allocate from the memalloc::Pool
  // using a type designated for handoff data so the kernel can decide if it
  // wants to reuse the space after consuming all the data.
  void Init(ktl::span<ktl::byte> handoff_payload);

  // This is the main structure.  After Init has been called the pointer is
  // valid but the data is in default-constructed state.
  PhysHandoff* handoff() { return handoff_; }

  // This returns new T(args...) using the temporary handoff allocator and
  // fills in the handoff_ptr to point to it.
  template <typename T, typename... Args>
  T* New(PhysHandoffTemporaryPtr<const T>& handoff_ptr, fbl::AllocChecker& ac, Args&&... args) {
    T* ptr = new (allocator(), ac) T(ktl::forward<Args>(args)...);
    if (ptr) {
      void* generic_ptr = static_cast<void*>(ptr);
      handoff_ptr.ptr_ = reinterpret_cast<uintptr_t>(generic_ptr);
    }
    return ptr;
  }

  // Summarizes the provided data ZBI's miscellaneous simple items for the
  // kernel, filling in corresponding handoff()->item fields.
  void SummarizeMiscZbiItems(ktl::span<const ktl::byte> zbi);

 private:
  using AllocateFunction = trivial_allocator::SingleHeapAllocator;
  using Allocator = trivial_allocator::BasicLeakyAllocator<AllocateFunction>;

  // TODO(fxbug.dev/84107): Later this will just return
  // gPhysNew<memalloc::Type::kPhysHandoff>.
  Allocator& allocator() { return allocator_; }

  // The arch-specific protocol for a given item.
  // Defined in //zircon/kernel/arch/$cpu/phys/arch-handoff-prep-zbi.cc.
  void ArchSummarizeMiscZbiItem(const zbi_header_t& header, ktl::span<const ktl::byte> payload);

  Allocator allocator_;
  PhysHandoff* handoff_ = nullptr;
};

#endif  // ZIRCON_KERNEL_PHYS_HANDOFF_PREP_H_
