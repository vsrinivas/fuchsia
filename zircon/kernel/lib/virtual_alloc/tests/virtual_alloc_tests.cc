// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/unittest/unittest.h>
#include <lib/virtual_alloc.h>
#include <lib/zircon-internal/macros.h>

#include <vm/vm_address_region.h>

namespace {

constexpr size_t kTestHeapSize = 16 * MB;
// We pick 2MiB for the heap alignment as this is a large page on many architectures. Having this
// be a large page is not actually necessary for this test coverage, but it serves as some bonus
// mmu testing so we might as well.
constexpr size_t kTestHeapAlignLog2 = 21;
constexpr size_t kAlignPages = 1ul << (kTestHeapAlignLog2 - PAGE_SIZE_SHIFT);

// Helper that checks if there exists any contiguous blocks in the pmm. This can be used by tests to
// attempt to avoid spurious failures.
bool CanExpectContiguous() {
  list_node_t pages = LIST_INITIAL_VALUE(pages);

  paddr_t paddr;
  zx_status_t status = pmm_alloc_contiguous(kAlignPages, 0, kTestHeapAlignLog2, &paddr, &pages);
  if (status == ZX_OK) {
    pmm_free(&pages);
    return true;
  }
  return false;
}

bool RangeEmpty(vaddr_t base, size_t num_pages) {
  for (size_t i = 0; i < num_pages; i++) {
    zx_status_t status =
        VmAspace::kernel_aspace()->arch_aspace().Query(base + i * PAGE_SIZE, nullptr, nullptr);
    if (status == ZX_OK) {
      return false;
    }
  }
  return true;
}

bool RangeContiguous(vaddr_t base, size_t num_pages) {
  ASSERT(num_pages > 0);
  // Get the paddr of the first page.
  paddr_t base_paddr;
  zx_status_t status = VmAspace::kernel_aspace()->arch_aspace().Query(base, &base_paddr, nullptr);
  if (status != ZX_OK) {
    return false;
  }
  // Check all subsequent pages are physically contiguous.
  for (size_t i = 1; i < num_pages; i++) {
    paddr_t paddr;
    status = VmAspace::kernel_aspace()->arch_aspace().Query(base + i * PAGE_SIZE, &paddr, nullptr);
    if (status != ZX_OK) {
      return false;
    }
    if (paddr != base_paddr + i * PAGE_SIZE) {
      return false;
    }
  }
  return true;
}

// Helper to construct and teardown a vmar for testing.
class TestVmar {
 public:
  TestVmar() {
    zx_status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateSubVmar(
        0,  // 0 offset to request random placement
        kTestHeapSize, kTestHeapAlignLog2, VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE,
        "virtual_alloc test", &vmar_);
    ASSERT(status == ZX_OK);
  }
  ~TestVmar() {
    ASSERT(RegionEmpty());
    vmar_->Destroy();
  }
  DISALLOW_COPY_ASSIGN_AND_MOVE(TestVmar);

  bool RegionEmpty() const { return RangeEmpty(vmar_->base(), vmar_->size() / PAGE_SIZE); }

  VmAddressRegion& operator*() const { return *vmar_; }
  VmAddressRegion* operator->() const { return &*vmar_; }

 private:
  fbl::RefPtr<VmAddressRegion> vmar_;
};

// Helper for touching a pages in a virtual address range to ensure they can be accessed without
// faulting. By default a unique value is written to and then read from every page, writing can be
// optionally disabled to validate that the contents of the range have not changed since last time.
bool TouchPages(vaddr_t vaddr, size_t num_pages, bool write = true) {
  for (vaddr_t page_base = vaddr; page_base < vaddr + (num_pages * PAGE_SIZE);
       page_base += PAGE_SIZE) {
    ktl::atomic_ref<uint64_t> var(*reinterpret_cast<uint64_t*>(page_base));
    if (write) {
      var.store(page_base, ktl::memory_order_relaxed);
    }
    if (var.load(ktl::memory_order_relaxed) != page_base) {
      return false;
    }
  }
  return true;
}

// Test that some simple ways to use a virtual allocator work as expected.
bool virtual_alloc_smoke_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);
  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 1, PAGE_SIZE_SHIFT));

  zx::result<vaddr_t> result = alloc.AllocPages(8);
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(TouchPages(*result, 8));

  zx::result<vaddr_t> result2 = alloc.AllocPages(8);
  ASSERT_TRUE(result2.is_ok());
  EXPECT_NE(*result, *result2);
  EXPECT_TRUE(TouchPages(*result2, 8));
  EXPECT_TRUE(TouchPages(*result, 8, false));

  EXPECT_FALSE(alloc.AllocPages(GB).is_ok());

  alloc.FreePages(*result, 8);
  alloc.FreePages(*result2, 4);
  alloc.FreePages(*result2 + 4 * PAGE_SIZE, 4);

  END_TEST;
}

