// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_TLB_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_TLB_H_

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <ktl/limits.h>
#include <ktl/pair.h>
#include <ktl/type_traits.h>

namespace hypervisor {

struct TlbEntry {
  // Guest virtual address.
  zx_vaddr_t virt;
  // Host physical address.
  zx_paddr_t phys;
};

// A TLB to cache guest virtual to host physical address translations.
//
// The TLB is constructed from two arrays:
// 1. To store TLB entries, and
// 2. To store indices into the 1.
//
// The TLB implements an LRU cache on top of the smaller indices array.
// `IndexType` is used to represent the type of indices stored, `TlbSize` the
// number of TLB entries, and `PageMask` is used mask address to resolve the
// relevant page.
template <typename IndexType, size_t TlbSize, uint64_t PageMask>
class Tlb {
 public:
  static_assert(ktl::is_unsigned_v<IndexType>);
  static_assert(ktl::numeric_limits<IndexType>::max() >= TlbSize - 1);

  Tlb() {
    // Initialise `indices_` to point to valid TLB entries.
    for (size_t i = 0; i < TlbSize; i++) {
      indices_[i] = static_cast<IndexType>(i);
    }
    Reset();
  }

  // Size of the TLB.
  constexpr size_t Size() const { return TlbSize; }

  // Resets the TLB.
  void Reset() {
    Guard<SpinLock, IrqSave> guard{&lock_};
    memset(entries_, 0xff, sizeof(entries_));
  }

  // Clears a particular range of addresses from the TLB.
  void ClearRange(zx_vaddr_t addr, size_t len) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    for (size_t i = 0; i < TlbSize; i++) {
      TlbEntry& entry = entries_[indices_[i]];
      if (entry.virt < addr || entry.virt >= addr + len) {
        continue;
      }
      entry.virt = UINT64_MAX;
      if (i != TlbSize - 1) {
        IndexType index = indices_[i];
        memmove(indices_ + i, indices_ + i + 1, sizeof(IndexType) * (TlbSize - i - 1));
        indices_[TlbSize - 1] = index;
        i--;
      }
    }
  }

  // Find the host physical address for to the provided guest virtual address.
  ktl::pair<zx_paddr_t, bool> Find(zx_vaddr_t virt) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    for (size_t i = 0; i < TlbSize; i++) {
      const TlbEntry& entry = entries_[indices_[i]];
      if (entry.virt != (virt & PageMask)) {
        continue;
      }
      if (i != 0) {
        IndexType index = indices_[i];
        memmove(indices_ + 1, indices_, sizeof(IndexType) * i);
        indices_[0] = index;
      }
      return ktl::pair{entry.phys, true};
    }
    return ktl::pair{0, false};
  }

  // Insert a mapping from guest virtual to host physical address.
  void Insert(zx_vaddr_t virt, zx_paddr_t phys) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    IndexType index = indices_[TlbSize - 1];
    memmove(indices_ + 1, indices_, sizeof(IndexType) * (TlbSize - 1));
    indices_[0] = index;
    entries_[index] = {
        .virt = virt & PageMask,
        .phys = phys & PageMask,
    };
  }

 private:
  DECLARE_SPINLOCK(Tlb) lock_;
  IndexType indices_[TlbSize] TA_GUARDED(lock_);
  TlbEntry entries_[TlbSize] TA_GUARDED(lock_);
};

constexpr uint64_t k4kbPageFrame = ~((1ul << 12) - 1);
using DefaultTlb = Tlb<uint8_t, 256, k4kbPageFrame>;

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_TLB_H_
