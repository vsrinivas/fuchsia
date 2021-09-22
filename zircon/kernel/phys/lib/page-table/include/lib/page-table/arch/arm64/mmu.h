// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_MMU_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_MMU_H_

#include <lib/arch/arm64/system.h>
#include <lib/page-table/internal/bits.h>
#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <atomic>  // TODO(mcgrathr): <lib/stdcompat/atomic.h>
#include <optional>
#include <type_traits>

#include <hwreg/bitfields.h>

namespace page_table::arm64 {

// Maximum valid physical address.
//
// We don't attempt to support the ARM FEAT_LPA feature, which would
// increase this to 2**52 - 1.
//
// [arm/v8]: Table D5-4 Physical address size implementation options
constexpr Paddr kMaxPhysAddress = Paddr((uint64_t(1) << 48) - 1);

// Supported granule sizes.
//
// The data value is the number of bits in the granule size.
enum class GranuleSize {
  k4KiB = 12,
  k16KiB = 14,
  k64KiB = 16,
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

  // 16 kiB granules
  k16KiB = 14,
  k32MiB = 25,

  // 64 kiB granules
  k64KiB = 16,
  k512MiB = 29,
};

// Return the number of bits of address in the given page size.
constexpr size_t PageBits(PageSize size) { return static_cast<size_t>(size); }

// Return the number of bytes spanned by the given page size.
constexpr size_t PageBytes(PageSize size) { return size_t{1} << PageBits(size); }

// Return the granule size associated with the given page size.
//
// Each page size is only valid for one particular granule size.
GranuleSize GranuleForPageSize(PageSize page_size);

// Page table entry types.
//
// [arm/v8]: D5.3.1 VMSAv8-64 translation table level 0, level 1, and level 2
// descriptor formats.
// [arm/v8]: D5.3.2 Armv8 translation table level 3 descriptor formats
enum class PageTableEntryType {
  // Block descriptor for levels {0, 1, 2}. Invalid for level 3.
  kBlockDescriptor = 0b0,

  // Table descriptor for levels {0, 1, 2}, page descriptor for level 3.
  kTableOrPageDescriptor = 0b1,
};

// Access permission for page table entries.
//
// [arm/v8]: Table D5-28 Data access permissions for stage 1 translations
enum class PagePermissions {
  kSupervisorReadWrite = 0b00,  // EL1+ can read/write, EL0 no access.
  kReadWrite = 0b01,            // All levels can read/write.
  kSupervisorReadOnly = 0b10,   // EL1+ can read, EL0 no access.
  kReadOnly = 0b11,             // All levels can read.
};

// Shareability attribute for Normal memory
//
// Table D5-36 SH[1:0] field encoding for Normal memory, VMSAv8-64 translation table format
enum class Shareability {
  kNonShareable = 0b00,
  kOuterShareable = 0b10,
  kInnerShareable = 0b11,
};

// Page table entry upper and lower attributes.
//
// [arm/v8]: D5.3.3 Memory attribute fields in the VMSAv8-64 translation table format descriptors
struct PteUpperAttrs {
  uint64_t raw;

  // Bit 63 ignored.
  DEF_SUBFIELD(raw, 62, 59, pbha);  // Page-based hardware attributes
  // Bits [58:55] ignored.
  DEF_SUBBIT(raw, 54, xn);          // Execute never / Unprivileged execute never
  DEF_SUBBIT(raw, 53, pxn);         // Privileged execute never
  DEF_SUBBIT(raw, 52, contiguous);  //
  DEF_SUBBIT(raw, 51, dbm);         // Dirty bit modifier
  DEF_SUBBIT(raw, 50, gp);          // Guarded page
};
struct PteLowerAttrs {
  uint64_t raw;

