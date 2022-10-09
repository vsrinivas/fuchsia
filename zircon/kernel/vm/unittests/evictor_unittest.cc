// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/evictor.h>
#include <vm/stack_owned_loaned_pages_interval.h>

#include "test_helper.h"

namespace vm_unittest {

// Custom pmm node to link with the evictor under test. Facilitates verifying the free count which
// is not possible with the global pmm node.
class TestPmmNode {
 public:
  TestPmmNode() : evictor_(&node_, pmm_page_queues()) { evictor_.EnableEviction(); }

  ~TestPmmNode() {
    // Pages that were evicted are being held in |node_|'s free list.
    // Return them to the global pmm node before exiting.
    DecrementFreePages(node_.CountFreePages());
    ASSERT(node_.CountFreePages() == 0);
  }

  // Reduce free pages in |node_| by |num_pages|.
  void DecrementFreePages(uint64_t num_pages) {
    uint64_t free_count = node_.CountFreePages();
    if (free_count < num_pages) {
      num_pages = free_count;
    }
    list_node list = LIST_INITIAL_VALUE(list);
    zx_status_t status = node_.AllocPages(num_pages, 0, &list);
    ASSERT(status == ZX_OK);

    // Return these pages to the global pmm. Our goal is to just reduce the free count of |node_|,
    // we do not intend to use the allocated pages for anything.
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
                                                        fbl::RefPtr<VmObjectPaged>* vmo_out,
                                                        vm_page_t** out_pages = nullptr) {
  // The size should be page aligned for TakePages and SupplyPages to work.
  if (size % PAGE_SIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  fbl::RefPtr<StubPageProvider> pager = fbl::MakeRefCountedChecked<StubPageProvider>(&ac);
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

  // Pin the pages momentarily to force the pages to be non-loaned pages.  This allows us to be more
  // strict with asserts that verify how many non-loaned pages are evicted.  Loaned pages can also
  // be evicted along the way to evicting non-loaned pages, but only non-loaned pages count as fully
  // free.
  ASSERT(ZX_OK == vmo->CommitRangePinned(0, size, false));
  vmo->Unpin(0, size);

  // Get the pages after the pin, so that we find non-loaned pages.
  if (out_pages) {
    for (uint64_t i = 0; i < size; i += PAGE_SIZE) {
      status = vmo->GetPage(i, 0, nullptr, nullptr, &out_pages[i / PAGE_SIZE], nullptr);
      if (status != ZX_OK) {
        return status;
      }
    }
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

  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

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
  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

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

  // Promote the pages for eviction.
  vmo_pager->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

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
  // Promote the pages for eviction.
  vmo_pager->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

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

  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

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

// Test that pages are evicted when continuous eviction is enabled, and not evicted when disabled.
static bool evictor_continuous_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo to evict pages from.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 44;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));

  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

  TestPmmNode node;

  // Evict every 10 milliseconds.
  node.evictor()->SetContinuousEvictionInterval(ZX_MSEC(10));
  // Enable eviction. Min pages target is 10 pages. Free mem target is 20 pages.
  const uint64_t free_target = 20;
  node.evictor()->EnableContinuousEviction(10u * PAGE_SIZE, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);

  // Poll the node's free count, relying on the test timeout to kill us if something goes wrong.
  // The free target was 20 and min pages target was 10. We should see 20 pages freed.
  while (node.FreePages() < free_target) {
    printf("polling free count (case 1) ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), free_target);

  // Get rid of all free pages and wait for eviction to happen again.
  node.DecrementFreePages(node.FreePages());
  // Pages should be evicted per the free target again.
  while (node.FreePages() < free_target) {
    printf("polling free count (case 2) ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), free_target);

  // No more pages should be evicted even though eviction is enabled, since we've already met our
  // free target. Wait twice the eviction interval just to be sure.
  Thread::Current::SleepRelative(ZX_MSEC(20));
  EXPECT_EQ(node.FreePages(), free_target);

  // No pages evicted after disabling eviction.
  node.evictor()->DisableContinuousEviction();
  Thread::Current::SleepRelative(ZX_MSEC(20));
  node.DecrementFreePages(node.FreePages());
  Thread::Current::SleepRelative(ZX_MSEC(20));
  EXPECT_EQ(node.FreePages(), 0u);

  END_TEST;
}

// Test that the min pages target specified over multiple calls to enable continuous eviction is
// combined as expected.
static bool evictor_continuous_combine_targets_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo to evict pages from.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 22;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));

  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

  TestPmmNode node;

  // Evict every 10 milliseconds.
  node.evictor()->SetContinuousEvictionInterval(ZX_MSEC(10));
  const uint64_t free_target = 4;
  // Enable eviction. Min pages target is 5 pages. Free mem target is 4 pages.
  //
  // The free target is intentionally chosen to be smaller than the min target, so that we can
  // reliably predict how many pages will be evicted, regardless of how the min target updates are
  // interleaved between the test thread setting it and the eviction thread decrementing it after
  // freeing pages.
  //
  // For example, consider the case where the free target was 6 pages, that is greater than the
  // first min target of 5. There are three outcomes possible here (all valid from the evictor's
  // point of view):
  //
  // 1) The second EnableContinuousEviction happens *before* the eviction thread has decremented the
  // min target after freeing the first set of pages. Here the min target will be 13 when the
  // eviction thread goes to decrement it, and the decrement amount will be 6 (since 6 pages were
  // evicted per the free target with a min target of 5). The updated min target will be 7 and so
  // further 7 pages will be evicted. A total of 13 pages are evicted.
  //
  // 2) The second EnableContinuousEviction happens *after* the eviction thread has decremented the
  // min target after freeing the first set of pages. Here the min target will be 5 when the
  // eviction thread goes to decrement it, the decrement amount will be 6, so the min target will be
  // updated to 0. Now the new EnableContinuousEviction call will set min count to 8, so a further
  // of 8 pages will be evicted. A total of 14 pages are evicted.
  //
  // 3) Both EnableContinuousEviction calls happen before the eviction thread has performed any
  // eviction at all, i.e. it processes both requests together. It will see a min target of 13, a
  // free target of 6, and will evict a total of 13 pages at once.
  //
  // To avoid this inconsistency, we let the min target drive how many pages are evicted as opposed
  // to the free target, by setting the free target lower than the min target. In case 1) the
  // decrement amount will be 5, so a further of 8 pages will be evicted, i.e. a total of 13. In
  // case 2) as well, the decrement amount will be 5, so a further of 8 pages will be evicted i.e. a
  // total of 13. And in case 3) as well, a total of 13 pages will be evicted.
  //
  // Note that the opposite case (free target larger than min target) is covered in
  // evictor_continuous_test.
  node.evictor()->EnableContinuousEviction(5u * PAGE_SIZE, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);
  // Verify that two successive calls to enable combine the min page targets.
  node.evictor()->EnableContinuousEviction(8u * PAGE_SIZE, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);

  // The free target is 4 pages. The combined min target is 13 pages. We should see 13 pages
  // evicted.
  uint64_t expected_free_count = 13;
  while (node.FreePages() < expected_free_count) {
    printf("polling free count ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), expected_free_count);
  EXPECT_GE(node.FreePages(), free_target);

  // Make sure eviction is disabled so that the TestPmmNode destructor can clean up freed pages.
  node.evictor()->DisableContinuousEviction();
  Thread::Current::SleepRelative(ZX_MSEC(20));

  END_TEST;
}

// Test that pages are evicted as expected when continuous eviction is enabled and disabled
// repeatedly.
static bool evictor_continuous_repeated_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo to evict pages from.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 44;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo));

  // Promote the pages for eviction.
  vmo->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

  TestPmmNode node;

  // Evict every 10 milliseconds.
  node.evictor()->SetContinuousEvictionInterval(ZX_MSEC(10));
  uint64_t free_target = 4;
  // Enable eviction. Min pages target is 5 pages. Free mem target is 4 pages.
  //
  // The free target is intentionally chosen to be smaller than the min target, so that we can
  // reliably predict how many pages will be evicted, regardless of how the min target updates are
  // interleaved between the test thread setting it and the eviction thread decrementing it after
  // freeing pages.
  //
  // For example, consider the case where the free target was 6 pages, that is greater than the
  // first min target of 5. There are two outcomes possible here (both valid from the evictor's
  // point of view):
  //
  // 1) The second EnableContinuousEviction happens *before* the eviction thread has decremented the
  // min target after freeing the first set of pages. Here the min target will be 12 when the
  // eviction thread goes to decrement it, and the decrement amount will be 6 (since 6 pages were
  // evicted per the free target with a min target of 5). The updated min target will be 6 and so
  // further 6 pages will be evicted. A total of 12 pages are evicted.
  //
  // 2) The second EnableContinuousEviction happens *after* the eviction thread has decremented the
  // min target after freeing the first set of pages. Here the min target will be 5 when the
  // eviction thread goes to decrement it, the decrement amount will be 6, so the min target will be
  // updated to 0. Now the new EnableContinuousEviction call will set min count to 7, so a further
  // of 7 pages will be evicted. A total of 13 pages are evicted.
  //
  // To avoid this inconsistency, we let the min target drive how many pages are evicted as opposed
  // to the free target, by setting the free target lower than the min target. In case 1) the
  // decrement amount will be 5, so a further of 7 pages will be evicted, i.e. a total of 12. In
  // case 2) as well, the decrement amount will be 5, so a further of 7 pages will be evicted i.e. a
  // total of 12.
  //
  // Note that the opposite case (free target larger than min target) is covered in
  // evictor_continuous_test.
  node.evictor()->EnableContinuousEviction(5u * PAGE_SIZE, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);

  // Poll the node's free count, relying on the test timeout to kill us if something goes wrong.
  // The free target was 4 and min pages target was 5. We should see 5 pages freed.
  uint64_t expected_free_count = 5;
  while (node.FreePages() < expected_free_count) {
    printf("polling free count (case 1) ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), expected_free_count);
  EXPECT_GE(node.FreePages(), free_target);

  // Enable eviction again with a different min pages target.
  node.evictor()->EnableContinuousEviction(7u * PAGE_SIZE, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);
  expected_free_count += 7;
  // We should see another 7 pages freed.
  while (node.FreePages() < expected_free_count) {
    printf("polling free count (case 2) ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), expected_free_count);
  EXPECT_GE(node.FreePages(), free_target);

  // Verify that we can disable and re-enable eviction.
  node.evictor()->DisableContinuousEviction();
  // Set a free target that is higher than the current free count to ensure we see some more pages
  // evicted.
  //
  // We're not relying on min target here to avoid another similar race as outlined above with
  // combining min targets. Here, the eviction thread could decrement the min target (based on the
  // previously freed 7 pages) before or after the following EnableContinuousEviction call. Say we
  // were setting the min target to M keeping the free target the same as before, then we could have
  // two cases (both valid from the evictor's point of view):
  //
  // 1) Eviction thread decrements by 7 *before* we enable. After the eviction thread is done, the
  // min target is going to be zero (regardless of the order of the disable call above, which also
  // resets to zero). When we enable, we will set the min target to M, and so M pages will be
  // evicted the next time.
  //
  // 2) Eviction thread decrements by 7 *after* we enable. The eviction thread will find the min
  // target to be M, and so will decrement it by 7. The resulting target will be |M-7| or 0,
  // depending on whether M is greater than 7 or smaller, respectively. So we will evict either
  // |M-7| or 0 pages.
  //
  // To avoid this scenario, we let the free target drive the next round of eviction, and set the
  // min target to 0. In both cases, the eviction thread will evict further pages based on the delta
  // between free target and the current free count.
  free_target = expected_free_count + 3;
  node.evictor()->EnableContinuousEviction(0, free_target * PAGE_SIZE,
                                           Evictor::EvictionLevel::IncludeNewest);
  // We should see another 3 pages freed.
  while (node.FreePages() < free_target) {
    printf("polling free count (case 3) ...\n");
    Thread::Current::SleepRelative(ZX_MSEC(10));
  }
  EXPECT_EQ(node.FreePages(), free_target);

  // Make sure eviction is disabled so that the TestPmmNode destructor can clean up freed pages.
  node.evictor()->DisableContinuousEviction();
  Thread::Current::SleepRelative(ZX_MSEC(20));

  END_TEST;
}

// Test that the evictor can evict DontNeed hinted pager backed pages as expected.
static bool evictor_dont_need_pager_backed_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo with committed pages.
  fbl::RefPtr<VmObjectPaged> vmo1;
  static constexpr size_t kNumPages = 5;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo1));

  // Promote the pages for eviction. This will put these pages in the DontNeed queue.
  vmo1->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);
  // Now touch these pages, changing the queue stashed in their vm_page_t without actually moving
  // them from the DontNeed queue. The expectation is that the next eviction attempt will fix up the
  // queue for these pages.
  for (size_t i = 0; i < kNumPages; i++) {
    uint8_t data;
    ASSERT_EQ(ZX_OK, vmo1->Read(&data, i * PAGE_SIZE, sizeof(data)));
  }

  // Create another pager backed vmo, which has newer pages compared to the previous one. This will
  // supply the pages below that actually get evicted.
  fbl::RefPtr<VmObjectPaged> vmo2;
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo2));

