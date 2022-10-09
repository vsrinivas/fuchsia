// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>

#include <arch/kernel_aspace.h>
#include <ktl/initializer_list.h>
#include <ktl/limits.h>
#include <vm/vm.h>
#include <vm/vm_address_region_enumerator.h>

#include "test_helper.h"

#include <ktl/enforce.h>

namespace vm_unittest {

// Wrapper for harvesting access bits that informs the page queues
static void harvest_access_bits(VmAspace::NonTerminalAction non_terminal_action,
                                VmAspace::TerminalAction terminal_action) {
  AutoVmScannerDisable scanner_disable;
  pmm_page_queues()->BeginAccessScan();
  VmAspace::HarvestAllUserAccessedBits(non_terminal_action, terminal_action);
  pmm_page_queues()->EndAccessScan();
}

// Allocates a region in kernel space, reads/writes it, then destroys it.
static bool vmm_alloc_smoke_test() {
  BEGIN_TEST;
  static const size_t alloc_size = 256 * 1024;

  // allocate a region of memory
  void* ptr;
  auto kaspace = VmAspace::kernel_aspace();
  auto err = kaspace->Alloc("test", alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");
  ASSERT_NONNULL(ptr, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // free the region
  err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(ZX_OK, err, "VmAspace::FreeRegion region of memory");
  END_TEST;
}

// Allocates a contiguous region in kernel space, reads/writes it,
// then destroys it.
static bool vmm_alloc_contiguous_smoke_test() {
  BEGIN_TEST;
  static const size_t alloc_size = 256 * 1024;

  // allocate a region of memory
  void* ptr;
  auto kaspace = VmAspace::kernel_aspace();
  auto err = kaspace->AllocContiguous("test", alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                      kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "VmAspace::AllocContiguous region of memory");
  ASSERT_NONNULL(ptr, "VmAspace::AllocContiguous region of memory");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // test that it is indeed contiguous
  unittest_printf("testing that region is contiguous\n");
  paddr_t last_pa = 0;
  for (size_t i = 0; i < alloc_size / PAGE_SIZE; i++) {
    paddr_t pa = vaddr_to_paddr((uint8_t*)ptr + i * PAGE_SIZE);
    if (last_pa != 0) {
      EXPECT_EQ(pa, last_pa + PAGE_SIZE, "region is contiguous");
    }

    last_pa = pa;
  }

  // free the region
  err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(ZX_OK, err, "VmAspace::FreeRegion region of memory");
  END_TEST;
}

// Allocates a new address space and creates a few regions in it,
// then destroys it.
static bool multiple_regions_test() {
  BEGIN_TEST;

  user_inout_ptr<void> ptr{nullptr};
  static const size_t alloc_size = 16 * 1024;

  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test aspace");
  ASSERT_NONNULL(aspace, "VmAspace::Create pointer");

  VmAspace* old_aspace = Thread::Current::Get()->aspace();
  vmm_set_active_aspace(aspace.get());

  // allocate region 0
  zx_status_t err = AllocUser(aspace.get(), "test0", alloc_size, &ptr);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test_user(ptr, alloc_size)) {
    all_ok = false;
  }

  // allocate region 1
  err = AllocUser(aspace.get(), "test1", alloc_size, &ptr);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test_user(ptr, alloc_size)) {
    all_ok = false;
  }

  // allocate region 2
  err = AllocUser(aspace.get(), "test2", alloc_size, &ptr);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test_user(ptr, alloc_size)) {
    all_ok = false;
  }

  vmm_set_active_aspace(old_aspace);

  // free the address space all at once
  err = aspace->Destroy();
  EXPECT_EQ(ZX_OK, err, "VmAspace::Destroy");
  END_TEST;
}

static bool vmm_alloc_zero_size_fails() {
  BEGIN_TEST;
  const size_t zero_size = 0;
  void* ptr;
  zx_status_t err = VmAspace::kernel_aspace()->Alloc("test", zero_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, err);
  END_TEST;
}

static bool vmm_alloc_bad_specific_pointer_fails() {
  BEGIN_TEST;
  // bad specific pointer
  void* ptr = (void*)1;
  zx_status_t err = VmAspace::kernel_aspace()->Alloc(
      "test", 16384, &ptr, 0, VmAspace::VMM_FLAG_VALLOC_SPECIFIC | VmAspace::VMM_FLAG_COMMIT,
      kArchRwFlags);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, err);
  END_TEST;
}

static bool vmm_alloc_contiguous_missing_flag_commit_fails() {
  BEGIN_TEST;
  // should have VmAspace::VMM_FLAG_COMMIT
  const uint zero_vmm_flags = 0;
  void* ptr;
  zx_status_t err = VmAspace::kernel_aspace()->AllocContiguous("test", 4096, &ptr, 0,
                                                               zero_vmm_flags, kArchRwFlags);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, err);
  END_TEST;
}

static bool vmm_alloc_contiguous_zero_size_fails() {
  BEGIN_TEST;
  const size_t zero_size = 0;
  void* ptr;
  zx_status_t err = VmAspace::kernel_aspace()->AllocContiguous(
      "test", zero_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, err);
  END_TEST;
}

// Allocates a vm address space object directly, allows it to go out of scope.
static bool vmaspace_create_smoke_test() {
  BEGIN_TEST;
  auto aspace = VmAspace::Create(VmAspace::Type::User, "test aspace");
  zx_status_t err = aspace->Destroy();
  EXPECT_EQ(ZX_OK, err, "VmAspace::Destroy");
  END_TEST;
}

static bool vmaspace_create_invalid_ranges() {
  BEGIN_TEST;

// These are defined in vm_aspace.cc.
#define GUEST_PHYSICAL_ASPACE_BASE 0UL
#define GUEST_PHYSICAL_ASPACE_SIZE (1UL << MMU_GUEST_SIZE_SHIFT)

  // Test when base < valid base.
  EXPECT_NULL(VmAspace::Create(USER_ASPACE_BASE - 1, 4096, VmAspace::Type::User, "test"));
  EXPECT_NULL(VmAspace::Create(KERNEL_ASPACE_BASE - 1, 4096, VmAspace::Type::Kernel, "test"));
  EXPECT_NULL(VmAspace::Create(GUEST_PHYSICAL_ASPACE_BASE - 1, 4096, VmAspace::Type::GuestPhysical,
                               "test"));

  // Test when base + size exceeds valid range.
  EXPECT_NULL(
      VmAspace::Create(USER_ASPACE_BASE, USER_ASPACE_SIZE + 1, VmAspace::Type::User, "test"));
  EXPECT_NULL(
      VmAspace::Create(KERNEL_ASPACE_BASE, KERNEL_ASPACE_SIZE + 1, VmAspace::Type::Kernel, "test"));
  EXPECT_NULL(VmAspace::Create(GUEST_PHYSICAL_ASPACE_BASE, GUEST_PHYSICAL_ASPACE_SIZE + 1,
                               VmAspace::Type::GuestPhysical, "test"));

  END_TEST;
}

// Allocates a vm address space object directly, maps something on it,
// allows it to go out of scope.
static bool vmaspace_alloc_smoke_test() {
  BEGIN_TEST;
  auto aspace = VmAspace::Create(VmAspace::Type::User, "test aspace2");

  user_inout_ptr<void> ptr{nullptr};
  auto err = AllocUser(aspace.get(), "test", PAGE_SIZE, &ptr);
  ASSERT_EQ(ZX_OK, err, "allocating region\n");

  // destroy the aspace, which should drop all the internal refs to it
  err = aspace->Destroy();
  EXPECT_EQ(ZX_OK, err, "VmAspace::Destroy");

  // drop the ref held by this pointer
  aspace.reset();
  END_TEST;
}

