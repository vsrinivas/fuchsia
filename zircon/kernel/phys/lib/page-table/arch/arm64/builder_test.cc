// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/system.h>
#include <lib/page-table/arch/arm64/builder.h>
#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lookup.h"
#include "testing/test_util.h"

namespace page_table::arm64 {
namespace {

constexpr size_t kPageSize4KiB = 4096;

constexpr PageTableLayout kDefaultLayout = PageTableLayout{
    .granule_size = GranuleSize::k4KiB,
    .region_size_bits = 48,
};

TEST(Builder, Empty) {
  TestMemoryManager allocator;

  // Create an empty builder.
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());

  // Lookups won't resolve any pages, but should still succeed.
  EXPECT_EQ(LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0)), std::nullopt);
}

TEST(Builder, InvalidArgs) {
  TestMemoryManager allocator;
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());

  // Unaligned vaddr / paddr.
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(1), kPageSize4KiB, CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(builder->MapRegion(Vaddr(1), Paddr(0), kPageSize4KiB, CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);

  // Size not page aligned.
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0), kPageSize4KiB + 1, CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);

  // Non-canonical address.
  EXPECT_EQ(builder->MapRegion(Vaddr(0xf000'0000'0000'0000), Paddr(0), kPageSize4KiB,
                               CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);

  // Overflow the address space.
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0xffff'ffff'ffff'f000), kPageSize4KiB * 10,
                               CacheAttributes::kNormal),
            ZX_ERR_INVALID_ARGS);
}

TEST(Builder, SinglePage) {
  TestMemoryManager allocator;

  // Create a builder, and map a single page.
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(
      builder->MapRegion(Vaddr(0), Paddr(0xaaaa'0000), kPageSize4KiB, CacheAttributes::kNormal),
      ZX_OK);

  // Ensure we can lookup the page.
  EXPECT_EQ(LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x0))->phys_addr,
            Paddr(0xaaaa'0000u));
}

TEST(Builder, MultiplePages) {
  TestMemoryManager allocator;
  constexpr int kNumPages = 13;

  // Create a builder, and map in a range of pages.
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0xaaaa'0000), kPageSize4KiB * kNumPages,
                               CacheAttributes::kNormal),
            ZX_OK);

  // Ensure we can look up the pages.
  EXPECT_EQ(
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x0000))->phys_addr,
      Paddr(0xaaaa'0000));
  EXPECT_EQ(
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x1000))->phys_addr,
      Paddr(0xaaaa'1000));
  EXPECT_EQ(
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0xc000))->phys_addr,
      Paddr(0xaaaa'c000));
  EXPECT_EQ(LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0xd000)),
            std::nullopt);
}

TEST(Builder, LargePage) {
  TestMemoryManager allocator;

  // Create a builder, and map a large region with 1:1 phys/virt.
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(
      builder->MapRegion(Vaddr(0), Paddr(0), 0x1'0000'0000 /* 4 GiB */, CacheAttributes::kNormal),
      ZX_OK);

  // Lookup an address in the range, and ensure that large pages were used to construct
  // the entries.
  std::optional<LookupPageResult> result =
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x1'2345));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->entry.type() == PageTableEntryType::kBlockDescriptor);
  EXPECT_EQ(result->page_size_bits, PageBits(PageSize::k1GiB));
}

TEST(Builder, LargeGranules) {
  TestMemoryManager allocator;

  // Create a builder, and map a large region with 1:1 phys/virt.
  std::optional builder =
      AddressSpaceBuilder::Create(allocator, PageTableLayout{
                                                 .granule_size = GranuleSize::k64KiB,
                                                 .region_size_bits = 30,
                                             });
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0), 0x10'0000 /* 1 MiB */, CacheAttributes::kNormal),
            ZX_OK);

  // Lookup an address in the range, and ensure that 64 kiB mappings were used.
  std::optional<LookupPageResult> result =
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x1'2345));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->page_size_bits, PageBits(PageSize::k64KiB));
}

TEST(Builder, CacheAttributes) {
  TestMemoryManager allocator;

  // Create a builder, and map a single page uncached, and a single page cached.
  std::optional builder = AddressSpaceBuilder::Create(allocator, kDefaultLayout);
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0), kPageSize4KiB, CacheAttributes::kDevice), ZX_OK);
  EXPECT_EQ(builder->MapRegion(Vaddr(0x10'000), Paddr(0x10'0000), kPageSize4KiB,
                               CacheAttributes::kNormal),
            ZX_OK);

  // Get each page's PTE.
  PageTableEntry device_pte =
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x0))->entry;
  PageTableEntry normal_pte =
      LookupPage(allocator, builder->layout(), builder->root_node(), Vaddr(0x10'000))->entry;

  // Ensure each one has the correct attributes.
  arch::ArmMemoryAttrIndirectionRegister mair =
      AddressSpaceBuilder::GetArmMemoryAttrIndirectionRegister();
  EXPECT_EQ(mair.GetAttribute(device_pte.as_page().lower_attrs().attr_indx()),
            arch::ArmMemoryAttribute::kDevice_nGnRE);
  EXPECT_EQ(mair.GetAttribute(normal_pte.as_page().lower_attrs().attr_indx()),
            arch::ArmMemoryAttribute::kNormalCached);
}

}  // namespace
}  // namespace page_table::arm64
