// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_RISCV64_MMU_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_RISCV64_MMU_H_

#include <lib/arch/riscv64/system.h>
#include <lib/page-table/internal/bits.h>
#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <atomic>  // TODO(mcgrathr): <lib/stdcompat/atomic.h>
#include <optional>
#include <type_traits>

#include <hwreg/bitfields.h>

namespace page_table::riscv64 {

// Maximum valid physical address.
constexpr Paddr kMaxPhysAddress = Paddr((uint64_t(1) << 48) - 1);

// Supported granule sizes.
//
// The data value is the number of bits in the granule size.
enum class GranuleSize {
  k4KiB = 12,
};

// Return the number of bits of address in the given granule size.
constexpr size_t GranuleSizeShift(GranuleSize size) { return static_cast<size_t>(size); }

// Return the number of bytes spanned by a granule of the given size.
constexpr size_t GranuleBytes(GranuleSize size) { return size_t{1} << GranuleSizeShift(size); }

// Supported page sizes.
//
// The page sizes supported depends on the configured granule size for
// the page table.
//
// Values correspond to log_2(page_size).
enum class PageSize {
  // 4 kiB granules
  k4KiB = 12,
  k2MiB = 21,
  k1GiB = 30,
};

// Return the number of bits of address in the given page size.
constexpr size_t PageBits(PageSize size) { return static_cast<size_t>(size); }

// Return the number of bytes spanned by the given page size.
constexpr size_t PageBytes(PageSize size) { return size_t{1} << PageBits(size); }

// Return the granule size associated with the given page size.
//
// Each page size is only valid for one particular granule size.
GranuleSize GranuleForPageSize(PageSize page_size);

// RISCV page table entry.
struct PageTableEntry {
  uint64_t raw;

  // Bits [64:54] ignored.
  DEF_SUBFIELD(raw, 53, 10, ppn); // Physical page number
  DEF_SUBFIELD(raw, 9, 8, rsw);  // Reserved for software use
  DEF_SUBBIT(raw, 7, d); // Dirty
  DEF_SUBBIT(raw, 6, a); // Accessed
  DEF_SUBBIT(raw, 5, g); // Global
  DEF_SUBBIT(raw, 4, u); // User memory
  DEF_SUBBIT(raw, 3, x); // Executable
  DEF_SUBBIT(raw, 2, w); // Writable
  DEF_SUBBIT(raw, 1, r); // Readable
  DEF_SUBBIT(raw, 0, v); // Valid

  // Basic equality/inequality operators.
  friend bool operator==(const PageTableEntry& a, const PageTableEntry& b) {
    return a.raw == b.raw;
  }
  friend bool operator!=(const PageTableEntry& a, const PageTableEntry& b) { return !(a == b); }

  // Convenience function for creating Block/Page/Table entries marked
  // "present" at a given address.
  static constexpr PageTableEntry BlockAtAddress(Paddr addr) {
    return PageAtAddress(addr);
  }
  static constexpr PageTableEntry PageAtAddress(Paddr addr) {
    PageTableEntry pte = PageTableEntry{}.set_g(1)
					 .set_a(1)
					 .set_d(1)
					 .set_x(1)
					 .set_w(1)
					 .set_r(1)
					 .set_v(1)
					 .set_ppn(addr.value() >> 12);
    return pte;
  }
  static constexpr PageTableEntry TableAtAddress(Paddr addr) {
    PageTableEntry pte = PageTableEntry{}.set_v(1)
                                         .set_ppn(addr.value() >> 12);
    return pte;
  }
};

// Log base 2 Size of a PageTableEntry
constexpr uint64_t kPageTableEntrySizeShift = 3;
static_assert(1 << kPageTableEntrySizeShift == sizeof(PageTableEntry));

// Number of bits translated by a page table node of a particular granule size.
constexpr uint64_t TranslationBitsPerLevel(GranuleSize size) {
  return GranuleSizeShift(size) - kPageTableEntrySizeShift;
}

// Number of PageTableEntries for a page table node of a particular granule size.
constexpr uint64_t PageTableEntries(GranuleSize size) {
  return static_cast<uint64_t>(1) << TranslationBitsPerLevel(size);
}

// A span to a PageTableNode.
//
// Use of this pointer allows having code that handles all sizes of
// PageTableNode generically.
class PageTableNode {
 public:
  PageTableNode() = default;
  PageTableNode(PageTableEntry* entries, GranuleSize node_size)
      : entries_(entries), size_(node_size) {}

  // Allow copy and move.
  PageTableNode(const PageTableNode&) = default;
  PageTableNode& operator=(const PageTableNode&) = default;

  // Return the PTE at the given index.
  PageTableEntry at(size_t index) {
    // TODO(mcgrathr): cpp20::memory_order_relaxed for cpp20::atomic_ref
    return Entry(index).load(std::memory_order_relaxed);
  }

  // Set the PTE at the given index to the given value.
  void set(size_t index, PageTableEntry entry) {
    // TODO(mcgrathr): cpp20::memory_order_relaxed for cpp20::atomic_ref
    return Entry(index).store(entry, std::memory_order_relaxed);
  }

  // Get a pointer to the first element of the node.
  PageTableEntry* data() const { return entries_; }

  // Get the size of the node.
  GranuleSize size() const { return size_; }

 private:
  std::atomic<PageTableEntry>& Entry(size_t index) {
    ZX_DEBUG_ASSERT(index < PageTableEntries(size_));
    // TODO(mcgrathr): Replace this with cpp20::atomic_ref when it's ready.
    // The existing fbl::atomic_ref can't handle non-integer types like the std
    // type can.  Using fbl::atomic_ref<uint64_t> means losing the specific
    // alignment setting on the PageTableEntry type, which is always 64 bits
    // even when alignof(uint64_t) is only 32 bits as on x86-32.
    return *reinterpret_cast<std::atomic<PageTableEntry>*>(&entries_[index]);
  }

  PageTableEntry* entries_ = nullptr;
  GranuleSize size_ = GranuleSize::k4KiB;
};

// Describes a particular layout of an RISCV64 page table.
struct PageTableLayout {
  // Number of bits of a virtual address each level translates.
  constexpr uint64_t TranslationBitsPerLevel() const {
    return page_table::riscv64::TranslationBitsPerLevel(granule_size);
  }

  // Number of levels in the layout.
  constexpr uint64_t NumLevels() const {
    // The number of levels in the page table.
    //
    // The page tables need to resolve `region_size_bits - granule_bits` bits,
    // where each level can translate at most `bits_per_level` bits.
    uint64_t bits_to_resolve = region_size_bits - GranuleSizeShift(granule_size);
    uint64_t bits_per_level = TranslationBitsPerLevel();
    return static_cast<uint64_t>((bits_to_resolve + (bits_per_level - 1)) /
                                 bits_per_level);  // Divide rounding up
  }

  // Number of bits of virtual address covered by a PTE at the given level in this layout.
  constexpr uint64_t PageTableEntryRangeBits(uint64_t level) const {
    return static_cast<uint64_t>(level * TranslationBitsPerLevel() +
                                 GranuleSizeShift(granule_size));
  }

  // Return the size of the address space, in bytes.
  constexpr uint64_t AddressSpaceSize() const { return (uint64_t{1} << region_size_bits); }

  // Number of bits per granule.
  //
  // This will be the size of each page table node and the base size of pages.
  GranuleSize granule_size;

  // Number of bits in the address space.
  uint64_t region_size_bits;
};

}  // namespace page_table::riscv64

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_RISCV64_MMU_H_