// Touch mappings in an aspace and ensure we can correctly harvest the accessed bits.
// This test takes an optional tag that is placed in the top byte of the address when performing a
// user_copy.
static bool vmaspace_accessed_test(uint8_t tag) {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  // Create some memory we can map touch to test accessed tracking on. Needs to be created from
  // user pager backed memory as harvesting is allowed to be limited to just that.
  vm_page_t* page;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      make_committed_pager_vmo(1, /*trap_dirty=*/false, /*resizable=*/false, &page, &vmo);
  ASSERT_EQ(ZX_OK, status);
  auto mem = testing::UserMemory::Create(vmo, tag);

  ASSERT_EQ(ZX_OK, mem->CommitAndMap(PAGE_SIZE));

  // Initial accessed state is undefined, so harvest it away.
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);

  // Grab the current queue for the page and then rotate the page queues. This means any future,
  // correct, access harvesting should result in a new page queue.
  uint8_t current_queue = page->object.get_page_queue_ref().load();
  pmm_page_queues()->RotateReclaimQueues();

  // Read from the mapping to (hopefully) set the accessed bit.
  asm volatile("" ::"r"(mem->get<int>(0)) : "memory");
  // Harvest it to move it in the page queue.
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);

  EXPECT_NE(current_queue, page->object.get_page_queue_ref().load());
  current_queue = page->object.get_page_queue_ref().load();

  // Rotating and harvesting again should not make the queue change since we have not accessed it.
  pmm_page_queues()->RotateReclaimQueues();
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_EQ(current_queue, page->object.get_page_queue_ref().load());

  // Set the accessed bit again, and make sure it does now harvest.
  pmm_page_queues()->RotateReclaimQueues();
  asm volatile("" ::"r"(mem->get<int>(0)) : "memory");
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_NE(current_queue, page->object.get_page_queue_ref().load());

  // Set the accessed bit and update age without harvesting.
  asm volatile("" ::"r"(mem->get<int>(0)) : "memory");
  harvest_access_bits(VmAspace::NonTerminalAction::Retain, VmAspace::TerminalAction::UpdateAge);
  current_queue = page->object.get_page_queue_ref().load();

  // Now if we rotate and update again, we should re-age the page.
  pmm_page_queues()->RotateReclaimQueues();
  harvest_access_bits(VmAspace::NonTerminalAction::Retain, VmAspace::TerminalAction::UpdateAge);
  EXPECT_NE(current_queue, page->object.get_page_queue_ref().load());
  current_queue = page->object.get_page_queue_ref().load();
  pmm_page_queues()->RotateReclaimQueues();
  harvest_access_bits(VmAspace::NonTerminalAction::Retain, VmAspace::TerminalAction::UpdateAge);
  EXPECT_NE(current_queue, page->object.get_page_queue_ref().load());

  END_TEST;
}

static bool vmaspace_accessed_test_untagged() { return vmaspace_accessed_test(0); }

#if defined(__aarch64__)
// Rerun the `vmaspace_accessed_test` tests with tags in the top byte of user pointers. This tests
// that the subsequent accessed faults are handled successfully, even if the FAR contains a tag.
static bool vmaspace_accessed_test_tagged() { return vmaspace_accessed_test(0xAB); }
#endif

// Ensure that if a user requested VMO read/write operation would hit a page that has had its
// accessed bits harvested that any resulting fault (on ARM) can be handled.
static bool vmaspace_usercopy_accessed_fault_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  // Create some memory we can map touch to test accessed tracking on. Needs to be created from
  // user pager backed memory as harvesting is allowed to be limited to just that.
  vm_page_t* page;
  fbl::RefPtr<VmObjectPaged> mapping_vmo;
  zx_status_t status =
      make_committed_pager_vmo(1, /*trap_dirty=*/false, /*resizable=*/false, &page, &mapping_vmo);
  ASSERT_EQ(ZX_OK, status);
  auto mem = testing::UserMemory::Create(mapping_vmo);

  ASSERT_EQ(ZX_OK, mem->CommitAndMap(PAGE_SIZE));

  // Need a separate VMO to read/write from.
  fbl::RefPtr<VmObjectPaged> vmo;
  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  ASSERT_EQ(status, ZX_OK);

  // Touch the mapping to make sure it is committed and mapped.
  mem->put<char>(42);

  // Harvest any accessed bits.
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);

  // Read from the VMO into the mapping that has been harvested.
  size_t read_actual = 0;
  status = vmo->ReadUser(Thread::Current::Get()->aspace(), mem->user_out<char>(), 0, sizeof(char),
                         &read_actual);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(read_actual, sizeof(char));

  END_TEST;
}

// Test that page tables that do not get accessed can be successfully unmapped and freed.
static bool vmaspace_free_unaccessed_page_tables_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  constexpr size_t kNumPages = 512 * 3;
  constexpr size_t kMiddlePage = kNumPages / 2;
  constexpr size_t kMiddleOffset = kMiddlePage * PAGE_SIZE;
  ASSERT_OK(VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE * kNumPages, &vmo));

  // Construct an additional aspace to use for mappings and touching pages. This allows us to
  // control whether the aspace is considered active, which can effect reclamation and scanning.
  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test-aspace");
  ASSERT_NONNULL(aspace);

  auto cleanup_aspace = fit::defer([&aspace]() { aspace->Destroy(); });

  auto mem = testing::UserMemory::CreateInAspace(vmo, aspace);

  // Put the state we need to share in a struct so we can easily share it with the thread.
  struct State {
    testing::UserMemory* mem = nullptr;
    AutounsignalEvent touch_event;
    AutounsignalEvent complete_event;
    ktl::atomic<bool> running = true;
  } state;
  state.mem = &*mem;

  // Spin up a kernel thread in the aspace we made. This thread will just continuously wait on an
  // event, touching the mapping whenever it is signaled.
  auto thread_body = [](void* arg) -> int {
    State* state = static_cast<State*>(arg);

    while (state->running) {
      state->touch_event.Wait(Deadline::infinite());
      // Check running again so we do not try and touch mem if attempting to shutdown suddenly.
      if (state->running) {
        state->mem->put<char>(42, kMiddleOffset);
        // Signal the event back
        state->complete_event.Signal();
      }
    }
    return 0;
  };

  Thread* thread = Thread::Create("test-thread", thread_body, &state, DEFAULT_PRIORITY);
  ASSERT_NONNULL(thread);
  aspace->AttachToThread(thread);
  thread->Resume();

  auto cleanup_thread = fit::defer([&state, thread]() {
    state.running = false;
    state.touch_event.Signal();
    thread->Join(nullptr, ZX_TIME_INFINITE);
  });

  // Helper to synchronously wait for the thread to perform a touch.
  auto touch = [&state]() {
    state.touch_event.Signal();
    state.complete_event.Wait(Deadline::infinite());
  };

  EXPECT_OK(mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));

  // Touch the mapping to ensure its accessed.
  touch();

  // Attempting to map should fail, as it's already mapped.
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));

  touch();
  // Harvest the accessed information, this should not actually unmap it, even if we ask it to.
  harvest_access_bits(VmAspace::NonTerminalAction::FreeUnaccessed,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));

  touch();
  // Harvest the accessed information, then attempt to do it again so that it gets unmapped.
  harvest_access_bits(VmAspace::NonTerminalAction::FreeUnaccessed,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  harvest_access_bits(VmAspace::NonTerminalAction::FreeUnaccessed,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_OK(mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));

  // Touch the mapping to ensure its accessed.
  touch();

  // Harvest the page accessed information, but retain the non-terminals.
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  // We can do this a few times.
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  // Now if we attempt to free unaccessed the non-terminal should still be accessed and so nothing
  // should get unmapped.
  harvest_access_bits(VmAspace::NonTerminalAction::FreeUnaccessed,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));

  // If we are not requesting a free, then we should be able to harvest repeatedly.
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, mem->CommitAndMap(PAGE_SIZE, kMiddleOffset));
  harvest_access_bits(VmAspace::NonTerminalAction::Retain,
                      VmAspace::TerminalAction::UpdateAgeAndHarvest);

  END_TEST;
}