  DEF_SUBBIT(raw, 11, ng);                            // Not global
  DEF_SUBBIT(raw, 10, af);                            // Access flag
  DEF_ENUM_SUBFIELD(raw, Shareability, 9, 8, sh);     // Shareability
  DEF_ENUM_SUBFIELD(raw, PagePermissions, 7, 6, ap);  // Access permissions
  DEF_SUBBIT(raw, 5, ns);                             // Non-secure
  DEF_SUBFIELD(raw, 4, 2, attr_indx);                 // Memory attributes index
};

// ARM page table entry.
//
// The type contains three sub-types `PageDescriptor`, `BlockDescriptor`
// and `TableDescriptor` for page descriptors, block descriptors (i.e.,
// large pages), and table descriptors respectively.
//
// A PageTableEntry can be converted into the appropriate type using
// `as_page`, `as_block`, or `as_table`.
//
// Typical usage will be as follows:
//
//   // Given a page table entry "pte" assumed to be a block descriptor, get
//   // its address
//   PageTableEntry pte = ...;
//   ZX_ASSERT(pte.type() == PageTableEntryType::kBlockDescriptor);
//   uint64_t address =  pte.as_block().address();
//
//   // Update the address to something else.
//   pte.as_block().set_address(...);
//
//   // Create a new PageDescriptor PTE:
//   PageTableEntry page_entry = PageTableEntry{}
//     .set_present(true)
//     .ToPageDescriptor()   // sets `type` and converts to a PageDescriptor.
//     .set_upper_attrs(...)
//     .set_addr(...);
//
// [arm/v8]: D5.3.1 VMSAv8-64 translation table level 0, level 1, and level 2 descriptor formats.
// [arm/v8]: D5.3.2 Armv8 translation table level 3 descriptor formats
struct alignas(sizeof(uint64_t)) PageTableEntry {
  // Bit definitions when this PageTableEntry is a block descriptor.
  struct BlockDescriptor {
    uint64_t raw;

    DEF_UNSHIFTED_SUBFIELD(raw, 63, 50, raw_upper_attrs);
    // Bits [49:48] reserved.
    DEF_UNSHIFTED_SUBFIELD(raw, 47, 21, address);
    // Bits [20:17] reserved.
    DEF_SUBBIT(raw, 16, nt);
    // Bits [15:12] reserved.
    DEF_UNSHIFTED_SUBFIELD(raw, 11, 2, raw_lower_attrs);
    // Bit 1 defined as 0.
    DEF_SUBBIT(raw, 0, present);

    // Getters/setters for strongly typed upper/lower attribute fields.
    constexpr PteUpperAttrs upper_attrs() const { return PteUpperAttrs{raw_upper_attrs()}; }
    constexpr void set_upper_attrs(PteUpperAttrs attrs) { set_raw_upper_attrs(attrs.raw); }
    constexpr PteLowerAttrs lower_attrs() const { return PteLowerAttrs{raw_lower_attrs()}; }
    constexpr void set_lower_attrs(PteLowerAttrs attrs) { set_raw_lower_attrs(attrs.raw); }

    operator PageTableEntry() { return PageTableEntry{raw}; }  // NOLINT
  };

  struct PageDescriptor {
    uint64_t raw;

    DEF_UNSHIFTED_SUBFIELD(raw, 63, 50, raw_upper_attrs);
    // Bits [49:48] reserved.
    DEF_UNSHIFTED_SUBFIELD(raw, 47, 12, address);
    DEF_UNSHIFTED_SUBFIELD(raw, 11, 2, raw_lower_attrs);
    // Bit 1 defined as 0.
    DEF_SUBBIT(raw, 0, present);

    // Getters/setters for strongly typed upper/lower attribute fields.
    constexpr PteUpperAttrs upper_attrs() const { return PteUpperAttrs{raw_upper_attrs()}; }
    constexpr void set_upper_attrs(PteUpperAttrs attrs) { set_raw_upper_attrs(attrs.raw); }
    constexpr PteLowerAttrs lower_attrs() const { return PteLowerAttrs{raw_lower_attrs()}; }
    constexpr void set_lower_attrs(PteLowerAttrs attrs) { set_raw_lower_attrs(attrs.raw); }

    operator PageTableEntry() { return PageTableEntry{raw}; }  // NOLINT
  };