bool virtual_alloc_valid_size_test() {
  BEGIN_TEST;

  TestVmar vmar;

  {
    VirtualAlloc alloc(vm_page_state::ALLOC);
    // This would only hold the bitmap, but not the padding for it
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), 16 * PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    // Would hold the bitmap and one padding.
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), 17 * PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    // Would hold bitmap and two paddings, but still cannot actually allocate a page.
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), 33 * PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    // Succeeds, and should support a single page of allocation.
    EXPECT_OK(alloc.Init(vmar->base(), 34 * PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    zx::result<vaddr_t> result = alloc.AllocPages(1);
    EXPECT_TRUE(result.is_ok());
    // Should not be able to do additional allocations.
    EXPECT_FALSE(alloc.AllocPages(1).is_ok());
    alloc.FreePages(*result, 1);
    EXPECT_FALSE(alloc.AllocPages(2).is_ok());
  }
  EXPECT_TRUE(vmar.RegionEmpty());

  {
    VirtualAlloc alloc(vm_page_state::ALLOC);
    // Two allocations should have a single padding between them, not double padding. To have two
    // allocations of 1 page we should have a layout of
    // [bitmap(1)] [padding(16)] [allocation(1)] [padding(16)] [allocation(1)] [padding(16) = 51.
    ASSERT_OK(alloc.Init(vmar->base(), 51 * PAGE_SIZE, 16, PAGE_SIZE_SHIFT));
    zx::result<vaddr_t> result1 = alloc.AllocPages(1);
    EXPECT_TRUE(result1.is_ok());
    zx::result<vaddr_t> result2 = alloc.AllocPages(1);
    EXPECT_TRUE(result2.is_ok());
    EXPECT_FALSE(alloc.AllocPages(1).is_ok());
    alloc.FreePages(*result1, 1);
    alloc.FreePages(*result2, 1);
  }
  EXPECT_TRUE(vmar.RegionEmpty());

  END_TEST;
}

bool virtual_alloc_compact_test() {
  BEGIN_TEST;

  TestVmar vmar;

  constexpr size_t kNumAlloc = 8;
  vaddr_t allocs[kNumAlloc];

  VirtualAlloc alloc(vm_page_state::ALLOC);
  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 2, PAGE_SIZE_SHIFT));

  for (size_t i = 0; i < kNumAlloc; i++) {
    auto result = alloc.AllocPages(3);
    EXPECT_TRUE(result.is_ok());
    allocs[i] = *result;
    TouchPages(allocs[i], 3);
  }
  for (size_t i = 0; i < kNumAlloc; i++) {
    TouchPages(allocs[i], 3, false);
  }

  // We should now be able to repeatedly free and alloc one of the middle allocations and have it
  // continuously give us the same virtual address.
  for (size_t i = 0; i < 200; i++) {
    alloc.FreePages(allocs[kNumAlloc / 2], 3);
    auto result = alloc.AllocPages(3);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(allocs[kNumAlloc / 2], *result);
    TouchPages(allocs[kNumAlloc / 2], 3);
  }

  // Freeing an allocation in the middle, then the end, should first reuse the middle and then the
  // end.
  alloc.FreePages(allocs[kNumAlloc / 2], 3);
  alloc.FreePages(allocs[kNumAlloc - 1], 3);

  auto result = alloc.AllocPages(3);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(allocs[kNumAlloc / 2], *result);
  TouchPages(allocs[kNumAlloc / 2], 3);

  result = alloc.AllocPages(3);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(allocs[kNumAlloc - 1], *result);
  TouchPages(allocs[kNumAlloc - 1], 3);

  // Now if we free everything and realloc, should get the same starting address.
  for (size_t i = 0; i < kNumAlloc; i++) {
    TouchPages(allocs[i], 3, false);
    alloc.FreePages(allocs[i], 3);
  }

  result = alloc.AllocPages(3);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(allocs[0], *result);
  TouchPages(allocs[0], 3);

  // Cleanup.
  alloc.FreePages(allocs[0], 3);

  END_TEST;
}