// Tests that VmMappings that are marked mergeable behave correctly.
static bool vmaspace_merge_mapping_test() {
  BEGIN_TEST;

  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test aspace");

  // Create a sub VMAR we'll use for all our testing.
  fbl::RefPtr<VmAddressRegion> vmar;
  ASSERT_OK(aspace->RootVmar()->CreateSubVmar(
      0, PAGE_SIZE * 64, 0,
      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE, "test vmar",
      &vmar));

  // Create two different vmos to make mappings into.
  fbl::RefPtr<VmObjectPaged> vmo1;
  ASSERT_OK(VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE * 4, &vmo1));
  fbl::RefPtr<VmObjectPaged> vmo2;
  ASSERT_OK(VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE * 4, &vmo2));

  // Declare some enums to make writing test cases more readable instead of having lots of bools.
  enum MmuFlags { FLAG_TYPE_1, FLAG_TYPE_2 };
  enum MarkMerge { MERGE, NO_MERGE };
  enum MergeResult { MERGES_LEFT, DOES_NOT_MERGE };

  // To avoid boilerplate declare some tests in a data driven way.
  struct {
    struct {
      uint64_t vmar_offset;
      fbl::RefPtr<VmObjectPaged> vmo;
      uint64_t vmo_offset;
      MmuFlags flags;
      MergeResult merge_result;
    } mappings[3];
  } cases[] = {
      // Simple two mapping merge
      {{{0, vmo1, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE, vmo1, PAGE_SIZE, FLAG_TYPE_1, MERGES_LEFT},
        {}}},
      // Simple three mapping merge
      {{{0, vmo1, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE, vmo1, PAGE_SIZE, FLAG_TYPE_1, MERGES_LEFT},
        {PAGE_SIZE * 2, vmo1, PAGE_SIZE * 2, FLAG_TYPE_1, MERGES_LEFT}}},
      // Different mapping flags should block merge
      {{{0, vmo1, 0, FLAG_TYPE_2, DOES_NOT_MERGE},
        {PAGE_SIZE, vmo1, PAGE_SIZE, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE * 2, vmo1, PAGE_SIZE * 2, FLAG_TYPE_1, MERGES_LEFT}}},
      // Discontiguous aspace, but contiguous vmo should not work.
      {{{0, vmo1, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE * 2, vmo1, PAGE_SIZE, FLAG_TYPE_1, DOES_NOT_MERGE},
        {}}},
      // Similar discontiguous vmo, but contiguous aspace should not work.
      {{{0, vmo1, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE, vmo1, PAGE_SIZE * 2, FLAG_TYPE_1, DOES_NOT_MERGE},
        {}}},
      // Leaving a contiguous hole also does not work, mapping needs to actually join.
      {{{0, vmo1, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE * 2, vmo1, PAGE_SIZE * 2, FLAG_TYPE_1, DOES_NOT_MERGE},
        {}}},
      // Different vmo should not work.
      {{{0, vmo2, 0, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE, vmo1, PAGE_SIZE, FLAG_TYPE_1, DOES_NOT_MERGE},
        {PAGE_SIZE * 2, vmo1, PAGE_SIZE * 2, FLAG_TYPE_1, MERGES_LEFT}}},
  };

  for (auto& test : cases) {
    // Want to test all combinations of placing the mappings in subvmars, we just choose this by
    // iterating all the binary representations of 3 digits.
    for (int sub_vmar_comination = 0; sub_vmar_comination < 0b1000; sub_vmar_comination++) {
      const int use_subvmar[3] = {BIT_SET(sub_vmar_comination, 0), BIT_SET(sub_vmar_comination, 1),
                                  BIT_SET(sub_vmar_comination, 2)};
      // Iterate all orders of marking mergeable. For 3 mappings there are  6 possibilities.
      for (int merge_order_combination = 0; merge_order_combination < 6;
           merge_order_combination++) {
        const bool even_merge = (merge_order_combination % 2) == 0;
        const int first_merge = merge_order_combination / 2;
        const int merge_order[3] = {first_merge, (first_merge + (even_merge ? 1 : 2)) % 3,
                                    (first_merge + (even_merge ? 2 : 1)) % 3};

        // Instantiate the requested mappings.
        fbl::RefPtr<VmAddressRegion> vmars[3];
        fbl::RefPtr<VmMapping> mappings[3];
        MergeResult merge_result[3] = {DOES_NOT_MERGE, DOES_NOT_MERGE, DOES_NOT_MERGE};
        for (int i = 0; i < 3; i++) {
          if (test.mappings[i].vmo) {
            uint mmu_flags = ARCH_MMU_FLAG_PERM_READ |
                             (test.mappings[i].flags == FLAG_TYPE_1 ? ARCH_MMU_FLAG_PERM_WRITE : 0);
            if (use_subvmar[i]) {
              ASSERT_OK(vmar->CreateSubVmar(test.mappings[i].vmar_offset, PAGE_SIZE, 0,
                                            VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC |
                                                VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE,
                                            "sub vmar", &vmars[i]));
              ASSERT_OK(vmars[i]->CreateVmMapping(0, PAGE_SIZE, 0, VMAR_FLAG_SPECIFIC,
                                                  test.mappings[i].vmo, test.mappings[i].vmo_offset,
                                                  mmu_flags, "test mapping", &mappings[i]));
            } else {
              ASSERT_OK(vmar->CreateVmMapping(test.mappings[i].vmar_offset, PAGE_SIZE, 0,
                                              VMAR_FLAG_SPECIFIC, test.mappings[i].vmo,
                                              test.mappings[i].vmo_offset, mmu_flags,
                                              "test mapping", &mappings[i]));
            }
          }
          // By default we assume merging happens as declared in the test, unless either this our
          // immediate left is in a subvmar, in which case merging is blocked.
          if (use_subvmar[i] || (i > 0 && use_subvmar[i - 1])) {
            merge_result[i] = DOES_NOT_MERGE;
          } else {
            merge_result[i] = test.mappings[i].merge_result;
          }
        }

        // As we merge track expected mapping sizes and what we have merged
        bool merged[3] = {false, false, false};
        size_t expected_size[3] = {PAGE_SIZE, PAGE_SIZE, PAGE_SIZE};
        // Mark each mapping as mergeable based on merge_order
        for (const auto& mapping : merge_order) {
          if (test.mappings[mapping].vmo) {
            VmMapping::MarkMergeable(ktl::move(mappings[mapping]));
            merged[mapping] = true;
            // See if we have anything pending from the right
            if (mapping < 2 && merged[mapping + 1] && merge_result[mapping + 1] == MERGES_LEFT) {
              expected_size[mapping] += expected_size[mapping + 1];
              expected_size[mapping + 1] = 0;
            }
            // See if we should merge to the left.
            if (merge_result[mapping] == MERGES_LEFT && mapping > 0 && merged[mapping - 1]) {
              if (expected_size[mapping - 1] == 0) {
                expected_size[mapping - 2] += expected_size[mapping];
              } else {
                expected_size[mapping - 1] += expected_size[mapping];
              }
              expected_size[mapping] = 0;
            }
          }
          // Validate sizes to ensure any expected merging happened.
          for (int j = 0; j < 3; j++) {
            if (test.mappings[j].vmo) {
              EXPECT_EQ(mappings[j]->size(), expected_size[j]);
              if (expected_size[j] == 0) {
                EXPECT_EQ(nullptr, mappings[j]->vmo().get());
              } else {
                EXPECT_EQ(mappings[j]->vmo().get(), test.mappings[j].vmo.get());
              }
              EXPECT_EQ(mappings[j]->base(), vmar->base() + test.mappings[j].vmar_offset);
            }
          }
        }

        // Destroy any mappings and VMARs.
        for (int i = 0; i < 3; i++) {
          if (mappings[i]) {
            if (merge_result[i] == MERGES_LEFT) {
              EXPECT_EQ(mappings[i]->Destroy(), ZX_ERR_BAD_STATE);
            } else {
              EXPECT_EQ(mappings[i]->Destroy(), ZX_OK);
            }
          }
          if (vmars[i]) {
            EXPECT_OK(vmars[i]->Destroy());
          }
        }
      }
    }
  }

  // Cleanup the address space.
  EXPECT_OK(vmar->Destroy());
  EXPECT_OK(aspace->Destroy());
  END_TEST;
}

