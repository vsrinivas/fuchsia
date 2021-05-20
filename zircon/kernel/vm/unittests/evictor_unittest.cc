// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/evictor.h>

#include "test_helper.h"

namespace vm_unittest {

// Custom pmm node to link with the evictor under test. Facilitates verifying the free count which
// is not possible with the global pmm node.
class TestPmmNode {
 public:
  TestPmmNode() : evictor_(&node_, pmm_page_queues()) {}

  ~TestPmmNode() {
    // Pages that were evicted are being held in |node_|'s free list.
    // Return them to the global pmm node before exiting.
    uint64_t free_count = node_.CountFreePages();
    list_node list = LIST_INITIAL_VALUE(list);
    zx_status_t status = node_.AllocPages(free_count, 0, &list);
    ASSERT(status == ZX_OK);

    vm_page_t* page;
    list_for_every_entry (&list, page, vm_page_t, queue_node) {
      page->set_state(vm_page_state::ALLOC);
    }
    pmm_free(&list);
  }

  Evictor::EvictionTarget GetOneShotEvictionTarget() const {
    return evictor_.DebugGetOneShotEvictionTarget();
  }

  void SetMinDiscardableAge(zx_time_t age) { evictor_.DebugSetMinDiscardableAge(age); }

  uint64_t FreePages() const { return node_.CountFreePages(); }

  Evictor* evictor() { return &evictor_; }

