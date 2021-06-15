// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lookup.h"

#include <lib/page-table/types.h>
#include <stdlib.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mmu.h"
#include "testing/test_util.h"

namespace page_table::x86 {
namespace {

// GMock matcher to check that a std::optional<LookupResult> evaluates to the given
// physical address.
MATCHER_P(MapsToPaddr, paddr, "") { return arg.has_value() && arg->phys_addr == Paddr(paddr); }

TEST(LookupPage, LookupZero) {
  TestMemoryManager allocator;
  PageTableNode nodes[4];

  // Construct the 4 kiB page at vaddr 0.
  nodes[0].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/3, false)
                      .set_child_paddr(PaddrOf(&nodes[1]).value()));
  nodes[1].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/2, false)
                      .set_child_paddr(PaddrOf(&nodes[2]).value()));
  nodes[2].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/1, false)
                      .set_child_paddr(PaddrOf(&nodes[3]).value()));
  nodes[3].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/0, true)
                      .set_page_paddr(/*level=*/0, 0xabcd'e000));

  // Ensure the returned physical addresses are valid.
  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0x0)), MapsToPaddr(0xabcd'e000));
  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0x123)), MapsToPaddr(0xabcd'e123));
  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0xfff)), MapsToPaddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, nodes, Vaddr(0x1000)), std::nullopt);

  // Ensure that the returned level and PTE values are correct.
  std::optional<LookupResult> result = LookupPage(allocator, nodes, Vaddr(0x0));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->phys_addr, Paddr(0xabcd'e000));
  EXPECT_EQ(result->level, 0);
  EXPECT_EQ(result->entry, nodes[3].at(0));
}

TEST(LookupPage, LookupLast) {
  TestMemoryManager allocator;
  PageTableNode nodes[4];

  // Construct the 4 kiB page at vaddr 0xffff'ffff'ffff'f000.
  nodes[0].set(kEntriesPerNode - 1, PageTableEntry{}
                                        .set_present(1)
                                        .set_is_page(/*level=*/3, false)
                                        .set_child_paddr(PaddrOf(&nodes[1]).value()));
  nodes[1].set(kEntriesPerNode - 1, PageTableEntry{}
                                        .set_present(1)
                                        .set_is_page(/*level=*/2, false)
                                        .set_child_paddr(PaddrOf(&nodes[2]).value()));
  nodes[2].set(kEntriesPerNode - 1, PageTableEntry{}
                                        .set_present(1)
                                        .set_is_page(/*level=*/1, false)
                                        .set_child_paddr(PaddrOf(&nodes[3]).value()));
  nodes[3].set(kEntriesPerNode - 1, PageTableEntry{}
                                        .set_present(1)
                                        .set_is_page(/*level=*/0, true)
                                        .set_page_paddr(/*level=*/0, 0xabcd'e000));

  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0xffff'ffff'ffff'f000)), MapsToPaddr(0xabcd'e000));
  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0xffff'ffff'ffff'f123)), MapsToPaddr(0xabcd'e123));
  EXPECT_THAT(LookupPage(allocator, nodes, Vaddr(0xffff'ffff'ffff'ffff)), MapsToPaddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, nodes, Vaddr(0xffff'ffff'ffff'efff)), std::nullopt);
}

TEST(LookupPage, LookupLargePages) {
  TestMemoryManager allocator;
  PageTableNode nodes[4];

  // Construct the 4 kiB page at vaddr 0.
  nodes[0].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/3, false)
                      .set_child_paddr(PaddrOf(&nodes[1]).value()));
  nodes[1].set(0, PageTableEntry{}
                      .set_present(1)
                      .set_is_page(/*level=*/2, true)
                      .set_page_paddr(/*level=*/2, 0x000a'bcde'c000'0000));

  // Expect the lookup to return the correct address, level, and PTE.
  std::optional<LookupResult> result = LookupPage(allocator, nodes, Vaddr(0x0000'0000u));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0x000a'bcde'c000'0000));
  EXPECT_EQ(result->level, 2);
  EXPECT_EQ(result->entry, nodes[1].at(0));

  // Also check the last byte of the page.
  result = LookupPage(allocator, nodes, Vaddr(0x3fff'ffffu));
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(result, MapsToPaddr(0x000a'bcde'ffff'ffff));
  EXPECT_EQ(result->level, 2);
  EXPECT_EQ(result->entry, nodes[1].at(0));
}

TEST(MapPage, SingleMapping) {
  TestMemoryManager allocator;
  PageTableNode pml4;
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0000'1234'5678'9000), Paddr(0x0001'2345'6789'a000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_OK);
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x0000'1234'5678'9000)),
              MapsToPaddr(0x0001'2345'6789'a000u));
}

TEST(MapPage, ReplaceMapping) {
  TestMemoryManager allocator;
  PageTableNode pml4{};

  // Attempt to map the same vaddr twice.
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0), Paddr(0xaaaa'0000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_OK);
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0), Paddr(0xbbbb'0000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_ERR_ALREADY_EXISTS);

  // Should still have the original mapping.
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0)), MapsToPaddr(0xaaaa'0000));
}

TEST(MapPage, MultipleMappings) {
  TestMemoryManager allocator;
  PageTableNode pml4{};
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0000), Paddr(0xaaaa'0000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_OK);
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x1000), Paddr(0xbbbb'0000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_OK);
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x0000)), MapsToPaddr(0xaaaa'0000));
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x1000)), MapsToPaddr(0xbbbb'0000));
}

TEST(MapPage, LargePage) {
  TestMemoryManager allocator;
  PageTableNode pml4{};

  // Map in a 2MiB page.
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0000), Paddr(0xaaa0'0000),
                    /*page_size=*/PageSize::k2MiB),
            ZX_OK);

  // We shouldn't be able to map in a smaller page in the middle.
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x1000), Paddr(0xbbbb'0000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_ERR_ALREADY_EXISTS);

  // We should be able to lookup different parts of the page.
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x0000)), MapsToPaddr(0xaaa0'0000u));
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x1000)), MapsToPaddr(0xaaa0'1000u));
  EXPECT_THAT(LookupPage(allocator, &pml4, Vaddr(0x1f'ffff)), MapsToPaddr(0xaabf'ffffu));
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x20'0000)), std::nullopt);
}

}  // namespace
}  // namespace page_table::x86