// Tests that page attribution caching at the VmMapping layer behaves as expected under
// commits and decommits on the vmo range.
static bool vm_mapping_attribution_commit_decommit_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  using AttributionCounts = VmObject::AttributionCounts;
  // Create a test VmAspace to temporarily switch to for creating test mappings.
  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test-aspace");
  ASSERT_NONNULL(aspace);

  // Create a VMO to map.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, 16 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_vmo_gen_count = 1;
  uint64_t expected_mapping_gen_count = 1;
  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));

  // Map the left half of the VMO.
  fbl::RefPtr<VmMapping> mapping;
  EXPECT_EQ(aspace->is_user(), true);
  status = aspace->RootVmar()->CreateVmMapping(0, 8 * PAGE_SIZE, 0, 0, vmo, 0, kArchRwUserFlags,
                                               "test-mapping", &mapping);
  EXPECT_EQ(ZX_OK, status);

  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));
  EXPECT_EQ(true, verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                                  expected_vmo_gen_count, AttributionCounts{}));

  // Commit pages a little into the mapping, and past it.
  // Should increment the vmo generation count, but not the mapping generation count.
  status = vmo->CommitRange(4 * PAGE_SIZE, 8 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_vmo_gen_count += 8;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{8u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{4u, 0u}));

  // Decommit the pages committed above, returning the VMO to zero committed pages.
  // Should increment the vmo generation count, but not the mapping generation count.
  status = vmo->DecommitRange(4 * PAGE_SIZE, 8 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_vmo_gen_count;
  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));
  EXPECT_EQ(true, verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                                  expected_vmo_gen_count, AttributionCounts{}));

  // Commit some pages in the VMO again.
  // Should increment the vmo generation count, but not the mapping generation count.
  status = vmo->CommitRange(0, 10 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_vmo_gen_count += 10;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{10u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{8u, 0u}));

  // Decommit pages in the vmo via the mapping.
  // Should increment the vmo generation count, not the mapping generation count.
  status = mapping->DecommitRange(0, mapping->size());
  ASSERT_EQ(ZX_OK, status);
  ++expected_vmo_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{2u, 0u}));
  EXPECT_EQ(true, verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                                  expected_vmo_gen_count, AttributionCounts{}));

  // Destroy the mapping.
  // Should increment the mapping generation count, and invalidate the cached attribution.
  status = mapping->Destroy();
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(0ul, mapping->size());
  ++expected_mapping_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{2u, 0u}));
  EXPECT_EQ(expected_mapping_gen_count, mapping->GetMappingGenerationCount());
  EXPECT_EQ(0ul, mapping->AllocatedPages().uncompressed);
  VmMapping::CachedPageAttribution attr = mapping->GetCachedPageAttribution();
  EXPECT_EQ(0ul, attr.mapping_generation_count);
  EXPECT_EQ(0ul, attr.vmo_generation_count);
  EXPECT_EQ(0ul, attr.page_counts.uncompressed);

  // Free the test address space.
  status = aspace->Destroy();
  EXPECT_EQ(ZX_OK, status);

  END_TEST;
}

// Tests that page attribution caching at the VmMapping layer behaves as expected under
// map and unmap operations on the mapping.
static bool vm_mapping_attribution_map_unmap_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  using AttributionCounts = VmObject::AttributionCounts;
  // Create a test VmAspace to temporarily switch to for creating test mappings.
  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test-aspace");
  ASSERT_NONNULL(aspace);

  // Create a VMO to map.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, 16 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_vmo_gen_count = 1;
  uint64_t expected_mapping_gen_count = 1;
  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));

  // Map the left half of the VMO.
  fbl::RefPtr<VmMapping> mapping;
  EXPECT_EQ(aspace->is_user(), true);
  status = aspace->RootVmar()->CreateVmMapping(0, 8 * PAGE_SIZE, 0, 0, vmo, 0, kArchRwUserFlags,
                                               "test-mapping", &mapping);
  EXPECT_EQ(ZX_OK, status);

  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));
  EXPECT_EQ(true, verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                                  expected_vmo_gen_count, AttributionCounts{}));

  // Commit pages in the vmo via the mapping.
  // Should increment the vmo generation count, not the mapping generation count.
  status = mapping->MapRange(0, mapping->size(), true);
  ASSERT_EQ(ZX_OK, status);
  expected_vmo_gen_count += 8;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{8u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{8u, 0u}));

  // Unmap from the right end of the mapping.
  // Should increment the mapping generation count.
  auto old_base = mapping->base();
  status = mapping->Unmap(mapping->base() + mapping->size() - PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(old_base, mapping->base());
  EXPECT_EQ(7ul * PAGE_SIZE, mapping->size());
  ++expected_mapping_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{8u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{7u, 0u}));

  // Unmap from the center of the mapping.
  // Should increment the mapping generation count.
  status = mapping->Unmap(mapping->base() + 4 * PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(old_base, mapping->base());
  EXPECT_EQ(4ul * PAGE_SIZE, mapping->size());
  ++expected_mapping_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{8u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{4u, 0u}));

  // Unmap from the left end of the mapping.
  // Should increment the mapping generation count.
  status = mapping->Unmap(mapping->base(), PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_NE(old_base, mapping->base());
  EXPECT_EQ(3ul * PAGE_SIZE, mapping->size());
  ++expected_mapping_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{8u, 0u}));
  EXPECT_EQ(true,
            verify_mapping_page_attribution(mapping.get(), expected_mapping_gen_count,
                                            expected_vmo_gen_count, AttributionCounts{3u, 0u}));

  // Free the test address space.
  status = aspace->Destroy();
  EXPECT_EQ(ZX_OK, status);

  END_TEST;
}

// Tests that page attribution caching at the VmMapping layer behaves as expected when
// adjacent mappings are merged.
static bool vm_mapping_attribution_merge_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  using AttributionCounts = VmObject::AttributionCounts;
  // Create a test VmAspace to temporarily switch to for creating test mappings.
  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test-aspace");
  ASSERT_NONNULL(aspace);
  EXPECT_EQ(aspace->is_user(), true);

  // Create a VMO to map.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, 16 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_vmo_gen_count = 1;
  EXPECT_EQ(true,
            verify_object_page_attribution(vmo.get(), expected_vmo_gen_count, AttributionCounts{}));

  // Create some contiguous mappings, marked unmergeable (default behavior) to begin with.
  struct {
    fbl::RefPtr<VmMapping> ref = nullptr;
    VmMapping* ptr = nullptr;
    uint64_t expected_gen_count = 1;
    AttributionCounts expected_page_count;
  } mappings[4];

  uint64_t offset = 0;
  static constexpr uint64_t kSize = 4 * PAGE_SIZE;
  for (int i = 0; i < 4; i++) {
    status =
        aspace->RootVmar()->CreateVmMapping(offset, kSize, 0, VMAR_FLAG_SPECIFIC, vmo, offset,
                                            kArchRwUserFlags, "test-mapping", &mappings[i].ref);
    ASSERT_EQ(ZX_OK, status);
    mappings[i].ptr = mappings[i].ref.get();
    EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                   AttributionCounts{}));
    EXPECT_EQ(true, verify_mapping_page_attribution(mappings[i].ptr, mappings[i].expected_gen_count,
                                                    expected_vmo_gen_count,
                                                    mappings[i].expected_page_count));
    offset += kSize;
  }
  EXPECT_EQ(offset, 16ul * PAGE_SIZE);

  // Commit pages in the VMO.
  status = vmo->CommitRange(0, 16 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_vmo_gen_count += 16;
  for (int i = 0; i < 4; i++) {
    mappings[i].expected_page_count.uncompressed += 4;
    EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                   AttributionCounts{16u, 0u}));
    EXPECT_EQ(true, verify_mapping_page_attribution(mappings[i].ptr, mappings[i].expected_gen_count,
                                                    expected_vmo_gen_count,
                                                    mappings[i].expected_page_count));
  }

  // Mark mappings 0 and 2 mergeable. This should not change anything since they're separated by an
  // unmergeable mapping.
  VmMapping::MarkMergeable(ktl::move(mappings[0].ref));
  VmMapping::MarkMergeable(ktl::move(mappings[2].ref));
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                   AttributionCounts{16u, 0u}));
    EXPECT_EQ(true, verify_mapping_page_attribution(mappings[i].ptr, mappings[i].expected_gen_count,
                                                    expected_vmo_gen_count,
                                                    mappings[i].expected_page_count));
  }

  // Mark mapping 3 mergeable. This will merge mappings 2 and 3, destroying mapping 3 and moving all
  // of its pages into mapping 2. Should also increment the generation count for mapping 2.
  VmMapping::MarkMergeable(ktl::move(mappings[3].ref));
  ++mappings[2].expected_gen_count;
  mappings[2].expected_page_count += mappings[3].expected_page_count;
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                   AttributionCounts{16u, 0u}));
    EXPECT_EQ(true, verify_mapping_page_attribution(mappings[i].ptr, mappings[i].expected_gen_count,
                                                    expected_vmo_gen_count,
                                                    mappings[i].expected_page_count));
  }

  // Mark mapping 1 mergeable. This will merge mappings 0, 1 and 2, with only mapping 0 surviving
  // the merge. All the VMO's pages will have been moved to mapping 0. Should also increment the
  // generation count for mapping 0.
  VmMapping::MarkMergeable(ktl::move(mappings[1].ref));
  ++mappings[0].expected_gen_count;
  mappings[0].expected_page_count += mappings[1].expected_page_count;
  mappings[0].expected_page_count += mappings[2].expected_page_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_vmo_gen_count,
                                                 AttributionCounts{16u, 0u}));
  EXPECT_EQ(true, verify_mapping_page_attribution(mappings[0].ptr, mappings[0].expected_gen_count,
                                                  expected_vmo_gen_count,
                                                  mappings[0].expected_page_count));

  // Free the test address space.
  status = aspace->Destroy();
  EXPECT_EQ(ZX_OK, status);

  END_TEST;
}