 private:
  PmmNode node_;
  Evictor evictor_;
};

// Test that a one shot eviction target can be set as expected.
static bool evictor_set_target_test() {
  BEGIN_TEST;

  TestPmmNode node;

  auto expected = Evictor::EvictionTarget{
      .pending = static_cast<bool>(rand() % 2),
      .free_pages_target = static_cast<uint64_t>(rand()),
      .min_pages_to_free = static_cast<uint64_t>(rand()),
      .level =
          (rand() % 2) ? Evictor::EvictionLevel::IncludeNewest : Evictor::EvictionLevel::OnlyOldest,
  };

  node.evictor()->SetOneShotEvictionTarget(expected);

  auto actual = node.GetOneShotEvictionTarget();

  ASSERT_EQ(actual.pending, expected.pending);
  ASSERT_EQ(actual.free_pages_target, expected.free_pages_target);
  ASSERT_EQ(actual.min_pages_to_free, expected.min_pages_to_free);
  ASSERT_EQ(actual.level, expected.level);

  END_TEST;
}

// Test that multiple one shot eviction targets can be combined as expected.
static bool evictor_combine_targets_test() {
  BEGIN_TEST;

  TestPmmNode node;

  static constexpr int kNumTargets = 5;
  Evictor::EvictionTarget targets[kNumTargets];

  for (auto& target : targets) {
    target = Evictor::EvictionTarget{
        .pending = true,
        .free_pages_target = static_cast<uint64_t>(rand() % 1000),
        .min_pages_to_free = static_cast<uint64_t>(rand() % 1000),
        .level = Evictor::EvictionLevel::IncludeNewest,
    };
    node.evictor()->CombineOneShotEvictionTarget(target);
  }

  Evictor::EvictionTarget expected = {};
  for (auto& target : targets) {
    expected.pending = expected.pending || target.pending;
    expected.level = ktl::max(expected.level, target.level);
    expected.min_pages_to_free += target.min_pages_to_free;
    expected.free_pages_target = ktl::max(expected.free_pages_target, target.free_pages_target);
  }

  auto actual = node.GetOneShotEvictionTarget();

  ASSERT_EQ(actual.pending, expected.pending);
  ASSERT_EQ(actual.free_pages_target, expected.free_pages_target);
  ASSERT_EQ(actual.min_pages_to_free, expected.min_pages_to_free);
  ASSERT_EQ(actual.level, expected.level);

  END_TEST;
}

// Helper to create a pager backed vmo and commit all its pages.
static zx_status_t create_precommitted_pager_backed_vmo(uint64_t size,
                                                        fbl::RefPtr<VmObjectPaged>* vmo_out) {
  // The size should be page aligned for TakePages and SupplyPages to work.
  if (size % PAGE_SIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<StubPageProvider> pager = ktl::make_unique<StubPageProvider>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<PageSource> src = fbl::MakeRefCountedChecked<PageSource>(&ac, ktl::move(pager));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateExternal(ktl::move(src), 0u, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  // Create an aux VMO to transfer pages into the pager-backed vmo.
  fbl::RefPtr<VmObjectPaged> aux_vmo;
  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &aux_vmo);
  if (status != ZX_OK) {
    return status;
  }

  status = aux_vmo->CommitRange(0, size);
  if (status != ZX_OK) {
    return status;
  }

  VmPageSpliceList page_list;
  status = aux_vmo->TakePages(0, size, &page_list);
  if (status != ZX_OK) {
    return status;
  }

  status = vmo->SupplyPages(0, size, &page_list);
  if (status != ZX_OK) {
    return status;
  }

  *vmo_out = ktl::move(vmo);
  return ZX_OK;
}

// Test that the evictor can evict from pager backed vmos as expected.
static bool evictor_pager_backed_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo to evict pages from.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 22;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));

  // Promote the pages for eviction i.e. mark them inactive (first in line for eviction).
  vmo->PromoteForReclamation();

  TestPmmNode node;
  // Only evict from pager backed vmos.
  node.evictor()->SetDiscardableEvictionsPercent(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 20,
      .min_pages_to_free = 10,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  // The node starts off with zero pages.
  uint64_t free_count = node.FreePages();
  EXPECT_EQ(free_count, 0u);

  node.evictor()->SetOneShotEvictionTarget(target);
  auto counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages were evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Free pages target was greater than min pages target. So precisely free pages target must have
  // been evicted.
  EXPECT_EQ(counts.pager_backed, target.free_pages_target);
  EXPECT_GE(counts.pager_backed, target.min_pages_to_free);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_EQ(free_count, target.free_pages_target);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // Re-initialize the vmo and try again with a different target.
  vmo.reset();
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));
  // Promote the pages for eviction i.e. mark them inactive (first in line for eviction).
  vmo->PromoteForReclamation();

  target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 10,
      .min_pages_to_free = 20,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages were evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Min pages target was greater than free pages target. So precisely min pages target must have
  // been evicted.
  EXPECT_EQ(counts.pager_backed, target.min_pages_to_free);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  EXPECT_GE(node.FreePages(), target.free_pages_target);
  EXPECT_EQ(node.FreePages(), free_count + target.min_pages_to_free);

  END_TEST;
}

// Helper to create a fully committed discardable vmo, which is unlocked and can be evicted.
static zx_status_t create_committed_unlocked_discardable_vmo(uint64_t size,
                                                             fbl::RefPtr<VmObjectPaged>* vmo_out) {
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kDiscardable, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  // Lock and commit the vmo.
  status = vmo->TryLockRange(0, size);
  if (status != ZX_OK) {
    return status;
  }
  status = vmo->CommitRange(0, size);
  if (status != ZX_OK) {
    return status;
  }

  // Unlock the vmo so that it can be discarded.
  status = vmo->UnlockRange(0, size);
  if (status != ZX_OK) {
    return status;
  }

  *vmo_out = ktl::move(vmo);
  return ZX_OK;
}

