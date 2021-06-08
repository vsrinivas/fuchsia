// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/page-table/arch/x86/builder.h>
#include <lib/page-table/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lookup.h"
#include "mmu.h"
#include "zircon/kernel/phys/lib/page-table/testing/test_util.h"

namespace page_table::x86 {
namespace {

// Return a CPUID that contains all features used by the builder.
auto FullFeatureCpuid() {
  return arch::testing::FakeCpuidIo(arch::testing::X86Microprocessor::kIntelXeonE5_2690_V4);
}

// Return a CPUID that does not support 1 GiB page mappins.
auto No1GibMappingsCpuid() {
  return arch::testing::FakeCpuidIo(arch::testing::X86Microprocessor::kIntelAtom330);
}

TEST(Builder, Empty) {
  TestMemoryManager allocator;

  // Create an empty builder.
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
  ASSERT_TRUE(builder.has_value());

  // Lookups won't resolve any pages, but should still succeed.
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0)), std::nullopt);
}

TEST(Builder, InvalidArgs) {
  TestMemoryManager allocator;
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
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
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(
      builder->MapRegion(Vaddr(0), Paddr(0xaaaa'0000), kPageSize4KiB, CacheAttributes::kNormal),
      ZX_OK);

  // Ensure we can lookup the page.
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0x0))->phys_addr,
            Paddr(0xaaaa'0000u));
}

TEST(Builder, MultiplePages) {
  TestMemoryManager allocator;
  constexpr int kNumPages = 13;

  // Create a builder, and map in a range of pages.
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0xaaaa'0000), kPageSize4KiB * kNumPages,
                               CacheAttributes::kNormal),
            ZX_OK);

  // Ensure we can look up the pages.
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0x0000))->phys_addr,
            Paddr(0xaaaa'0000));
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0x1000))->phys_addr,
            Paddr(0xaaaa'1000));
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0xc000))->phys_addr,
            Paddr(0xaaaa'c000));
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), Vaddr(0xd000)), std::nullopt);
}

TEST(Builder, LastPage) {
  TestMemoryManager allocator;
  constexpr Vaddr kLastPage = Vaddr(-kPageSize4KiB);

  // Create a builder, and map a single page.
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(
      builder->MapRegion(kLastPage, Paddr(0xaaaa'0000), kPageSize4KiB, CacheAttributes::kNormal),
      ZX_OK);

  // Ensure we can lookup the page.
  EXPECT_EQ(LookupPage(allocator, builder->root_node(), kLastPage)->phys_addr, Paddr(0xaaaa'0000u));
}

TEST(Builder, LargePage) {
  TestMemoryManager allocator;

  // Create a builder, and map a large region with 1:1 phys/virt.
  std::optional builder = AddressSpaceBuilder::Create(allocator, FullFeatureCpuid());
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0), kPageSize1GiB * 4, CacheAttributes::kNormal),
            ZX_OK);

  // Lookup an address in the range, and ensure that large pages were used to construct
  // the entries.
  std::optional<LookupResult> result = LookupPage(allocator, builder->root_node(), Vaddr(0x1234));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->entry.is_page(result->level));
  EXPECT_EQ(result->level, 2);  // 1 GiB
}

TEST(Builder, DisableGigabyteMappings) {
  TestMemoryManager allocator;

  // Create a builder, and map a large region with 1:1 phys/virt.
  std::optional builder = AddressSpaceBuilder::Create(allocator, No1GibMappingsCpuid());
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->MapRegion(Vaddr(0), Paddr(0), kPageSize1GiB * 4, CacheAttributes::kNormal),
            ZX_OK);

  // Lookup an address in the range, and ensure that 2MiB pages were used to construct
  // the entries.
  std::optional<LookupResult> result = LookupPage(allocator, builder->root_node(), Vaddr(0x1234));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->entry.is_page(result->level));
  EXPECT_EQ(result->level, 1);  // 2 MiB.
}

}  // namespace
}  // namespace page_table::x86