static bool arch_noncontiguous_map() {
  BEGIN_TEST;

  // Get some phys pages to test on
  paddr_t phys[3];
  struct list_node phys_list = LIST_INITIAL_VALUE(phys_list);
  zx_status_t status = pmm_alloc_pages(ktl::size(phys), 0, &phys_list);
  ASSERT_EQ(ZX_OK, status, "non contig map alloc");
  {
    size_t i = 0;
    vm_page_t* p;
    list_for_every_entry (&phys_list, p, vm_page_t, queue_node) {
      phys[i] = p->paddr();
      ++i;
    }
  }

  {
    ArchVmAspace aspace(USER_ASPACE_BASE, USER_ASPACE_SIZE, 0);
    status = aspace.Init();
    ASSERT_EQ(ZX_OK, status, "failed to init aspace\n");

    // Attempt to map a set of vm_page_t
    size_t mapped;
    vaddr_t base = USER_ASPACE_BASE + 10 * PAGE_SIZE;
    status = aspace.Map(base, phys, ktl::size(phys), ARCH_MMU_FLAG_PERM_READ,
                        ArchVmAspace::ExistingEntryAction::Error, &mapped);
    ASSERT_EQ(ZX_OK, status, "failed first map\n");
    EXPECT_EQ(ktl::size(phys), mapped, "weird first map\n");
    for (size_t i = 0; i < ktl::size(phys); ++i) {
      paddr_t paddr;
      uint mmu_flags;
      status = aspace.Query(base + i * PAGE_SIZE, &paddr, &mmu_flags);
      EXPECT_EQ(ZX_OK, status, "bad first map\n");
      EXPECT_EQ(phys[i], paddr, "bad first map\n");
      EXPECT_EQ(ARCH_MMU_FLAG_PERM_READ, mmu_flags, "bad first map\n");
    }

    // Attempt to map again, should fail
    status = aspace.Map(base, phys, ktl::size(phys), ARCH_MMU_FLAG_PERM_READ,
                        ArchVmAspace::ExistingEntryAction::Error, &mapped);
    EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

    // Attempt to map partially ovelapping, should fail
    status = aspace.Map(base + 2 * PAGE_SIZE, phys, ktl::size(phys), ARCH_MMU_FLAG_PERM_READ,
                        ArchVmAspace::ExistingEntryAction::Error, &mapped);
    EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");
    status = aspace.Map(base - 2 * PAGE_SIZE, phys, ktl::size(phys), ARCH_MMU_FLAG_PERM_READ,
                        ArchVmAspace::ExistingEntryAction::Error, &mapped);
    EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

    // No entries should have been created by the partial failures
    status = aspace.Query(base - 2 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base - PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base + 3 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base + 4 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");

    status = aspace.Unmap(base, ktl::size(phys), ArchVmAspace::EnlargeOperation::Yes, &mapped);
    ASSERT_EQ(ZX_OK, status, "failed unmap\n");
    EXPECT_EQ(ktl::size(phys), mapped, "weird unmap\n");
    status = aspace.Destroy();
    EXPECT_EQ(ZX_OK, status, "failed to destroy aspace\n");
  }

  pmm_free(&phys_list);

  END_TEST;
}

// Get the mmu_flags of the given vaddr of the given aspace.
//
// Return 0 if the page is unmapped or on error.
static uint get_vaddr_flags(ArchVmAspace* aspace, vaddr_t vaddr) {
  paddr_t unused_paddr;
  uint mmu_flags;
  if (aspace->Query(vaddr, &unused_paddr, &mmu_flags) != ZX_OK) {
    return 0;
  }
  return mmu_flags;
}

// Determine if the given page is mapped in.
static bool is_vaddr_mapped(ArchVmAspace* aspace, vaddr_t vaddr) {
  return get_vaddr_flags(aspace, vaddr) != 0;
}

static bool arch_vm_aspace_protect_split_pages() {
  BEGIN_TEST;

  constexpr uint kReadOnly = ARCH_MMU_FLAG_PERM_READ;
  constexpr uint kReadWrite = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

  // Create a basic address space, starting from vaddr 0.
  ArchVmAspace aspace(0, USER_ASPACE_SIZE, 0);
  ASSERT_OK(aspace.Init());
  auto cleanup = fit::defer([&]() {
    aspace.Unmap(0, USER_ASPACE_SIZE / PAGE_SIZE, ArchVmAspace::EnlargeOperation::Yes, nullptr);
    aspace.Destroy();
  });

  // Map in a large contiguous area, which should be mapped by two large pages.
  static_assert(ZX_MAX_PAGE_SIZE > PAGE_SIZE);
  constexpr size_t kRegionSize = 16ul * 1024 * 1024 * 1024;  // 16 GiB.
  ASSERT_OK(aspace.MapContiguous(/*vaddr=*/0, /*paddr=*/0, /*count=*/kRegionSize / PAGE_SIZE,
                                 kReadOnly, nullptr));

  // Attempt to protect a subrange in the middle of the region, which will require splitting
  // pages.
  constexpr vaddr_t kProtectedRange = kRegionSize / 2 - PAGE_SIZE;
  constexpr size_t kProtectedPages = 2;
  ASSERT_OK(aspace.Protect(kProtectedRange, /*count=*/kProtectedPages, kReadWrite));

  // Ensure the pages inside the range changed.
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange), kReadWrite);
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange + PAGE_SIZE), kReadWrite);

  // Ensure the pages surrounding the range did not change.
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange - PAGE_SIZE), kReadOnly);
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange + kProtectedPages * PAGE_SIZE), kReadOnly);

  END_TEST;
}

static bool arch_vm_aspace_protect_split_pages_out_of_memory() {
  BEGIN_TEST;

  constexpr uint kReadOnly = ARCH_MMU_FLAG_PERM_READ;
  constexpr uint kReadWrite = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

  // Create a custom allocator that we can cause to stop returning allocations.
  //
  // ArchVmAspace doesn't allow us to send state to the allocator, so we use a
  // global static here to control the allocator.
  static bool allow_allocations;
  auto allocator = +[](uint alloc_flags, vm_page** p, paddr_t* pa) -> zx_status_t {
    if (!allow_allocations) {
      return ZX_ERR_NO_MEMORY;
    }
    return pmm_alloc_page(0, p, pa);
  };
  allow_allocations = true;

  // Create a basic address space, starting from vaddr 0.
  ArchVmAspace aspace(0, USER_ASPACE_SIZE, 0, allocator);
  ASSERT_OK(aspace.Init());
  auto cleanup = fit::defer([&]() {
    aspace.Unmap(0, USER_ASPACE_SIZE / PAGE_SIZE, ArchVmAspace::EnlargeOperation::Yes, nullptr);
    aspace.Destroy();
  });

  // Map in a large contiguous area, large enough to use large pages to fill.
  constexpr size_t kRegionSize = 16ul * 1024 * 1024 * 1024;  // 16 GiB.
  ASSERT_OK(aspace.MapContiguous(/*vaddr=*/0, /*paddr=*/0, /*count=*/kRegionSize / PAGE_SIZE,
                                 kReadOnly, nullptr));

  // Prevent further allocations.
  allow_allocations = false;

  // Attempt to protect a subrange in the middle of the region, which will require splitting
  // pages. Expect this to fail.
  constexpr vaddr_t kProtectedRange = kRegionSize / 2 - PAGE_SIZE;
  constexpr size_t kProtectedSize = 2 * PAGE_SIZE;
  zx_status_t status = aspace.Protect(kProtectedRange, /*count=*/2, kReadWrite);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);

  // The pages surrounding our protect range should still be mapped.
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange - PAGE_SIZE), kReadOnly);
  EXPECT_EQ(get_vaddr_flags(&aspace, kProtectedRange + kProtectedSize), kReadOnly);

  // The pages we tried to protect should still be mapped, albeit permissions might
  // be changed.
  EXPECT_TRUE(is_vaddr_mapped(&aspace, kProtectedRange));
  EXPECT_TRUE(is_vaddr_mapped(&aspace, kProtectedRange + PAGE_SIZE));

  END_TEST;
}