bool virtual_alloc_partial_free_compact_test() {
  BEGIN_TEST;

  TestVmar vmar;

  constexpr size_t kNumAlloc = 8;
  vaddr_t allocs[kNumAlloc];

  VirtualAlloc alloc(vm_page_state::ALLOC);
  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 2, PAGE_SIZE_SHIFT));

  for (size_t i = 0; i < kNumAlloc; i++) {
    auto result = alloc.AllocPages(3);
    EXPECT_TRUE(result.is_ok());
    allocs[i] = *result;
    TouchPages(allocs[i], 3);
  }

  // Free one of the middle allocations, and part of the one before it.
  alloc.FreePages(allocs[kNumAlloc / 2], 3);
  alloc.FreePages(allocs[kNumAlloc / 2 - 1] + PAGE_SIZE, 2);

  // Now when we alloc we should get something earlier than the full allocation that we freed.
  auto result = alloc.AllocPages(3);
  EXPECT_TRUE(result.is_ok());
  EXPECT_LT(*result, allocs[kNumAlloc / 2]);
  EXPECT_GT(*result, allocs[kNumAlloc / 2 - 1]);
  TouchPages(*result, 3);

  // Finish freeing the early allocation. A new full allocation should no longer fit anywhere and
  // have to go at the end.
  alloc.FreePages(allocs[kNumAlloc / 2 - 1], 1);

  auto result2 = alloc.AllocPages(3);
  EXPECT_TRUE(result2.is_ok());
  EXPECT_GT(*result2, allocs[kNumAlloc - 1]);
  TouchPages(*result2, 3);

  // cleanup all the allocs.
  allocs[kNumAlloc / 2 - 1] = *result;
  allocs[kNumAlloc / 2] = *result2;

  for (size_t i = 0; i < kNumAlloc; i++) {
    TouchPages(allocs[i], 3, false);
    alloc.FreePages(allocs[i], 3);
  }

  END_TEST;
}

bool virtual_alloc_large_alloc_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);
  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 2, PAGE_SIZE_SHIFT));

  // Create and validate some large allocations to ensure the batching in the mapping path works
  // correctly.
  auto result = alloc.AllocPages(128);
  ASSERT_TRUE(result.is_ok());
  TouchPages(*result, 128);
  alloc.FreePages(*result, 128);

  result = alloc.AllocPages(250);
  ASSERT_TRUE(result.is_ok());
  TouchPages(*result, 250);
  alloc.FreePages(*result, 250);

  END_TEST;
}

bool virtual_alloc_arch_alloc_failure_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);
  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 2, PAGE_SIZE_SHIFT));

  // Perform an allocation to see what virtual address we expect to be using.
  auto result = alloc.AllocPages(250);
  ASSERT_TRUE(result.is_ok());
  TouchPages(*result, 250);
  vaddr_t vaddr = *result;
  alloc.FreePages(vaddr, 250);

  // Let's map our own page towards the end of the allocation.
  vm_page_t* page = nullptr;
  ASSERT_OK(pmm_alloc_page(0, &page));
  auto free_page = fit::defer([&page]() { pmm_free_page(page); });

  paddr_t page_paddr = page->paddr();
  EXPECT_OK(VmAspace::kernel_aspace()->arch_aspace().Map(
      vaddr + 240ul * PAGE_SIZE, &page_paddr, 1,
      ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
      ArchVmAspace::ExistingEntryAction::Error, nullptr));

  // Attempting our allocation again should fail.
  result = alloc.AllocPages(250);
  EXPECT_FALSE(result.is_ok());

  // No other pages should have gotten mapped in though as once our page was discovered everything
  // should have been unmapped and cleaned up.
  EXPECT_TRUE(RangeEmpty(vaddr, 240));

  // Once we unmap our page we should successfully allocate the original mapping.
  EXPECT_OK(VmAspace::kernel_aspace()->arch_aspace().Unmap(
      vaddr + 240ul * PAGE_SIZE, 1, ArchVmAspace::EnlargeOperation::No, nullptr));

  result = alloc.AllocPages(250);
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(vaddr, *result);
  TouchPages(vaddr, 250);

  alloc.FreePages(vaddr, 250);

  END_TEST;
}

