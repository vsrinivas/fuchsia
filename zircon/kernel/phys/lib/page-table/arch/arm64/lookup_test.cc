// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lookup.h"

#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/types.h>
#include <stdlib.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "testing/test_util.h"

namespace page_table::arm64 {
namespace {

// Storage for a PageTableNode.
//
// On ARM64, the side of nodes in the page table depends on the configured
// granule size. This type takes the granule size as a template parameter,
// allowing static allocation of nodes where the granule size is known at
// compile time.
template <GranuleSize Size>
class PageTableNodeStorage {
 public:
  // Get/set the given index.
  PageTableEntry at(size_t index) { return ptr().at(index); }
  void set(size_t index, PageTableEntry entry) { ptr().set(index, entry); }

  // Return a PageTableNode to this object.
  PageTableNode ptr() { return PageTableNode(entries_, Size); }

 private:
  PageTableEntry entries_[PageTableEntries(Size)] = {};
} __ALIGNED(GranuleBytes(Size));

static_assert(std::is_standard_layout<PageTableNodeStorage<GranuleSize::k4KiB>>::value);
static_assert(sizeof(PageTableNodeStorage<GranuleSize::k4KiB>) == GranuleBytes(GranuleSize::k4KiB));

// Standard layout: 4 kiB granule, full 48-bits of virtual address space.
constexpr PageTableLayout kDefaultLayout = {
    .granule_size = GranuleSize::k4KiB,
    .region_size_bits = 48,
};

// GMock matcher to check that a std::optional<LookupPageResult> evaluates to the given
// physical address.
MATCHER_P(MapsToPaddr, paddr, "") { return arg.has_value() && arg->phys_addr == Paddr(paddr); }

// Granule-sized page table nodes.

TEST(Arm64LookupPage, LookupZero) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> nodes[4];
  PageTableNode table = nodes[0].ptr();

  // Construct the 4 kiB page at vaddr 0.
  nodes[0].set(0, PageTableEntry::TableAtAddress(PaddrOf(&nodes[1])));
  nodes[1].set(0, PageTableEntry::TableAtAddress(PaddrOf(&nodes[2])));
  nodes[2].set(0, PageTableEntry::TableAtAddress(PaddrOf(&nodes[3])));
  nodes[3].set(0, PageTableEntry::PageAtAddress(Paddr(0xabcd'e000)));

  // Ensure the returned physical addresses are valid.
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0x0)), MapsToPaddr(0xabcd'e000));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0x123)), MapsToPaddr(0xabcd'e123));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0xfff)), MapsToPaddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, kDefaultLayout, table, Vaddr(0x1000)), std::nullopt);

  // Ensure that the returned level and PTE values are correct.
  std::optional<LookupPageResult> result = LookupPage(allocator, kDefaultLayout, table, Vaddr(0x0));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->phys_addr, Paddr(0xabcd'e000));
  EXPECT_EQ(result->page_size_bits, 12u);
  EXPECT_EQ(result->entry, nodes[3].at(0));
}

TEST(Arm64LookupPage, LookupLast) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> nodes[4];
  PageTableNode table = nodes[0].ptr();

  // Construct the 4 kiB page at vaddr 0x0fff'ffff'ffff'f000.
  nodes[0].set(PageTableEntries(GranuleSize::k4KiB) - 1,
               PageTableEntry::TableAtAddress(PaddrOf(&nodes[1])));
  nodes[1].set(PageTableEntries(GranuleSize::k4KiB) - 1,
               PageTableEntry::TableAtAddress(PaddrOf(&nodes[2])));
  nodes[2].set(PageTableEntries(GranuleSize::k4KiB) - 1,
               PageTableEntry::TableAtAddress(PaddrOf(&nodes[3])));
  nodes[3].set(PageTableEntries(GranuleSize::k4KiB) - 1,
               PageTableEntry::PageAtAddress(Paddr(0xabcd'e000)));

  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0xffff'ffff'f000)),
              MapsToPaddr(0xabcd'e000));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0xffff'ffff'f123)),
              MapsToPaddr(0xabcd'e123));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, table, Vaddr(0xffff'ffff'ffff)),
              MapsToPaddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, kDefaultLayout, table, Vaddr(0xffff'ffff'efff)), std::nullopt);
}

TEST(Arm64LookupPage, LookupLargePages) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> nodes[3];
  PageTableNode table = nodes[0].ptr();

  // Construct a 2 MiB page at vaddr 0.
  nodes[0].set(0, PageTableEntry::TableAtAddress(PaddrOf(&nodes[1])));
  nodes[1].set(0, PageTableEntry::TableAtAddress(PaddrOf(&nodes[2])));
  nodes[2].set(0, PageTableEntry::BlockAtAddress(Paddr(0xffab'cde0'0000)));

  // Expect the lookup to return the correct address, level, and PTE.
  std::optional<LookupPageResult> result = LookupPage(allocator, kDefaultLayout, table, Vaddr(0));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0xffab'cde0'0000));
  EXPECT_EQ(result->page_size_bits, 21u);

  // Also check the last byte of the page.
  result = LookupPage(allocator, kDefaultLayout, table, Vaddr(0x1f'ffffu));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0xffab'cdff'ffff));
}

TEST(Arm64LookupPage, Lookup16KiBGranule) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k16KiB> nodes[4];
  PageTableNode table = nodes[0].ptr();

  // Create a layout with 16 kiB granules.
  constexpr PageTableLayout layout = {
      .granule_size = GranuleSize::k16KiB,
      .region_size_bits = 48,
  };

  // Construct a 16 kiB page at vaddr 0x8010'0200'4000 (corresponding to the first
  // slot of each node).
  nodes[0].set(1, PageTableEntry::TableAtAddress(PaddrOf(&nodes[1])));
  nodes[1].set(1, PageTableEntry::TableAtAddress(PaddrOf(&nodes[2])));
  nodes[2].set(1, PageTableEntry::TableAtAddress(PaddrOf(&nodes[3])));
  nodes[3].set(1, PageTableEntry::PageAtAddress(Paddr(0xabcd'efff'c000)));

  // Expect the lookup to return the correct address, level, and PTE.
  std::optional<LookupPageResult> result =
      LookupPage(allocator, layout, table, Vaddr(0x8010'0200'4000));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0xabcd'efff'c000));
  EXPECT_EQ(result->page_size_bits, 14u);
}