// Test to make sure all the vm kernel regions (code, rodata, data, bss, etc.) is correctly mapped
// in vm and has the correct arch_mmu_flags. This test also check that all gaps are contained within
// a VMAR.
static bool vm_kernel_region_test() {
  BEGIN_TEST;

  fbl::RefPtr<VmAddressRegionOrMapping> kernel_vmar =
      VmAspace::kernel_aspace()->RootVmar()->FindRegion(reinterpret_cast<vaddr_t>(__code_start));
  EXPECT_NE(kernel_vmar.get(), nullptr);
  EXPECT_FALSE(kernel_vmar->is_mapping());
  for (vaddr_t base = reinterpret_cast<vaddr_t>(__code_start);
       base < reinterpret_cast<vaddr_t>(_end); base += PAGE_SIZE) {
    bool within_region = false;
    for (const auto& kernel_region : kernel_regions) {
      // This would not overflow because the region base and size are hard-coded.
      if (base >= kernel_region.base &&
          base + PAGE_SIZE <= kernel_region.base + kernel_region.size) {
        // If this page exists within a kernel region, then it should be within a VmMapping with
        // the correct arch MMU flags.
        within_region = true;
        fbl::RefPtr<VmAddressRegionOrMapping> region =
            kernel_vmar->as_vm_address_region()->FindRegion(base);
        // Every page from __code_start to _end should either be a VmMapping or a VMAR.
        EXPECT_NE(region.get(), nullptr);
        EXPECT_TRUE(region->is_mapping());
        Guard<CriticalMutex> guard{region->as_vm_mapping()->lock()};
        EXPECT_EQ(kernel_region.arch_mmu_flags,
                  region->as_vm_mapping()->arch_mmu_flags_locked(base));
        break;
      }
    }
    if (!within_region) {
      auto region = VmAspace::kernel_aspace()->RootVmar()->FindRegion(base);
      EXPECT_EQ(region.get(), kernel_vmar.get());
    }
  }

  END_TEST;
}

class TestRegion : public fbl::RefCounted<TestRegion>,
                   public fbl::WAVLTreeContainable<fbl::RefPtr<TestRegion>> {
 public:
  TestRegion(vaddr_t base, size_t size) : base_(base), size_(size) {}
  ~TestRegion() = default;
  vaddr_t base() const { return base_; }
  size_t size() const { return size_; }
  vaddr_t GetKey() const { return base(); }

 private:
  vaddr_t base_;
  size_t size_;
};

static void insert_region(RegionList<TestRegion>* regions, vaddr_t base, size_t size) {
  fbl::AllocChecker ac;
  auto test_region = fbl::AdoptRef(new (&ac) TestRegion(base, size));
  ASSERT(ac.check());
  regions->InsertRegion(ktl::move(test_region));
}

static bool remove_region(RegionList<TestRegion>* regions, vaddr_t base) {
  auto region = regions->FindRegion(base);
  if (region == nullptr) {
    return false;
  }
  regions->RemoveRegion(region);
  return true;
}

static bool region_list_get_alloc_spot_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;
  vaddr_t size = 0x0001000000000000;
  vaddr_t alloc_spot = 0;
  // Set the align to be 0x1000.
  uint8_t align_pow2 = 12;
  // Allocate 1 page, should be allocated at [+0, +0x1000].
  size_t alloc_size = 0x1000;
  zx_status_t status = regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size,
                                            base, size, /*prng=*/nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(base, alloc_spot);
  insert_region(&regions, alloc_spot, alloc_size);

  // Manually insert a sub region at [+0x2000, 0x3000].
  insert_region(&regions, base + 0x2000, alloc_size);

  // Try to allocate 2 page, since the gap is too small, we would allocate at [0x3000, 0x5000].
  alloc_size = 0x2000;
  status = regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size, base, size,
                                /*prng=*/nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(base + 0x3000, alloc_spot);
  insert_region(&regions, alloc_spot, alloc_size);

  EXPECT_TRUE(remove_region(&regions, base + 0x2000));

  // After we remove the region, we now have a gap at [0x1000, 0x3000].
  alloc_size = 0x2000;
  status = regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size, base, size,
                                /*prng=*/nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(base + 0x1000, alloc_spot);
  insert_region(&regions, alloc_spot, alloc_size);

  // Now we have fill all the gaps, next region should start at 0x5000.
  alloc_size = 0x1000;
  status = regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size, base, size,
                                /*prng=*/nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(base + 0x5000, alloc_spot);
  insert_region(&regions, alloc_spot, alloc_size);

  // Test for possible overflow cases. We try to allocate all the rest of the spaces. The last
  // region should be from [0x6000, base + size - 1], we should be able to find this region and
  // allocate all the size from it.
  alloc_size = size - 0x6000;
  status = regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size, base, size,
                                /*prng=*/nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(base + 0x6000, alloc_spot);

  END_TEST;
}

static bool region_list_get_alloc_spot_no_memory_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;
  vaddr_t size = 0x0001000000000000;
  // Set the align to be 0x1000.
  uint8_t align_pow2 = 12;

  insert_region(&regions, base, size - 0x1000);

  size_t alloc_size = 0x2000;
  vaddr_t alloc_spot = 0;
  // There is only a 1 page gap, and we are asking for two pages, so ZX_ERR_NO_RESOURCES should be
  // returned.
  zx_status_t status =
      regions.GetAllocSpot(&alloc_spot, align_pow2, /*entropy=*/0, alloc_size, base, size,
                           /*prng=*/nullptr);
  EXPECT_EQ(ZX_ERR_NO_RESOURCES, status);

  END_TEST;
}

static bool region_list_find_region_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;

  auto region = regions.FindRegion(base);
  EXPECT_EQ(region, nullptr);

  insert_region(&regions, base + 0x1000, 0x1000);

  region = regions.FindRegion(base + 1);
  EXPECT_EQ(region, nullptr);

  region = regions.FindRegion(base + 0x1001);
  EXPECT_NE(region, nullptr);
  EXPECT_EQ(base + 0x1000, region->base());
  EXPECT_EQ((size_t)0x1000, region->size());

  END_TEST;
}

static bool region_list_include_or_higher_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;

  insert_region(&regions, base + 0x1000, 0x1000);

  auto itr = regions.IncludeOrHigher(base + 1);
  EXPECT_TRUE(itr.IsValid());
  EXPECT_EQ(base + 0x1000, itr->base());
  EXPECT_EQ((size_t)0x1000, itr->size());

  itr = regions.IncludeOrHigher(base + 0x1001);
  EXPECT_TRUE(itr.IsValid());
  EXPECT_EQ(base + 0x1000, itr->base());
  EXPECT_EQ((size_t)0x1000, itr->size());

  itr = regions.IncludeOrHigher(base + 0x2000);
  EXPECT_FALSE(itr.IsValid());

  END_TEST;
}

static bool region_list_upper_bound_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;

  insert_region(&regions, base + 0x1000, 0x1000);

  auto itr = regions.UpperBound(base + 0xFFF);
  EXPECT_TRUE(itr.IsValid());
  EXPECT_EQ(base + 0x1000, itr->base());
  EXPECT_EQ((size_t)0x1000, itr->size());

  itr = regions.UpperBound(base + 0x1000);
  EXPECT_FALSE(itr.IsValid());

  END_TEST;
}