// Test that the evictor can discard from discardable vmos as expected.
static bool evictor_discardable_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a discardable vmo.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 22;
  ASSERT_EQ(ZX_OK, create_committed_unlocked_discardable_vmo(kNumPages * PAGE_SIZE, &vmo));

  TestPmmNode node;
  // Only evict from discardable vmos.
  node.evictor()->SetDiscardableEvictionsPercent(100);
  // Set min discardable age to 0 to that the vmo is eligible for eviction.
  node.SetMinDiscardableAge(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 20,
      .min_pages_to_free = 10,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  // The node starts off with zero pages.
  uint64_t free_count = node.FreePages();
  EXPECT_EQ(free_count, 0u);

  node.evictor()->SetOneShotEvictionTarget(target);
  auto counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No pager backed pages were evicted.
  EXPECT_EQ(counts.pager_backed, 0u);
  // Free pages target was greater than min pages target. So precisely free pages target must have
  // been evicted. However, a discardable vmo can only be discarded in its entirety, so we can't
  // check for equality with free pages target. We can't check for equality with |kNumPages| either
  // as it is possible (albeit unlikely) that a discardable vmo other than the one we created
  // here was discarded, since we're discarding from the global list of discardable vmos. In the
  // future (if and) when vmos are PMM node aware, we will be able to control this better by
  // creating a vmo backed by the test node.
  EXPECT_GE(counts.discardable, target.free_pages_target);
  EXPECT_GE(counts.discardable, target.min_pages_to_free);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_GE(free_count, target.free_pages_target);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // Re-initialize the vmo and try again with a different target.
  vmo.reset();
  ASSERT_EQ(ZX_OK, create_committed_unlocked_discardable_vmo(kNumPages * PAGE_SIZE, &vmo));

  target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 10,
      .min_pages_to_free = 20,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No pager backed pages were evicted.
  EXPECT_EQ(counts.pager_backed, 0u);
  // Min pages target was greater than free pages target. So precisely min pages target must have
  // been evicted. However, a discardable vmo can only be discarded in its entirety, so we can't
  // check for equality with free pages target. We can't check for equality with |kNumPages| either
  // as it is possible (albeit unlikely) that a discardable vmo other than the one we created
  // here was discarded, since we're discarding from the global list of discardable vmos. In the
  // future (if and) when vmos are PMM node aware, we will be able to control this better by
  // creating a vmo backed by the test node.
  EXPECT_GE(counts.discardable, target.min_pages_to_free);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  EXPECT_GE(node.FreePages(), target.free_pages_target);
  EXPECT_GE(node.FreePages(), free_count + target.min_pages_to_free);

  END_TEST;
}

// Test that the evictor can evict out of both discardable and pager backed vmos simultaneously.
static bool evictor_pager_backed_and_discardable_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed and a discardable vmo to share the eviction load.
  static constexpr uint64_t kNumPages = 11;
  fbl::RefPtr<VmObjectPaged> vmo_pager, vmo_discardable;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo_pager));
  ASSERT_EQ(ZX_OK,
            create_committed_unlocked_discardable_vmo(kNumPages * PAGE_SIZE, &vmo_discardable));

  // Promote the pages for eviction i.e. mark them inactive (first in line for eviction).
  vmo_pager->PromoteForReclamation();

  TestPmmNode node;
  // Half the pages will be evicted from pager backed and the other half from discardable vmos.
  node.evictor()->SetDiscardableEvictionsPercent(50);
  // Set min discardable age to 0 to that the discardable vmo is eligible for eviction.
  node.SetMinDiscardableAge(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 20,
      .min_pages_to_free = 10,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  // The node starts off with zero pages.
  uint64_t free_count = node.FreePages();
  EXPECT_EQ(free_count, 0u);

  node.evictor()->SetOneShotEvictionTarget(target);
  auto counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // It's hard to check for equality with discardable vmos in the picture. Refer to the comments in
  // evictor_discardable_test regarding this. Perform some basic sanity checks on the number of
  // pages evicted.
  uint64_t expected_pages_freed = ktl::max(target.free_pages_target, target.min_pages_to_free);
  EXPECT_GE(counts.discardable + counts.pager_backed, expected_pages_freed);
  EXPECT_GE(counts.discardable, 0u);
  EXPECT_GE(counts.pager_backed, 0u);

  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_GE(free_count, target.free_pages_target);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // Reset the vmos and try with a different target.
  vmo_pager.reset();
  vmo_discardable.reset();
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo_pager));
  ASSERT_EQ(ZX_OK,
            create_committed_unlocked_discardable_vmo(kNumPages * PAGE_SIZE, &vmo_discardable));
  // Promote the pages for eviction i.e. mark them inactive (first in line for eviction).
  vmo_pager->PromoteForReclamation();

  target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 10,
      .min_pages_to_free = 20,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // It's hard to check for equality with discardable vmos in the picture. Refer to the comments in
  // evictor_discardable_test regarding this. Perform some basic sanity checks on the number of
  // pages evicted.
  expected_pages_freed = ktl::max(target.free_pages_target, target.min_pages_to_free);
  EXPECT_GE(counts.discardable + counts.pager_backed, expected_pages_freed);
  EXPECT_GE(counts.discardable, 0u);
  EXPECT_GE(counts.pager_backed, 0u);

  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  EXPECT_GE(node.FreePages(), target.free_pages_target);
  EXPECT_GE(node.FreePages(), free_count + target.min_pages_to_free);

  END_TEST;
}

