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

namespace page_table::x86 {

// Return a "physical address" (actually just the virtual address) of the given object.
template <typename T>
constexpr Paddr PaddrOf(T* object) {
  return Paddr(reinterpret_cast<uint64_t>(object));
}

// An allocator that just uses new/delete to allocate, and assumes a 1:1
// mapping from physical addresses to host virtual addresses.
class TestMemoryManager final : public MemoryManager {
 public:
  std::byte* Allocate(size_t size, size_t alignment) final {
    // Allocate aligned memory.
    void* result;
    if (int error = posix_memalign(&result, alignment, size); error != 0) {
      return nullptr;
    }
    ZX_ASSERT(result != nullptr);

    // Track the allocation.
    allocations_.push_back(result);

    return static_cast<std::byte*>(result);
  }

  Paddr PtrToPhys(std::byte* ptr) final { return PaddrOf(ptr); }

  std::byte* PhysToPtr(Paddr phys) final { return reinterpret_cast<std::byte*>(phys.value()); }

  ~TestMemoryManager() {
    for (void* allocation : allocations_) {
      free(allocation);
    }
  }

 private:
  // Tracks allocations so that we can free them when the test finishes.
  std::vector<void*> allocations_;
};

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

  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0x0)), Paddr(0xabcd'e000));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0x123)), Paddr(0xabcd'e123));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0xfff)), Paddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0x1000)), std::nullopt);
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

  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0xffff'ffff'ffff'f000)), Paddr(0xabcd'e000));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0xffff'ffff'ffff'f123)), Paddr(0xabcd'e123));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0xffff'ffff'ffff'ffff)), Paddr(0xabcd'efff));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0xffff'ffff'ffff'efff)), std::nullopt);
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

  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0x0000'0000u)), Paddr(0x000a'bcde'c000'0000));
  EXPECT_EQ(LookupPage(allocator, &nodes[0], Vaddr(0x3fff'ffffu)), Paddr(0x000a'bcde'ffff'ffff));
}

TEST(MapPage, SingleMapping) {
  TestMemoryManager allocator;
  PageTableNode pml4;
  EXPECT_EQ(MapPage(allocator, &pml4, Vaddr(0x0000'1234'5678'9000), Paddr(0x0001'2345'6789'a000),
                    /*page_size=*/PageSize::k4KiB),
            ZX_OK);
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x0000'1234'5678'9000)),
            Paddr(0x0001'2345'6789'a000u));
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
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0)), Paddr(0xaaaa'0000));
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
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x0000)), Paddr(0xaaaa'0000));
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x1000)), Paddr(0xbbbb'0000));
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
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x0000)), Paddr(0xaaa0'0000u));
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x1000)), Paddr(0xaaa0'1000u));
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x1f'ffff)), Paddr(0xaabf'ffffu));
  EXPECT_EQ(LookupPage(allocator, &pml4, Vaddr(0x20'0000)), std::nullopt);
}

}  // namespace page_table::x86