static bool region_list_is_range_available_test() {
  BEGIN_TEST;

  RegionList<TestRegion> regions;
  vaddr_t base = 0xFFFF000000000000;

  insert_region(&regions, base + 0x1000, 0x1000);
  insert_region(&regions, base + 0x3000, 0x1000);

  EXPECT_TRUE(regions.IsRangeAvailable(base, 0x1000));
  EXPECT_FALSE(regions.IsRangeAvailable(base, 0x1001));
  EXPECT_FALSE(regions.IsRangeAvailable(base + 1, 0x1000));
  EXPECT_TRUE(regions.IsRangeAvailable(base + 0x2000, 1));
  EXPECT_FALSE(regions.IsRangeAvailable(base + 0x1FFF, 0x2000));

  EXPECT_TRUE(regions.IsRangeAvailable(0xFFFFFFFFFFFFFFFF, 1));
  EXPECT_FALSE(regions.IsRangeAvailable(base, 0x0001000000000000));

  END_TEST;
}

// Helper class for writing tests against the pausable VmAddressRegionEnumerator
class EnumeratorTestHelper {
 public:
  EnumeratorTestHelper() = default;
  ~EnumeratorTestHelper() { Destroy(); }
  zx_status_t Init(fbl::RefPtr<VmAspace> aspace) TA_EXCL(lock()) {
    Destroy();
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, GB, &vmo_);
    if (status != ZX_OK) {
      return status;
    }

    status = aspace->RootVmar()->CreateSubVmar(
        0, GB, 0, VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ, "test vmar", &test_vmar_);
    if (status != ZX_OK) {
      return status;
    }
    return ZX_OK;
  }

  struct ChildRegion {
    bool mapping;
    size_t page_offset_begin;
    size_t page_offset_end;
  };
  zx_status_t AddRegions(ktl::initializer_list<ChildRegion>&& regions) TA_EXCL(lock()) {
    for (auto& region : regions) {
      ASSERT(region.page_offset_end > region.page_offset_begin);
      const size_t offset = region.page_offset_begin * PAGE_SIZE;
      const vaddr_t vaddr = test_vmar_->base() + offset;
      // See if there's a child VMAR that we should be making this in instead of our test root.
      fbl::RefPtr<VmAddressRegion> vmar;
      auto child_region = test_vmar_->FindRegion(vaddr);
      if (child_region) {
        // Set our target vmar to the child region. If it's actually a mapping then this is fine,
        // we'll end up trying to create in the test_vmar which will then fail.
        vmar = child_region->as_vm_address_region();
      }
      // If no child then use the root test_vmar
      if (!vmar) {
        vmar = test_vmar_;
      }
      // Create either a mapping or vmar as requested.
      const size_t size = (region.page_offset_end - region.page_offset_begin) * PAGE_SIZE;
      zx_status_t status;
      if (region.mapping) {
        fbl::RefPtr<VmMapping> new_mapping;
        status = vmar->CreateVmMapping(offset, size, 0, VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_SPECIFIC,
                                       vmo_, 0, ARCH_MMU_FLAG_PERM_READ, "mapping", &new_mapping);
      } else {
        fbl::RefPtr<VmAddressRegion> new_vmar;
        status = vmar->CreateSubVmar(
            offset, size, 0,
            VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC, "vmar",
            &new_vmar);
      }
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }
  using RegionEnumerator =
      VmAddressRegionEnumerator<VmAddressRegionEnumeratorType::PausableMapping>;
  RegionEnumerator Enumerator(size_t page_offset_begin, size_t page_offset_end) TA_REQ(lock()) {
    const vaddr_t min_addr = test_vmar_->base() + page_offset_begin * PAGE_SIZE;
    const vaddr_t max_addr = test_vmar_->base() + page_offset_end * PAGE_SIZE;
    return VmAddressRegionEnumerator<VmAddressRegionEnumeratorType::PausableMapping>(
        *test_vmar_, min_addr, max_addr);
  }

  void Resume(RegionEnumerator& enumerator) TA_REQ(lock()) {
    AssertHeld(enumerator.lock_ref());
    enumerator.resume();
  }

  bool ExpectRegions(RegionEnumerator& enumerator, ktl::initializer_list<ChildRegion>&& regions)
      TA_REQ(lock()) {
    AssertHeld(enumerator.lock_ref());
    for (auto& region : regions) {
      ASSERT(region.page_offset_end > region.page_offset_begin);
      if (!region.mapping) {
        continue;
      }
      auto next = enumerator.next();
      if (!next.has_value()) {
        return false;
      }
      if (next->region_or_mapping->base() !=
          test_vmar_->base() + region.page_offset_begin * PAGE_SIZE) {
        return false;
      }
      if (next->region_or_mapping->size() !=
          (region.page_offset_end - region.page_offset_begin) * PAGE_SIZE) {
        return false;
      }
    }
    return true;
  }

  zx_status_t Unmap(size_t page_offset_begin, size_t page_offset_end) TA_EXCL(lock()) {
    ASSERT(page_offset_end > page_offset_begin);
    const vaddr_t vaddr = test_vmar_->base() + page_offset_begin * PAGE_SIZE;
    const size_t size = (page_offset_end - page_offset_begin) * PAGE_SIZE;
    return test_vmar_->Unmap(vaddr, size);
  }

  Lock<CriticalMutex>* lock() const TA_RET_CAP(test_vmar_->lock()) { return test_vmar_->lock(); }

 private:
  void Destroy() {
    if (test_vmar_) {
      test_vmar_->Destroy();
      test_vmar_.reset();
    }
    vmo_.reset();
  }
  fbl::RefPtr<VmObjectPaged> vmo_;
  fbl::RefPtr<VmAddressRegion> test_vmar_;
};

static bool address_region_enumerator_mapping_test() {
  BEGIN_TEST;

  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test aspace");

  // Smoke test of a single region.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 0, 1}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 1);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 0, 1}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Unmap while iterating a subvmar and resume in the parent.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(
        test.AddRegions({{false, 0, 7}, {true, 1, 2}, {true, 3, 4}, {true, 5, 6}, {true, 7, 8}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 10);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 2}}));
    enumerator.pause();
    // Unmap the entire subvmar we created
    guard.CallUnlocked([&test] { test.Unmap(0, 7); });
    test.Resume(enumerator);
    // Last mapping should still be there.
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 7, 8}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Pause immediately without enumerating when the start is a subvmar.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{false, 0, 2}, {true, 1, 2}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 2);
    AssertHeld(enumerator.lock_ref());
    enumerator.pause();
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 2}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Add future mapping.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 0, 1}, {true, 1, 2}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 3);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 0, 1}}));
    enumerator.pause();
    guard.CallUnlocked([&test] { test.AddRegions({{true, 2, 3}}); });
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 2}, {true, 2, 3}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Replace the next mapping.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 0, 1}, {true, 1, 2}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 3);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 0, 1}}));
    enumerator.pause();
    guard.CallUnlocked([&test] {
      test.Unmap(1, 2);
      test.AddRegions({{true, 1, 3}});
    });
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 3}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Add earlier regions.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 2, 3}, {true, 3, 4}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 4);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 2, 3}}));
    enumerator.pause();
    guard.CallUnlocked([&test] { test.AddRegions({{true, 0, 1}, {true, 1, 32}}); });
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 3, 4}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Replace current.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 1, 2}, {true, 2, 3}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 3);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 2}}));
    enumerator.pause();
    guard.CallUnlocked([&test] {
      test.Unmap(1, 2);
      test.AddRegions({{true, 0, 2}});
    });
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 2, 3}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Replace current and next with a single mapping.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{true, 1, 2}, {true, 2, 3}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(0, 3);
    AssertHeld(enumerator.lock_ref());
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 1, 2}}));
    enumerator.pause();
    guard.CallUnlocked([&test] {
      test.Unmap(1, 3);
      test.AddRegions({{true, 0, 3}});
    });
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 0, 3}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }
  // Start enumerating part way into a mapping.
  {
    EnumeratorTestHelper test;
    ASSERT_OK(test.Init(aspace));
    EXPECT_OK(test.AddRegions({{false, 0, 6}, {true, 0, 2}, {true, 2, 4}, {true, 6, 7}}));
    Guard<CriticalMutex> guard{test.lock()};
    auto enumerator = test.Enumerator(3, 7);
    AssertHeld(enumerator.lock_ref());
    enumerator.pause();
    test.Resume(enumerator);
    ASSERT_TRUE(test.ExpectRegions(enumerator, {{true, 2, 4}, {true, 6, 7}}));
    ASSERT_FALSE(enumerator.next().has_value());
  }

  EXPECT_OK(aspace->Destroy());

  END_TEST;
}