  // Promote the pages for eviction. This will put these pages in the DontNeed queue in LRU order,
  // i.e. they will be considered for eviction only after vmo1's pages.
  vmo2->HintRange(0, kNumPages * PAGE_SIZE, VmObject::EvictionHint::DontNeed);

  TestPmmNode node;
  // Only evict from pager backed vmos.
  node.evictor()->SetDiscardableEvictionsPercent(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = 5,
      .min_pages_to_free = 5,
      .level = Evictor::EvictionLevel::IncludeNewest,
  };

  // The node starts off with zero pages.
  uint64_t free_count = node.FreePages();
  EXPECT_EQ(free_count, 0u);

  node.evictor()->SetOneShotEvictionTarget(target);
  auto counts = node.evictor()->EvictOneShotFromPreloadedTarget();

  // No discardable pages were evicted.
  EXPECT_EQ(counts.discardable, 0u);
  // Free pages target was the same as min pages target. So precisely free pages target must have
  // been evicted.
  EXPECT_EQ(counts.pager_backed, target.free_pages_target);
  EXPECT_GE(counts.pager_backed, target.min_pages_to_free);
  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_EQ(free_count, target.free_pages_target);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // vmo1 should have no pages evicted from it.
  EXPECT_EQ(kNumPages, vmo1->AttributedPages().uncompressed);