TEST(Arm64LookupPage, Lookup64KiBGranule) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k64KiB> nodes[3];

  // Create a layout with 64 kiB granules.
  constexpr PageTableLayout layout = {
      .granule_size = GranuleSize::k64KiB,
      .region_size_bits = 48,
  };

  // Construct a 64 kiB page at vaddr 0x0400'2001'0000 (corresponding to the
  // first slot of each node).
  nodes[0].set(1, PageTableEntry::TableAtAddress(PaddrOf(&nodes[1])));
  nodes[1].set(1, PageTableEntry::TableAtAddress(PaddrOf(&nodes[2])));
  nodes[2].set(1, PageTableEntry::PageAtAddress(Paddr(0xabcd'efff'0000)));

  // Expect the lookup to return the correct address, level, and PTE.
  PageTableNode table = nodes[0].ptr();
  std::optional<LookupPageResult> result =
      LookupPage(allocator, layout, table, Vaddr(0x0400'2001'0000));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0xabcd'efff'0000));
  EXPECT_EQ(result->page_size_bits, 16u);
}

TEST(Arm64LookupPage, SmallRegionSize) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k16KiB> root;

  // Create a layout with 4 kiB granules.
  constexpr PageTableLayout layout = {
      .granule_size = GranuleSize::k4KiB,
      .region_size_bits = 16,  // 64 kiB of virtual address space
  };
  static_assert(layout.NumLevels() == 1);

  // Construct the 4 kiB page at vaddr 0x1000.
  root.set(1, PageTableEntry::PageAtAddress(Paddr(0xffff'eeee'f000)));

  // Ensure the returned physical addresses are valid.
  EXPECT_EQ(LookupPage(allocator, layout, root.ptr(), Vaddr(0x0)), std::nullopt);
  EXPECT_THAT(LookupPage(allocator, layout, root.ptr(), Vaddr(0x1000)),
              MapsToPaddr(0xffff'eeee'f000));
  EXPECT_EQ(LookupPage(allocator, layout, root.ptr(), Vaddr(0x2000)), std::nullopt);
  EXPECT_EQ(LookupPage(allocator, layout, root.ptr(), Vaddr(0x10000)), std::nullopt);
}

TEST(Arm64MapPage, SingleMapping) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> root;

  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1234'5678'9000),
                    Paddr(0x1234'5678'9000), PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_OK);
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1234'5678'9000)),
              MapsToPaddr(0x1234'5678'9000u));
}

TEST(Arm64MapPage, ReplaceMapping) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> root{};

  // Attempt to map the same vaddr twice.
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0), Paddr(0xaaaa'0000),
                    PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_OK);
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0), Paddr(0xbbbb'0000),
                    PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_ERR_ALREADY_EXISTS);

  // Should still have the original mapping.
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0)),
              MapsToPaddr(0xaaaa'0000));
}

TEST(Arm64MapPage, MultipleMappings) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> root{};

  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0000), Paddr(0xaaaa'0000),
                    PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_OK);
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1000), Paddr(0xbbbb'0000),
                    PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_OK);
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0000)),
              MapsToPaddr(0xaaaa'0000));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1000)),
              MapsToPaddr(0xbbbb'0000));
}

TEST(Arm64MapPage, LargePage) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> root{};

  // Map in a 2MiB page.
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0000), Paddr(0xaaa0'0000),
                    PageSize::k2MiB, CacheAttributes::kNormal),
            ZX_OK);

  // We shouldn't be able to map in a smaller page in the middle.
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1000), Paddr(0xbbbb'0000),
                    PageSize::k4KiB, CacheAttributes::kNormal),
            ZX_ERR_ALREADY_EXISTS);

  // We should be able to lookup different parts of the page.
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0000)),
              MapsToPaddr(0xaaa0'0000u));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1000)),
              MapsToPaddr(0xaaa0'1000u));
  EXPECT_THAT(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x1f'ffff)),
              MapsToPaddr(0xaabf'ffffu));
  EXPECT_EQ(LookupPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x20'0000)), std::nullopt);
}

TEST(Arm64MapPage, BadPageSize) {
  TestMemoryManager allocator;
  PageTableNodeStorage<GranuleSize::k4KiB> root{};

  // Map in a 16 kiB page, which isn't valid with 4 kiB granules.
  EXPECT_EQ(MapPage(allocator, kDefaultLayout, root.ptr(), Vaddr(0x0000), Paddr(0xaaa0'0000),
                    PageSize::k16KiB, CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace page_table::arm64