// Doesn't do anything, just prints all aspaces.
// Should be run after all other tests so that people can manually comb
// through the output for leaked test aspaces.
static bool dump_all_aspaces() {
  BEGIN_TEST;

  // Remove for debugging.
  END_TEST;

  unittest_printf("verify there are no test aspaces left around\n");
  VmAspace::DumpAllAspaces(/*verbose*/ true);
  END_TEST;
}

// Check if a range of addresses is accessible to the user. If `spectre_validation` is true, this is
// done by checking if `validate_user_accessible_range` returns {0,0}. Otherwise, check using
// `is_user_accessible_range`.
static bool check_user_accessible_range(vaddr_t vaddr, size_t len, bool spectre_validation) {
  if (spectre_validation) {
    // If the address and length were not modified, then the pair is valid.
    vaddr_t old_vaddr = vaddr;
    size_t old_len = len;
    internal::validate_user_accessible_range(&vaddr, &len);
    return vaddr == old_vaddr && len == old_len;
  }

  return is_user_accessible_range(vaddr, len);
}

static bool check_user_accessible_range_test(bool spectre_validation) {
  BEGIN_TEST;
  vaddr_t va;
  size_t len;

  // Test address of zero.
  va = 0;
  len = PAGE_SIZE;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test address and length of zero (both are valid).
  va = 0;
  len = 0;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test very end of address space and zero length (this is invalid since the start has bit 55 set
  // despite zero length).
  va = ktl::numeric_limits<uint64_t>::max();
  len = 0;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test a regular user address.
  va = USER_ASPACE_BASE;
  len = PAGE_SIZE;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test zero-length on a regular user address.
  va = USER_ASPACE_BASE;
  len = 0;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test overflow past 64 bits.
  va = USER_ASPACE_BASE;
  len = ktl::numeric_limits<uint64_t>::max() - va + 1;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

#if defined(__aarch64__)

  // On aarch64, an address is accessible to the user if bit 55 is zero.

  // Test starting on a bad user address.
  constexpr vaddr_t kBadAddrMask = UINT64_C(1) << 55;
  va = kBadAddrMask | USER_ASPACE_BASE;
  len = PAGE_SIZE;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test zero-length on a bad user address.
  va = kBadAddrMask | USER_ASPACE_BASE;
  len = 0;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test 2^55 is in the range of [va, va+len), ending on a bad user address.
  va = USER_ASPACE_BASE;
  len = kBadAddrMask;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test this returns false if any address within the range of [va, va+len)
  // contains a value where bit 55 is set. This also implies there are many
  // gaps in ranges above 2^56.
  //
  // Here both the start and end values are valid, but this range contains an
  // address that is invalid.
  va = 0;
  len = 0x17f'ffff'ffff'ffff;  // Bits 0-56 (except 55) are set.
  ASSERT_TRUE(is_user_accessible(va));
  ASSERT_TRUE(is_user_accessible(va + len));
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test the range of the largest value less than 2^55 and the smallest value
  // greater than 2^55 where bit 55 == 0.
  va = (UINT64_C(1) << 55) - 1;
  len = 0x80'0000'0000'0001;  // End = va + len = 2^56.
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Be careful not to just check that 2^55 is in the range. We really want to
  // check whenever bit 55 is flipped in the range.
  va = 0x17f'ffff'ffff'ffff;  // Start above 2^56. Bit 55 is not set.
  len = 0x80'0000'0000'0001;  // End = va + len = 0x200'0000'0000'0000. This is above 2^56 and bit
                              // 55 also is not set.
  ASSERT_TRUE(is_user_accessible(va));
  ASSERT_TRUE(is_user_accessible(va + len));
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  va = USER_ASPACE_BASE;
  len = (UINT64_C(1) << 57) + 1;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test a range above 2^56 where bit 55 is never set.
  va = 0x170'0000'0000'0000;
  len = 0xf'ffff'ffff'ffff;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test a range right below 2^55 where bit 55 is never set.
  va = 0x70'0000'0000'0000;
  len = 0xf'ffff'ffff'ffff;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

  // Test the last valid user space address with a tag of 0.
  va = ktl::numeric_limits<uint64_t>::max();
  va &= ~(UINT64_C(0xFF) << 56);  // Set tag to zero.
  va &= ~kBadAddrMask;            // Ensure valid user address.
  len = 0;
  EXPECT_TRUE(check_user_accessible_range(va, len, spectre_validation));

#elif defined(__x86_64__)

  // On x86_64, an address is accessible to the user if bits 48-63 are zero.

  // Test a bad user address.
  constexpr vaddr_t kBadAddrMask = UINT64_C(1) << 48;
  va = kBadAddrMask | USER_ASPACE_BASE;
  len = PAGE_SIZE;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test zero-length on a bad user address.
  va = kBadAddrMask | USER_ASPACE_BASE;
  len = 0;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

  // Test ending on a bad user address.
  va = USER_ASPACE_BASE;
  len = kBadAddrMask;
  EXPECT_FALSE(check_user_accessible_range(va, len, spectre_validation));

#endif

  END_TEST;
}

static bool arch_is_user_accessible_range() { return check_user_accessible_range_test(false); }

static bool validate_user_address_range() { return check_user_accessible_range_test(true); }

UNITTEST_START_TESTCASE(aspace_tests)
VM_UNITTEST(vmm_alloc_smoke_test)
VM_UNITTEST(vmm_alloc_contiguous_smoke_test)
VM_UNITTEST(multiple_regions_test)
VM_UNITTEST(vmm_alloc_zero_size_fails)
VM_UNITTEST(vmm_alloc_bad_specific_pointer_fails)
VM_UNITTEST(vmm_alloc_contiguous_missing_flag_commit_fails)
VM_UNITTEST(vmm_alloc_contiguous_zero_size_fails)
VM_UNITTEST(vmaspace_create_smoke_test)
VM_UNITTEST(vmaspace_create_invalid_ranges)
VM_UNITTEST(vmaspace_alloc_smoke_test)
VM_UNITTEST(vmaspace_accessed_test_untagged)
#if defined(__aarch64__)
VM_UNITTEST(vmaspace_accessed_test_tagged)
#endif
VM_UNITTEST(vmaspace_usercopy_accessed_fault_test)
VM_UNITTEST(vmaspace_free_unaccessed_page_tables_test)
VM_UNITTEST(vmaspace_merge_mapping_test)
VM_UNITTEST(vm_mapping_attribution_commit_decommit_test)
VM_UNITTEST(vm_mapping_attribution_map_unmap_test)
VM_UNITTEST(vm_mapping_attribution_merge_test)
VM_UNITTEST(arch_is_user_accessible_range)
VM_UNITTEST(validate_user_address_range)
VM_UNITTEST(arch_noncontiguous_map)
VM_UNITTEST(arch_vm_aspace_protect_split_pages)
VM_UNITTEST(arch_vm_aspace_protect_split_pages_out_of_memory)
VM_UNITTEST(vm_kernel_region_test)
VM_UNITTEST(region_list_get_alloc_spot_test)
VM_UNITTEST(region_list_get_alloc_spot_no_memory_test)
VM_UNITTEST(region_list_find_region_test)
VM_UNITTEST(region_list_include_or_higher_test)
VM_UNITTEST(region_list_upper_bound_test)
VM_UNITTEST(region_list_is_range_available_test)
VM_UNITTEST(address_region_enumerator_mapping_test)
VM_UNITTEST(dump_all_aspaces)  // Run last
UNITTEST_END_TESTCASE(aspace_tests, "aspace", "VmAspace / ArchVmAspace / VMAR tests")

}  // namespace vm_unittest