bool virtual_alloc_zero_pages_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);

  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 1, PAGE_SIZE_SHIFT));

  // Allocating zero pages is considered an error.
  // TODO: understand and remove cast.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, (zx_status_t)alloc.AllocPages(0).error_value());

  END_TEST;
}

bool virtual_alloc_init_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);

  // Any allocation should fail.
  // TODO: understand and remove cast.
  EXPECT_EQ(ZX_ERR_BAD_STATE, (zx_status_t)alloc.AllocPages(1).error_value());
  // TODO: understand and remove cast.
  EXPECT_EQ(ZX_ERR_BAD_STATE, (zx_status_t)alloc.AllocPages(0).error_value());

  // Bases and sizes need to be aligned
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base() + 1, vmar->size(), 1, PAGE_SIZE_SHIFT));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            alloc.Init(vmar->base() + 1, vmar->size() + 1, 1, PAGE_SIZE_SHIFT));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), vmar->size() + 1, 1, PAGE_SIZE_SHIFT));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            alloc.Init(vmar->base() + 1, vmar->size() - 1, 1, PAGE_SIZE_SHIFT));

  // Require at least page size alignment
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), vmar->size(), 1, 0));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, alloc.Init(vmar->base(), vmar->size(), 1, PAGE_SIZE_SHIFT - 1));

  ASSERT_OK(alloc.Init(vmar->base(), vmar->size(), 1, PAGE_SIZE_SHIFT));

  // Should not be able to re-init
  EXPECT_EQ(ZX_ERR_BAD_STATE, alloc.Init(vmar->base(), vmar->size(), 1, PAGE_SIZE_SHIFT));

  END_TEST;
}

bool virtual_alloc_aligned_alloc_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);

  EXPECT_OK(alloc.Init(vmar->base(), vmar->size(), 1, kTestHeapAlignLog2));

  // Create a large allocation that we will use for future allocations. Size it under the assumption
  // that the bitmap and padding will only use up a single aligned block.
  constexpr size_t kLargeAllocPages = (kTestHeapSize >> PAGE_SIZE_SHIFT) - kAlignPages * 2;
  // Validate that we have at least a few alignment multiples so that any maths we do below is
  // guaranteed to not overflow.
  static_assert(kLargeAllocPages / kAlignPages > 5);
  static_assert(kLargeAllocPages % kAlignPages == 0);
  auto result = alloc.AllocPages(kLargeAllocPages);
  ASSERT_TRUE(result.is_ok());

  // Record the base address of the large block. We'll use this to calculate all our test areas.
  const vaddr_t base_test_vaddr = result.value();

  // Alloc single pages until failure to ensure that in the future any allocations can only succeed
  // where we and when we want them to.
  while ((result = alloc.AllocPages(1)).is_ok())
    ;

  // Free a range in the middle such that with padding and alignment taken into account a single
  // large allocation would fit.
  alloc.FreePages(base_test_vaddr + (kAlignPages * 2 - 1) * PAGE_SIZE, kAlignPages + 2);
  bool contiguous = CanExpectContiguous();
  result = alloc.AllocPages(kAlignPages);
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(base_test_vaddr + kAlignPages * 2 * PAGE_SIZE, result.value());
  EXPECT_TRUE(!contiguous || RangeContiguous(result.value(), kAlignPages));

  // Free the range and re-allocate the lowest page in the gap.
  alloc.FreePages(result.value(), kAlignPages);
  alloc.DebugAllocateVaddrRange(base_test_vaddr + (kAlignPages * 2 - 1) * PAGE_SIZE, 1);

  // Gap should be too small to allocate kAlignPages now.
  ASSERT_FALSE(alloc.AllocPages(kAlignPages).is_ok());

  // Free another page higher up. This should allow the allocation to succeed, although we can make
  // no claims on it being contiguous, since it's no longer aligned.
  alloc.FreePages(base_test_vaddr + (kAlignPages * 3 + 1) * PAGE_SIZE, 1);
  result = alloc.AllocPages(kAlignPages);
  ASSERT_TRUE(result.is_ok());
  alloc.FreePages(result.value(), kAlignPages);

  // Free a large range before and after this, but it should still use the aligned range even though
  // it crates fragmentation that prevents future allocations.
  alloc.FreePages(base_test_vaddr + kAlignPages * PAGE_SIZE, kAlignPages);
  alloc.FreePages(base_test_vaddr + (kAlignPages * 3 + 2) * PAGE_SIZE, kAlignPages - 2);
  contiguous = CanExpectContiguous();
  result = alloc.AllocPages(kAlignPages);
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(base_test_vaddr + kAlignPages * 2 * PAGE_SIZE, result.value());
  EXPECT_TRUE(!contiguous || RangeContiguous(result.value(), kAlignPages));
  EXPECT_FALSE(alloc.AllocPages(kAlignPages).is_ok());

  // We didn't properly track our allocations but it's not the goal of this test so just ask the
  // allocator to clean up for us.
  alloc.DebugFreeAllAllocations();

  END_TEST;
}

