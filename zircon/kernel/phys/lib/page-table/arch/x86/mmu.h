// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_MMU_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_MMU_H_

#include <lib/page-table/internal/bits.h>
#include <lib/page-table/types.h>
#include <lib/stdcompat/atomic.h>
#include <zircon/types.h>

#include <atomic>  // TODO(mcgrathr): <lib/stdcompat/atomic.h>
#include <optional>
#include <type_traits>

#include <hwreg/bitfields.h>

namespace page_table::x86 {

// Number of bits supported in the virtual / physical addresses.
//
// [intel/vol3]: Section 4.5: 4-Level Paging and 5-Level Paging
constexpr uint64_t kVirtAddressBits = 48;
constexpr uint64_t kPhysAddressBits = 52;

// The maximum valie physical address.
//
// See IsCanonicalVaddr() for determining if a virtual address is valid.
constexpr Paddr kMaxPhysAddress = Paddr((uint64_t(1) << kPhysAddressBits) - 1);

// Number of page table levels.
//
// While the x86_64 architecture can support 5 levels on some CPUs,
// we only support 4 levels.
//
// [intel/vol3]: Section 4.5: 4-Level Paging and 5-Level Paging
constexpr int64_t kPageTableLevels = 4;

// Number of bits covered by an entry at a given level.
//
// [intel/vol3]: Figure 4-8: Linear-Address Translation to a 4-KByte Page using 4-Level Paging
constexpr uint64_t kPtBits = 12;    // Level 0: Page table
constexpr uint64_t kPdBits = 21;    // Level 1: Page directory
constexpr uint64_t kPdpBits = 30;   // Level 2: Page directory pointer
constexpr uint64_t kPml4Bits = 39;  // Level 3: page map level 4.

// Number of entries per level, and the number of bits this corresponds to.
constexpr uint64_t kBitsPerLevel = 9;
constexpr uint64_t kEntriesPerNode = 512;
static_assert(1 << kBitsPerLevel == kEntriesPerNode);

// Size of a node in the page table.
constexpr uint64_t kPageTableNodeBytes = 4096;

// Number of bits translated by the given level of page-table.
constexpr uint8_t PageLevelBits(int8_t level) {
  return static_cast<uint8_t>(kPtBits + kBitsPerLevel * level);
}

// Page size constants.
constexpr size_t kPageSize4KiB = 4u * 1024u;
constexpr size_t kPageSize2MiB = 2u * 1024u * 1024u;
constexpr size_t kPageSize1GiB = 1024u * 1024u * 1024u;

// Supported page sizes.
//
// [intel/vol3]: Section 4.5: 4-Level Paging and 5-Level Paging
enum class PageSize {
  k4KiB = kPageSize4KiB,
  k2MiB = kPageSize2MiB,
  k1GiB = kPageSize1GiB,
};

// Return the number of bytes in the given PageSize.
constexpr size_t PageBytes(PageSize size) { return static_cast<size_t>(size); }

// Determine if the given virtual address is in canonical form.
//
// Virtual addresses consist of 48 bits ([0:47]) with the remaining
// bits a sign extension of bit 47 (that is, bits [63:48] should match
// bit 47).
//
// [intel/vol1]: Section 3.3.7.1: Canonical Addressing
constexpr bool IsCanonicalVaddr(Vaddr addr) {
  return internal::SignExtend(addr.value(), kVirtAddressBits) == addr.value();
}

// x86-64 page table base entry.
//
// The structure defines fields common to page table entries on all
// levels of the tree.
//
// [intel/vol3]: Figure 4-11: Formats of CR3 and Paging-Structure Entries with
// 4-Level Paging and 5-Level Paging
//
// [amd/vol2]: 5.4.1  Field Definitions.
struct alignas(sizeof(uint64_t)) PageTableEntry {
  uint64_t raw;

  //
  // Fields valid for all entry types.
  //

  DEF_SUBBIT(raw, 63, execute_disable);       // "XD": Prevent instruction fetches on this range.
  DEF_SUBFIELD(raw, 62, 59, protection_key);  // "PK": memory protection key
  // Bits 58:52 reserved
  DEF_UNSHIFTED_SUBFIELD(raw, 51, 12, child_paddr);  // Physical address of child table.
  // Bits 11:7 ignored.
  DEF_SUBBIT(raw, 8, global);  // "G": If CR4.PGE == 1, indicates a global translation.
  // Bit 7 is PAT or indicates if this is a terminal leaf, depending on level.
  DEF_SUBBIT(raw, 6, dirty);               // "D": Software has written to this page.
  DEF_SUBBIT(raw, 5, accessed);            // "A": This entry has been used for translation.
  DEF_SUBBIT(raw, 4, page_cache_disable);  // "PCD": Disable page-level caches.
  DEF_SUBBIT(raw, 3, page_write_through);  // "PWT": Page-level write through cache.
  DEF_SUBBIT(raw, 2, user_supervisor);     // "U/S": If 1, allow user-mode access.
  DEF_SUBBIT(raw, 1, read_write);          // "R/W": Allow writes to region. Enforced CR0.WP == 1.
  DEF_SUBBIT(raw, 0, present);             // "P": Entry contains valid data.

  // Determine if this entry points to a page. If false, it refers to a child node.
  bool is_page(int8_t level) const;
  PageTableEntry& set_is_page(int8_t level, bool value);

  // Get / set the page attribute table ("PAT") bit. It's location is
  // level-dependent.
  uint64_t pat(int8_t level) const;
  PageTableEntry& set_pat(int8_t level, uint64_t value);

  // Get / set the physical address of the page this entry refers to.
  uint64_t page_paddr(int8_t level) const;
  PageTableEntry& set_page_paddr(int8_t level, uint64_t value);

  // Basic equality/inequality operators.
  friend bool operator==(const PageTableEntry& a, const PageTableEntry& b) {
    return a.raw == b.raw;
  }
  friend bool operator!=(const PageTableEntry& a, const PageTableEntry& b) { return !(a == b); }

 private:
  // Location of the PAT bit at different levels.
  static constexpr std::array<int, kPageTableLevels> kPatBitIndex = {7, 12, 12, -1};

  // Location of bit that indicates if a PTE entry is a page or page table
  // pointer at different levels.
  static constexpr std::array<int, kPageTableLevels> kPageBitIndex = {-1, 7, 7, -1};
};
static_assert(sizeof(PageTableEntry) == sizeof(uint64_t));
static_assert(alignof(PageTableEntry) == sizeof(uint64_t));

// A node in the page table.
class alignas(kPageTableNodeBytes) PageTableNode {
 public:
  // Return the PTE at the given index.
  PageTableEntry at(size_t index) { return Entry(index).load(std::memory_order_relaxed); }

  // Set the PTE at the given index to the given value.
  void set(size_t index, PageTableEntry entry) {
    Entry(index).store(entry, std::memory_order_relaxed);
  }

 private:
  cpp20::atomic_ref<PageTableEntry> Entry(size_t index) {
    return cpp20::atomic_ref<PageTableEntry>(entries_[index]);
  }

  PageTableEntry entries_[kEntriesPerNode] = {};
};
static_assert(sizeof(PageTableNode) == kPageTableNodeBytes);
static_assert(alignof(PageTableNode) == kPageTableNodeBytes);
static_assert(std::is_standard_layout_v<PageTableNode>);

//
// Implementation details below.
//

}  // namespace page_table::x86

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_MMU_H_