// Test that eviction meets the required free and min target as expected.
static bool evictor_free_target_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo to evict pages from.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 111;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));

  // Promote the pages for eviction i.e. mark them inactive (first in line for eviction).
  vmo->PromoteForReclamation();

  TestPmmNode node;
  // Only evict from pager backed vmos.
  node.evictor()->SetDiscardableEvictionsPercent(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 20,
      .min_pages_to_free = 0,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  // The node starts off with zero pages.
  uint64_t free_count = node.FreePages();
  EXPECT_EQ(free_count, 0u);

  node.evictor()->SetOneShotEvictionTarget(target);
  auto counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages were evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Free pages target was greater than min pages target. So precisely free pages target must have
  // been evicted.
  EXPECT_EQ(counts.pager_backed, target.free_pages_target);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_EQ(free_count, target.free_pages_target);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // Evict again with the same target.
  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No new pages should have been evicted, as the free target was already met with the previous
  // round of eviction, and no minimum pages were requested to be evicted.
  EXPECT_EQ(counts.discardable, 0u);
  EXPECT_EQ(counts.pager_backed, 0u);
  EXPECT_EQ(node.FreePages(), free_count);

  // Evict again with a higher free memory target. No min pages target.
  uint64_t delta_pages = 10;
  target.free_pages_target += delta_pages;
  target.min_pages_to_free = 0;
  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Exactly delta_pages evicted.
  EXPECT_EQ(counts.pager_backed, delta_pages);
  EXPECT_GE(counts.pager_backed, target.min_pages_to_free);
  // Free count increased by delta_pages.
  free_count = node.FreePages();
  EXPECT_EQ(free_count, target.free_pages_target);

  // Evict again with a higher free memory target and also a min pages target.
  target.free_pages_target += delta_pages;
  target.min_pages_to_free = delta_pages;
  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Exactly delta_pages evicted.
  EXPECT_EQ(counts.pager_backed, delta_pages);
  EXPECT_GE(counts.pager_backed, target.min_pages_to_free);
  // Free count increased by delta_pages.
  free_count = node.FreePages();
  EXPECT_EQ(free_count, target.free_pages_target);

  // Evict again with the same free target, but request a min number of pages to be freed.
  target.min_pages_to_free = 2;
  node.evictor()->SetOneShotEvictionTarget(target);
  counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Exactly min pages evicted.
  EXPECT_EQ(counts.pager_backed, target.min_pages_to_free);
  // Free count increased by min pages.
  EXPECT_EQ(node.FreePages(), free_count + target.min_pages_to_free);

  END_TEST;
}

UNITTEST_START_TESTCASE(evictor_tests)
VM_UNITTEST(evictor_set_target_test)
VM_UNITTEST(evictor_combine_targets_test)
VM_UNITTEST(evictor_pager_backed_test)
VM_UNITTEST(evictor_discardable_test)
VM_UNITTEST(evictor_pager_backed_and_discardable_test)
VM_UNITTEST(evictor_free_target_test)
UNITTEST_END_TESTCASE(evictor_tests, "evictor", "Evictor tests")

}  // namespace vm_unittest