bool vritual_alloc_large_allocs_are_contiguous_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);

  EXPECT_OK(alloc.Init(vmar->base(), vmar->size(), 1, kTestHeapAlignLog2));

  if (CanExpectContiguous()) {
    auto result = alloc.AllocPages(kAlignPages);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(RangeContiguous(result.value(), kAlignPages));
    alloc.FreePages(result.value(), kAlignPages);
  } else {
    printf("Failed to find contiguous range of %zu pages, skipping test: %s\n", kAlignPages,
           __FUNCTION__);
  }

  END_TEST;
}

bool virtual_alloc_contiguous_fallback_test() {
  BEGIN_TEST;

  TestVmar vmar;

  VirtualAlloc alloc(vm_page_state::ALLOC);

  EXPECT_OK(alloc.Init(vmar->base(), vmar->size(), 1, kTestHeapAlignLog2));

  list_node_t pages = LIST_INITIAL_VALUE(pages);
  auto return_pages = fit::defer([&pages] { pmm_free(&pages); });
  paddr_t paddr;

  // Now steal all the contiguous blocks out of the pmm.
  list_node_t temp_pages = LIST_INITIAL_VALUE(temp_pages);
  while (pmm_alloc_contiguous(kAlignPages, 0, kTestHeapAlignLog2, &paddr, &temp_pages) == ZX_OK) {
    // Keep the first page and return the rest. This prevents us OOMing whilst still blocking future
    // allocations. We only need to hold one page since this allocation is size aligned and so no
    // page is a candidate for any other allocation.
    vm_page_t* head_page = list_remove_head_type(&temp_pages, vm_page_t, queue_node);
    DEBUG_ASSERT(head_page);
    list_add_head(&pages, &head_page->queue_node);
    pmm_free(&temp_pages);
    temp_pages = LIST_INITIAL_VALUE(temp_pages);
  }

  // A size aligned allocation should still work, as it should fall back to using non-contiguous
  // pages.
  auto result = alloc.AllocPages(kAlignPages);
  ASSERT_TRUE(result.is_ok());
  alloc.FreePages(result.value(), kAlignPages);

  END_TEST;
}

}  // anonymous namespace

// Use the function name as the test name
#define VA_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(virtual_alloc_tests)
VA_UNITTEST(virtual_alloc_smoke_test)
VA_UNITTEST(virtual_alloc_valid_size_test)
VA_UNITTEST(virtual_alloc_compact_test)
VA_UNITTEST(virtual_alloc_partial_free_compact_test)
VA_UNITTEST(virtual_alloc_large_alloc_test)
VA_UNITTEST(virtual_alloc_arch_alloc_failure_test)
VA_UNITTEST(virtual_alloc_zero_pages_test)
VA_UNITTEST(virtual_alloc_init_test)
VA_UNITTEST(virtual_alloc_aligned_alloc_test)
VA_UNITTEST(vritual_alloc_large_allocs_are_contiguous_test)
VA_UNITTEST(virtual_alloc_contiguous_fallback_test)
UNITTEST_END_TESTCASE(virtual_alloc_tests, "virtual_alloc", "virtual_alloc tests")