  // Bit definitions when this PageTableEntry is a table descriptor.
  struct TableDescriptor {
    uint64_t raw;

    DEF_SUBBIT(raw, 63, ns);        // Non-secure
    DEF_SUBFIELD(raw, 62, 61, ap);  // Access permission
    DEF_SUBBIT(raw, 60, xn);        // Execute never
    DEF_SUBBIT(raw, 59, pxn);       // Privileged execute never
    // Bits [58:51] ignored.
    // Bits [50:48] reserved.
    DEF_UNSHIFTED_SUBFIELD(raw, 47, 12, address);  // Physical address of the table
    // Bits [11:2] ignored.
    // Bit 1 defined as 1.
    DEF_SUBBIT(raw, 0, present);

    operator PageTableEntry() { return PageTableEntry{raw}; }  // NOLINT
  };

  // Raw data.
  union {
    uint64_t raw;
    BlockDescriptor block;
    TableDescriptor table;
    PageDescriptor page;
  };

  // Common fields.
  DEF_ENUM_SUBFIELD(raw, PageTableEntryType, 1, 1, type);
  DEF_SUBBIT(raw, 0, present);

  // Accessors for subtypes.
  constexpr BlockDescriptor& as_block() { return block; }
  constexpr const BlockDescriptor& as_block() const { return block; }
  constexpr TableDescriptor& as_table() { return table; }
  constexpr const TableDescriptor& as_table() const { return table; }
  constexpr PageDescriptor& as_page() { return page; }
  constexpr const PageDescriptor& as_page() const { return page; }

  // Set this entry as a particular type of entry, and return the type.
  constexpr BlockDescriptor& ToBlockDescriptor() {
    set_type(PageTableEntryType::kBlockDescriptor);
    return as_block();
  }
  constexpr PageDescriptor& ToPageDescriptor() {
    set_type(PageTableEntryType::kTableOrPageDescriptor);
    return as_page();
  }
  constexpr TableDescriptor& ToTableDescriptor() {
    set_type(PageTableEntryType::kTableOrPageDescriptor);
    return as_table();
  }

  // Basic equality/inequality operators.
  friend bool operator==(const PageTableEntry& a, const PageTableEntry& b) {
    return a.raw == b.raw;
  }
  friend bool operator!=(const PageTableEntry& a, const PageTableEntry& b) { return !(a == b); }

  // Convenience function for creating Block/Page/Table entries marked
  // "present" at a given address.
  static constexpr BlockDescriptor BlockAtAddress(Paddr addr) {
    // We have to extract `.raw` and insert it into a `BlockDescriptor`
    // because `constexpr` doesn't allow us to access different fields
    // in a union, even though the C++ standard allows us to in this
    // case.
    return PageTableEntry::BlockDescriptor{
        PageTableEntry{}.set_type(PageTableEntryType::kBlockDescriptor).set_present(1).raw}
        .set_address(addr.value());
  }
  static constexpr PageDescriptor PageAtAddress(Paddr addr) {
    return PageTableEntry::PageDescriptor{
        PageTableEntry{}.set_type(PageTableEntryType::kTableOrPageDescriptor).set_present(1).raw}
        .set_address(addr.value());
  }
  static constexpr TableDescriptor TableAtAddress(Paddr addr) {
    return PageTableEntry::TableDescriptor{
        PageTableEntry{}.set_type(PageTableEntryType::kTableOrPageDescriptor).set_present(1).raw}
        .set_address(addr.value());
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

// Describes a particular layout of an ARM64 page table.
struct PageTableLayout {
  // Construct a PageTableLayout from the settings in the given TCR config register for
  // the TTBR0 layout (i.e., low virtual addresses).
  static PageTableLayout FromTcrTtbr0(const arch::ArmTcrEl1& tcr);

  // Number of bits of a virtual address each level translates.
  constexpr uint64_t TranslationBitsPerLevel() const {
    return page_table::arm64::TranslationBitsPerLevel(granule_size);
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

}  // namespace page_table::arm64

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_MMU_H_