  END_TEST;
}

// Tests that evicted pages are removed from the VMO *and* added to the pmm free pool. Regression
// test for fxbug.dev/73865.
static bool evictor_evicted_pages_are_freed_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  // Create a pager backed vmo with committed pages.
  fbl::RefPtr<VmObjectPaged> vmo;
  static constexpr size_t kNumPages = 5;
  vm_page_t* pages[kNumPages];
  ASSERT_EQ(ZX_OK, create_precommitted_pager_backed_vmo(kNumPages * PAGE_SIZE, &vmo, pages));

  // Verify that the vmo has committed pages.
  EXPECT_EQ(kNumPages, vmo->AttributedPages().uncompressed);

  // Rotate page queues a few times so the newly committed pages above are eligible for eviction.
  for (int i = 0; i < 3; i++) {
    pmm_page_queues()->RotateReclaimQueues();
  }

  TestPmmNode node;
  // Only evict from pager backed vmos.
  node.evictor()->SetDiscardableEvictionsPercent(0);

  auto target = Evictor::EvictionTarget{
      .pending = true,
      // Ensure that all evictable pages end up evicted, so we can verify that the vmo we created
      // has no pages remaining.
      .free_pages_target = UINT64_MAX,
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
  // Evicted pager backed pages should be more than or equal to the vmo's pages. If there were no
  // other evictable pages, we should at least have been able to evict from the vmo we created.
  EXPECT_GE(counts.pager_backed, kNumPages);
  EXPECT_GE(counts.pager_backed, target.min_pages_to_free);

  // The node has the desired number of free pages now, and a minimum of min pages have been freed.
  free_count = node.FreePages();
  EXPECT_GE(free_count, kNumPages);
  EXPECT_GE(free_count, target.min_pages_to_free);

  // All the evicted pages should have ended up in the node's free list. Pages that were evicted in
  // this test is the only way we can end up with free pages in this node. This verifies that
  // pages evicted from pager-backed vmos are freed.
  EXPECT_EQ(free_count, counts.pager_backed);

  // Verify that the vmo has no committed pages remaining. Evicted pages are removed from the vmo.
  EXPECT_TRUE(VmObject::AttributionCounts{} == vmo->AttributedPages());

  // Verify free state for each page.
  for (auto page : pages) {
    EXPECT_TRUE(page->is_free());
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(evictor_tests)
VM_UNITTEST(evictor_set_target_test)
VM_UNITTEST(evictor_combine_targets_test)
VM_UNITTEST(evictor_pager_backed_test)
VM_UNITTEST(evictor_discardable_test)
VM_UNITTEST(evictor_pager_backed_and_discardable_test)
VM_UNITTEST(evictor_free_target_test)
VM_UNITTEST(evictor_continuous_test)
VM_UNITTEST(evictor_continuous_combine_targets_test)
VM_UNITTEST(evictor_continuous_repeated_test)
VM_UNITTEST(evictor_dont_need_pager_backed_test)
VM_UNITTEST(evictor_evicted_pages_are_freed_test)
UNITTEST_END_TESTCASE(evictor_tests, "evictor", "Evictor tests")

}  // namespace vm_unittest
