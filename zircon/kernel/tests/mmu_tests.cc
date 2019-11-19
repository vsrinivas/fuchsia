// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <arch/mmu.h>
#include <fbl/auto_call.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>

#ifdef __x86_64__
#include <arch/x86/mmu.h>
#define PGTABLE_L1_SHIFT PDP_SHIFT
#define PGTABLE_L2_SHIFT PD_SHIFT
#else
#define PGTABLE_L1_SHIFT MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 1)
#define PGTABLE_L2_SHIFT MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 2)
#endif

static bool test_large_unaligned_region() {
  BEGIN_TEST;
  ArchVmAspace aspace;
  vaddr_t base = 1UL << 20;
  size_t size = (1UL << 47) - base - (1UL << 20);
  zx_status_t err = aspace.Init(1UL << 20, size, 0);
  EXPECT_EQ(err, ZX_OK, "init aspace");

  const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

  // We want our region to be misaligned by at least a page, and for
  // it to straddle the PDP.
  vaddr_t va = (1UL << PGTABLE_L1_SHIFT) - (1UL << PGTABLE_L2_SHIFT) + 2 * PAGE_SIZE;
  // Make sure alloc_size is less than 1 PD page, to exercise the
  // non-terminal code path.
  static const size_t alloc_size = (1UL << PGTABLE_L2_SHIFT) - PAGE_SIZE;

  // Map a single page to force the lower PDP of the target region
  // to be created
  size_t mapped;
  err = aspace.MapContiguous(va - 3 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
  EXPECT_EQ(err, ZX_OK, "map single page");
  EXPECT_EQ(mapped, 1u, "map single page");

  // Map the last page of the region
  err = aspace.MapContiguous(va + alloc_size - PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
  EXPECT_EQ(err, ZX_OK, "map last page");
  EXPECT_EQ(mapped, 1u, "map single page");

  paddr_t pa;
  uint flags;
  err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
  EXPECT_EQ(err, ZX_OK, "last entry is mapped");

  // Attempt to unmap the target region (analogous to unmapping a demand
  // paged region that has only had its last page touched)
  size_t unmapped;
  err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
  EXPECT_EQ(err, ZX_OK, "unmap unallocated region");
  EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");

  err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
  EXPECT_EQ(err, ZX_ERR_NOT_FOUND, "last entry is not mapped anymore");

  // Unmap the single page from earlier
  err = aspace.Unmap(va - 3 * PAGE_SIZE, 1, &unmapped);
  EXPECT_EQ(err, ZX_OK, "unmap single page");
  EXPECT_EQ(unmapped, 1u, "unmap unallocated region");

  err = aspace.Destroy();
  EXPECT_EQ(err, ZX_OK, "destroy aspace");

  END_TEST;
}

static bool test_large_unaligned_region_without_map() {
  BEGIN_TEST;

  {
    ArchVmAspace aspace;
    vaddr_t base = 1UL << 20;
    size_t size = (1UL << 47) - base - (1UL << 20);
    zx_status_t err = aspace.Init(1UL << 20, size, 0);
    EXPECT_EQ(err, ZX_OK, "init aspace");

    const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

    // We want our region to be misaligned by a page, and for it to
    // straddle the PDP
    vaddr_t va = (1UL << PGTABLE_L1_SHIFT) - (1UL << PGTABLE_L2_SHIFT) + PAGE_SIZE;
    // Make sure alloc_size is bigger than 1 PD page, to exercise the
    // non-terminal code path.
    static const size_t alloc_size = 3UL << PGTABLE_L2_SHIFT;

    // Map a single page to force the lower PDP of the target region
    // to be created
    size_t mapped;
    err = aspace.MapContiguous(va - 2 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
    EXPECT_EQ(err, ZX_OK, "map single page");
    EXPECT_EQ(mapped, 1u, "map single page");

    // Attempt to unmap the target region (analogous to unmapping a demand
    // paged region that has not been touched)
    size_t unmapped;
    err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
    EXPECT_EQ(err, ZX_OK, "unmap unallocated region");
    EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");

    // Unmap the single page from earlier
    err = aspace.Unmap(va - 2 * PAGE_SIZE, 1, &unmapped);
    EXPECT_EQ(err, ZX_OK, "unmap single page");
    EXPECT_EQ(unmapped, 1u, "unmap single page");

    err = aspace.Destroy();
    EXPECT_EQ(err, ZX_OK, "destroy aspace");
  }

  END_TEST;
}

static bool test_large_region_protect() {
  BEGIN_TEST;

  static const vaddr_t va = 1UL << PGTABLE_L1_SHIFT;
  // Force a large page.
  static const size_t alloc_size = 1UL << PGTABLE_L2_SHIFT;
  static const vaddr_t alloc_end = va + alloc_size;

  vaddr_t target_vaddrs[] = {
      va,
      va + PAGE_SIZE,
      va + 2 * PAGE_SIZE,
      alloc_end - 3 * PAGE_SIZE,
      alloc_end - 2 * PAGE_SIZE,
      alloc_end - PAGE_SIZE,
  };

  for (unsigned i = 0; i < fbl::count_of(target_vaddrs); i++) {
    ArchVmAspace aspace;
    vaddr_t base = 1UL << 20;
    size_t size = (1UL << 47) - base - (1UL << 20);
    zx_status_t err = aspace.Init(1UL << 20, size, 0);
    EXPECT_EQ(err, ZX_OK, "init aspace");

    const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

    size_t mapped;
    err = aspace.MapContiguous(va, 0, alloc_size / PAGE_SIZE, arch_rw_flags, &mapped);
    EXPECT_EQ(err, ZX_OK, "map large page");
    EXPECT_EQ(mapped, 512u, "map large page");

    err = aspace.Protect(target_vaddrs[i], 1, ARCH_MMU_FLAG_PERM_READ);
    EXPECT_EQ(err, ZX_OK, "protect single page");

    for (unsigned j = 0; j < fbl::count_of(target_vaddrs); j++) {
      uint retrieved_flags = 0;
      paddr_t pa;
      EXPECT_EQ(ZX_OK, aspace.Query(target_vaddrs[j], &pa, &retrieved_flags));
      EXPECT_EQ(target_vaddrs[j] - va, pa);

      EXPECT_EQ(i == j ? ARCH_MMU_FLAG_PERM_READ : arch_rw_flags, retrieved_flags);
    }

    err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &mapped);
    EXPECT_EQ(err, ZX_OK, "unmap large page");
    EXPECT_EQ(mapped, 512u, "unmap large page");
    err = aspace.Destroy();
    EXPECT_EQ(err, ZX_OK, "destroy aspace");
  }

  END_TEST;
}

static list_node node = LIST_INITIAL_VALUE(node);
zx_status_t test_page_alloc_fn(uint unused, vm_page** p, paddr_t* pa) {
  if (list_is_empty(&node)) {
    return ZX_ERR_NO_MEMORY;
  }
  vm_page_t* page = list_remove_head_type(&node, vm_page_t, queue_node);
  if (p) {
    *p = page;
  }
  if (pa) {
    *pa = page->paddr();
  }
  return ZX_OK;
}

static bool test_mapping_oom() {
  BEGIN_TEST;

  constexpr uint64_t kMappingPageCount = 8;
  constexpr uint64_t kMappingSize = kMappingPageCount * PAGE_SIZE;
  constexpr vaddr_t kMappingStart = (1UL << PGTABLE_L1_SHIFT) - kMappingSize / 2;

  // Allocate the pages which will be mapped into the test aspace.
  vm_page_t* mapping_pages[kMappingPageCount] = {};
  paddr_t mapping_paddrs[kMappingPageCount] = {};

  auto undo = fbl::MakeAutoCall([&]() {
    for (unsigned i = 0; i < kMappingPageCount; i++) {
      if (mapping_pages[i]) {
        pmm_free_page(mapping_pages[i]);
      }
    }
  });

  for (unsigned i = 0; i < kMappingPageCount; i++) {
    ASSERT_EQ(pmm_alloc_page(0, mapping_pages + i, mapping_paddrs + i), ZX_OK);
  }

  // Try to create the mapping with a limited number of pages available to
  // the aspace. Start with only 1 available and continue until the map operation
  // succeeds without running out of memory.
  bool map_success = false;
  uint64_t avail_mmu_pages = 1;
  while (!map_success) {
    for (unsigned i = 0; i < avail_mmu_pages; i++) {
      vm_page_t* page;
      ASSERT_EQ(pmm_alloc_page(0, &page), ZX_OK, "alloc fail");
      list_add_head(&node, &page->queue_node);
    }

    TestArchVmAspace<test_page_alloc_fn> aspace;
    vaddr_t base = 1UL << 20;
    size_t size = (1UL << 47) - base - (1UL << 20);
    zx_status_t err = aspace.Init(base, size, 0);
    ASSERT_EQ(err, ZX_OK, "init aspace");

    const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

    size_t mapped;
    err = aspace.Map(kMappingStart, mapping_paddrs, kMappingPageCount, arch_rw_flags, &mapped);
    if (err == ZX_OK) {
      map_success = true;
      size_t unmapped;
      EXPECT_EQ(aspace.Unmap(kMappingStart, kMappingPageCount, &unmapped), ZX_OK);
      EXPECT_EQ(unmapped, kMappingPageCount);
    } else {
      // The arm aspace code isn't set up to return ZX_ERR_NO_MEMORY.
      avail_mmu_pages++;
    }

    // Destroying the aspace verifies that everything was cleaned up
    // when the mapping failed part way through.
    err = aspace.Destroy();
    ASSERT_EQ(err, ZX_OK, "destroy aspace");
    ASSERT_TRUE(list_is_empty(&node));
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(mmu_tests)
UNITTEST("create large unaligned region and ensure it can be unmapped", test_large_unaligned_region)
UNITTEST("create large unaligned region without mapping and ensure it can be unmapped",
         test_large_unaligned_region_without_map)
UNITTEST("creating large vm region, and change permissions", test_large_region_protect)
UNITTEST("trigger oom failures when creating a mapping", test_mapping_oom)
UNITTEST_END_TESTCASE(mmu_tests, "mmu", "mmu tests")
