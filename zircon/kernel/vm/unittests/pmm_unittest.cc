// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test_helper.h"

namespace vm_unittest {

namespace {

// Helper class for managing a PmmNode with real pages. AllocRange and AllocContiguous are not
// supported by the managed PmmNode object. Only a single instance can exist at a time.
class ManagedPmmNode {
 public:
  static constexpr size_t kNumPages = 64;
  static constexpr size_t kDefaultWatermark = kNumPages / 2;
  static constexpr size_t kDefaultDebounce = 2;

  // Number of pages to alloc from the default config to put the node in a low mem state.
  static constexpr size_t kDefaultLowMemAlloc = ManagedPmmNode::kNumPages -
                                                ManagedPmmNode::kDefaultWatermark +
                                                ManagedPmmNode::kDefaultDebounce;

  explicit ManagedPmmNode(const uint64_t* watermarks = kDefaultArray, uint8_t watermark_count = 1,
                          uint64_t debounce = kDefaultDebounce) {
    list_node list = LIST_INITIAL_VALUE(list);
    ZX_ASSERT(pmm_alloc_pages(kNumPages, 0, &list) == ZX_OK);
    vm_page_t* page;
    list_for_every_entry (&list, page, vm_page_t, queue_node) {
      page->set_state(VM_PAGE_STATE_FREE);
    }
    node_.AddFreePages(&list);

    ZX_ASSERT(node_.InitReclamation(watermarks, watermark_count, debounce * PAGE_SIZE, this,
                                    StateCallback) == ZX_OK);
    node_.InitRequestThread();
  }

  ~ManagedPmmNode() {
    list_node list = LIST_INITIAL_VALUE(list);
    zx_status_t status = node_.AllocPages(kNumPages, 0, &list);
    ASSERT(status == ZX_OK);
    vm_page_t* page;
    list_for_every_entry (&list, page, vm_page_t, queue_node) {
      page->set_state(VM_PAGE_STATE_ALLOC);
    }
    pmm_free(&list);
  }

  uint8_t cur_level() const { return cur_level_; }
  PmmNode& node() { return node_; }

 private:
  PmmNode node_;
  uint8_t cur_level_ = MAX_WATERMARK_COUNT + 1;

  static void StateCallback(void* context, uint8_t level) {
    ManagedPmmNode* instance = reinterpret_cast<ManagedPmmNode*>(context);
    instance->cur_level_ = level;
  }

  static constexpr uint64_t kDefaultArray[1] = {kDefaultWatermark * PAGE_SIZE};
};

}  // namespace

// Allocates a single page, translates it to a vm_page_t and frees it.
static bool pmm_smoke_test() {
  BEGIN_TEST;
  paddr_t pa;
  vm_page_t* page;

  zx_status_t status = pmm_alloc_page(0, &page, &pa);
  ASSERT_EQ(ZX_OK, status, "pmm_alloc single page");
  ASSERT_NONNULL(page, "pmm_alloc single page");
  ASSERT_NE(0u, pa, "pmm_alloc single page");

  vm_page_t* page2 = paddr_to_vm_page(pa);
  ASSERT_EQ(page2, page, "paddr_to_vm_page on single page");

  pmm_free_page(page);
  END_TEST;
}

// Allocates one page and frees it.
static bool pmm_alloc_contiguous_one_test() {
  BEGIN_TEST;
  list_node list = LIST_INITIAL_VALUE(list);
  paddr_t pa;
  size_t count = 1U;
  zx_status_t status = pmm_alloc_contiguous(count, 0, PAGE_SIZE_SHIFT, &pa, &list);
  ASSERT_EQ(ZX_OK, status, "pmm_alloc_contiguous returned failure\n");
  ASSERT_EQ(count, list_length(&list), "pmm_alloc_contiguous list size is wrong");
  ASSERT_NONNULL(paddr_to_physmap(pa));
  pmm_free(&list);
  END_TEST;
}

// Allocates more than one page and frees them.
static bool pmm_node_multi_alloc_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  static constexpr size_t alloc_count = ManagedPmmNode::kNumPages / 2;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(alloc_count, 0, &list);
  EXPECT_EQ(ZX_OK, status, "pmm_alloc_pages a few pages");
  EXPECT_EQ(alloc_count, list_length(&list), "pmm_alloc_pages a few pages list count");

  status = node.node().AllocPages(alloc_count, 0, &list);
  EXPECT_EQ(ZX_OK, status, "pmm_alloc_pages a few pages");
  EXPECT_EQ(2 * alloc_count, list_length(&list), "pmm_alloc_pages a few pages list count");

  node.node().FreeList(&list);
  END_TEST;
}

// Allocates one page from the bulk allocation api.
static bool pmm_node_singlton_list_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(1, 0, &list);
  EXPECT_EQ(ZX_OK, status, "pmm_alloc_pages a few pages");
  EXPECT_EQ(1ul, list_length(&list), "pmm_alloc_pages a few pages list count");

  node.node().FreeList(&list);
  END_TEST;
}

// Allocates too many pages and makes sure it fails nicely.
static bool pmm_node_oversized_alloc_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(ManagedPmmNode::kNumPages + 1, 0, &list);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "pmm_alloc_pages failed to alloc");
  EXPECT_TRUE(list_is_empty(&list), "pmm_alloc_pages list is empty");

  END_TEST;
}

// Checks the correctness of the reported watermark level.
static bool pmm_node_watermark_level_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  EXPECT_EQ(node.cur_level(), 1);

  while (node.node().CountFreePages() >
         (ManagedPmmNode::kDefaultWatermark - ManagedPmmNode::kDefaultDebounce) + 1) {
    vm_page_t* page;
    zx_status_t status = node.node().AllocPage(0, &page, nullptr);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(node.cur_level(), 1);
    list_add_tail(&list, &page->queue_node);
  }

  vm_page_t* page;
  zx_status_t status = node.node().AllocPage(0, &page, nullptr);

  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(node.cur_level(), 0);
  list_add_tail(&list, &page->queue_node);

  while (!list_is_empty(&list)) {
    node.node().FreePage(list_remove_head_type(&list, vm_page_t, queue_node));
    uint8_t expected = node.node().CountFreePages() >=
                       ManagedPmmNode::kDefaultWatermark + ManagedPmmNode::kDefaultDebounce;
    EXPECT_EQ(node.cur_level(), expected);
  }

  END_TEST;
}

// Checks the multiple watermark case given in the documentation for |pmm_init_reclamation|.
static bool pmm_node_multi_watermark_level_test() {
  BEGIN_TEST;

  uint64_t watermarks[4] = {20 * PAGE_SIZE, 40 * PAGE_SIZE, 45 * PAGE_SIZE, 55 * PAGE_SIZE};

  ManagedPmmNode node(watermarks, 4, 15);
  list_node list = LIST_INITIAL_VALUE(list);

  EXPECT_EQ(node.cur_level(), 4);

  auto consume_fn = [&](uint64_t level, uint64_t lower_limit) -> bool {
    while (node.node().CountFreePages() > lower_limit) {
      EXPECT_EQ(node.cur_level(), level);

      vm_page_t* page;
      zx_status_t status = node.node().AllocPage(0, &page, nullptr);
      EXPECT_EQ(ZX_OK, status);
      list_add_tail(&list, &page->queue_node);
    }
    return true;
  };

  EXPECT_TRUE(consume_fn(4, 40));
  EXPECT_TRUE(consume_fn(2, 25));
  EXPECT_TRUE(consume_fn(1, 5));

  auto release_fn = [&](uint64_t level, uint64_t upper_limit) -> bool {
    while (node.node().CountFreePages() < upper_limit) {
      EXPECT_EQ(node.cur_level(), level);
      node.node().FreePage(list_remove_head_type(&list, vm_page_t, queue_node));
    }
    return true;
  };

  EXPECT_TRUE(release_fn(0, 35));
  EXPECT_TRUE(release_fn(1, 55));
  EXPECT_TRUE(release_fn(4, node.kNumPages));

  END_TEST;
}

// A more abstract test for multiple watermarks.
static bool pmm_node_multi_watermark_level_test2() {
  BEGIN_TEST;

  static constexpr uint64_t kInterval = 7;
  uint64_t watermarks[MAX_WATERMARK_COUNT];
  for (unsigned i = 0; i < MAX_WATERMARK_COUNT; i++) {
    watermarks[i] = (i + 1) * kInterval * PAGE_SIZE;
  }
  static_assert(kInterval * MAX_WATERMARK_COUNT < ManagedPmmNode::kNumPages);

  ManagedPmmNode node(watermarks, MAX_WATERMARK_COUNT);
  list_node list = LIST_INITIAL_VALUE(list);

  EXPECT_EQ(node.cur_level(), MAX_WATERMARK_COUNT);

  uint64_t count = ManagedPmmNode::kNumPages;
  while (node.node().CountFreePages() > 0) {
    vm_page_t* page;
    zx_status_t status = node.node().AllocPage(0, &page, nullptr);
    EXPECT_EQ(ZX_OK, status);
    list_add_tail(&list, &page->queue_node);

    count--;
    uint64_t expected = ktl::min(static_cast<uint64_t>(MAX_WATERMARK_COUNT),
                                 (count + ManagedPmmNode::kDefaultDebounce - 1) / kInterval);
    EXPECT_EQ(node.cur_level(), expected);
  }

  vm_page_t* page;
  zx_status_t status = node.node().AllocPage(0, &page, nullptr);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, status);
  EXPECT_EQ(node.cur_level(), 0);

  while (!list_is_empty(&list)) {
    node.node().FreePage(list_remove_head_type(&list, vm_page_t, queue_node));
    count++;
    uint64_t expected = ktl::min(static_cast<uint64_t>(MAX_WATERMARK_COUNT),
                                 count > ManagedPmmNode::kDefaultDebounce
                                     ? (count - ManagedPmmNode::kDefaultDebounce) / kInterval
                                     : 0);
    EXPECT_EQ(node.cur_level(), expected);
  }

  END_TEST;
}

// Checks sync allocation failure when the node is in a low-memory state.
static bool pmm_node_oom_sync_alloc_failure_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  // Put the node in an oom state and make sure allocation fails.
  zx_status_t status = node.node().AllocPages(ManagedPmmNode::kDefaultLowMemAlloc, 0, &list);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(node.cur_level(), 0);

  vm_page_t* page;
  status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);

  // Free the list and make sure allocations work again.
  node.node().FreeList(&list);

  status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
  EXPECT_EQ(ZX_OK, status);

  node.node().FreePage(page);

  END_TEST;
}

// Checks async allocation queued while the node is in a low-memory state.
static bool pmm_node_delayed_alloc_test() {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(ManagedPmmNode::kDefaultLowMemAlloc, 0, &list);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(node.cur_level(), 0);

  vm_page_t* page;
  status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);

  static constexpr uint64_t kOffset = 1;
  static constexpr uint64_t kLen = 3 * ManagedPmmNode::kDefaultDebounce;
  TestPageRequest request(&node.node(), kOffset, kLen);
  node.node().AllocPages(0, request.request());

  EXPECT_EQ(node.cur_level(), 0);
  for (unsigned i = 0; i < 2 * ManagedPmmNode::kDefaultDebounce; i++) {
    node.node().FreePage(list_remove_head_type(&list, vm_page, queue_node));
  }
  EXPECT_EQ(node.cur_level(), 1);

  uint64_t expected_off, expected_len, actual_supplied;
  request.WaitForAvailable(&expected_off, &expected_len, &actual_supplied);
  EXPECT_EQ(expected_off, kOffset);
  EXPECT_EQ(expected_len, kLen);
  EXPECT_EQ(actual_supplied, 2 * ManagedPmmNode::kDefaultDebounce);
  EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::no_slack(ZX_TIME_INFINITE_PAST)),
            ZX_ERR_TIMED_OUT);

  node.node().FreeList(&list);

  request.WaitForAvailable(&expected_off, &expected_len, &actual_supplied);
  EXPECT_EQ(expected_off, kOffset + 2 * ManagedPmmNode::kDefaultDebounce);
  EXPECT_EQ(expected_len, kLen - 2 * ManagedPmmNode::kDefaultDebounce);
  EXPECT_EQ(actual_supplied, kLen - 2 * ManagedPmmNode::kDefaultDebounce);
  EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::no_slack(ZX_TIME_INFINITE)), ZX_OK);

  EXPECT_EQ(list_length(request.page_list()), kLen);

  node.node().FreeList(request.page_list());

  END_TEST;
}

// Checks async allocation queued while the node is not in a low-memory state.
static bool pmm_node_delayed_alloc_no_lowmem_test() {
  BEGIN_TEST;
  ManagedPmmNode node;

  TestPageRequest request(&node.node(), 0, 1);
  node.node().AllocPages(0, request.request());

  uint64_t expected_off, expected_len, actual_supplied;
  request.WaitForAvailable(&expected_off, &expected_len, &actual_supplied);
  EXPECT_EQ(expected_off, 0ul);
  EXPECT_EQ(expected_len, 1ul);
  EXPECT_EQ(actual_supplied, 1ul);
  EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::no_slack(ZX_TIME_INFINITE)), ZX_OK);

  EXPECT_EQ(list_length(request.page_list()), 1ul);

  node.node().FreeList(request.page_list());

  END_TEST;
}

// Checks swapping out the page_request_t backing a request, either before the request
// starts being serviced or while the request is being serviced (depending on |early|).
static bool pmm_node_delayed_alloc_swap_test_helper(bool early) {
  BEGIN_TEST;
  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(ManagedPmmNode::kDefaultLowMemAlloc, 0, &list);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(node.cur_level(), 0);

  vm_page_t* page;
  status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);

  TestPageRequest request(&node.node(), 0, 1);
  node.node().AllocPages(0, request.request());

  page_request_t new_mem = *request.request();

  if (early) {
    node.node().SwapRequest(request.request(), &new_mem);
  }

  EXPECT_EQ(node.cur_level(), 0);
  for (unsigned i = 0; i < 2 * ManagedPmmNode::kDefaultDebounce; i++) {
    node.node().FreePage(list_remove_head_type(&list, vm_page, queue_node));
  }
  EXPECT_EQ(node.cur_level(), 1);

  if (!early) {
    EXPECT_EQ(request.on_pages_avail_evt().Wait(Deadline::infinite()), ZX_OK);
    node.node().SwapRequest(request.request(), &new_mem);
  }

  uint64_t expected_off, expected_len, actual_supplied;
  request.WaitForAvailable(&expected_off, &expected_len, &actual_supplied);
  EXPECT_EQ(expected_off, 0ul);
  EXPECT_EQ(expected_len, 1ul);
  EXPECT_EQ(actual_supplied, 1ul);
  EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::infinite()), ZX_OK);
  EXPECT_EQ(list_length(request.page_list()), 1ul);

  node.node().FreeList(&list);
  node.node().FreeList(request.page_list());

  END_TEST;
}

static bool pmm_node_delayed_alloc_swap_early_test() {
  return pmm_node_delayed_alloc_swap_test_helper(true);
}

static bool pmm_node_delayed_alloc_swap_late_test() {
  return pmm_node_delayed_alloc_swap_test_helper(false);
}

// Checks cancelling the page_request_t backing a request, either before the request
// starts being serviced or while the request is being serviced (depending on |early|).
static bool pmm_node_delayed_alloc_clear_test_helper(bool early) {
  BEGIN_TEST;

  ManagedPmmNode node;
  list_node list = LIST_INITIAL_VALUE(list);

  zx_status_t status = node.node().AllocPages(ManagedPmmNode::kDefaultLowMemAlloc, 0, &list);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(node.cur_level(), 0);

  vm_page_t* page;
  status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);

  TestPageRequest request(&node.node(), 0, 1);
  node.node().AllocPages(0, request.request());

  if (early) {
    EXPECT_TRUE(request.Cancel());
  }

  EXPECT_EQ(node.cur_level(), 0);
  for (unsigned i = 0; i < 2 * ManagedPmmNode::kDefaultDebounce; i++) {
    node.node().FreePage(list_remove_head_type(&list, vm_page, queue_node));
  }
  EXPECT_EQ(node.cur_level(), 1);

  if (!early) {
    EXPECT_EQ(request.on_pages_avail_evt().Wait(Deadline::infinite()), ZX_OK);
    EXPECT_FALSE(request.Cancel());
    EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::infinite()), ZX_OK);
  } else {
    EXPECT_EQ(request.drop_ref_evt().Wait(Deadline::no_slack(ZX_TIME_INFINITE_PAST)),
              ZX_ERR_TIMED_OUT);
    request.drop_ref_evt().Signal();
  }

  EXPECT_EQ(list_length(request.page_list()), 0ul);
  node.node().FreeList(&list);

  END_TEST;
}

static bool pmm_node_delayed_alloc_clear_early_test() {
  return pmm_node_delayed_alloc_clear_test_helper(true);
}

static bool pmm_node_delayed_alloc_clear_late_test() {
  return pmm_node_delayed_alloc_clear_test_helper(false);
}

static bool pmm_checker_test_with_fill_size(size_t fill_size) {
  BEGIN_TEST;

  PmmChecker checker;

  // Starts off unarmed.
  EXPECT_FALSE(checker.IsArmed());

  // Borrow a real page from the PMM, ask the checker to validate it.  See that because the checker
  // is not armed, |ValidatePattern| still returns true even though the page has no pattern.
  vm_page_t* page;
  EXPECT_EQ(pmm_alloc_page(0, &page), ZX_OK);
  page->set_state(VM_PAGE_STATE_FREE);
  auto p = static_cast<uint8_t*>(paddr_to_physmap(page->paddr()));
  memset(p, 0, PAGE_SIZE);
  EXPECT_TRUE(checker.ValidatePattern(page));
  checker.AssertPattern(page);

  // Set the fill size and see that |GetFillSize| returns the size.
  checker.SetFillSize(fill_size);
  EXPECT_EQ(fill_size, checker.GetFillSize());

  // Arm the checker and see that |ValidatePattern| returns false.
  checker.Arm();
  EXPECT_TRUE(checker.IsArmed());
  EXPECT_FALSE(checker.ValidatePattern(page));

  // Fill with pattern one less than the fill size and see that it does not pass validation.
  memset(p, 0, fill_size - 1);
  EXPECT_FALSE(checker.ValidatePattern(page));

  // Fill with the full pattern and see that it validates.
  checker.FillPattern(page);
  for (size_t i = 0; i < fill_size; ++i) {
    EXPECT_NE(0, p[i]);
  }
  EXPECT_TRUE(checker.ValidatePattern(page));

  // Corrupt the page after the first |fill_size| bytes and see that the corruption is not detected.
  if (fill_size < PAGE_SIZE) {
    p[fill_size] = 1;
    EXPECT_TRUE(checker.ValidatePattern(page));
  }

  // Corrupt the page within the first |fill_size| bytes and see that the corruption is detected.
  p[fill_size - 1] = 1;
  EXPECT_FALSE(checker.ValidatePattern(page));

  // Disarm the checker and see that it now passes.
  checker.Disarm();
  EXPECT_FALSE(checker.IsArmed());
  EXPECT_TRUE(checker.ValidatePattern(page));
  checker.AssertPattern(page);

  page->set_state(VM_PAGE_STATE_ALLOC);
  pmm_free_page(page);

  END_TEST;
}

static bool pmm_checker_test() {
  BEGIN_TEST;

  EXPECT_TRUE(pmm_checker_test_with_fill_size(8));
  EXPECT_TRUE(pmm_checker_test_with_fill_size(16));
  EXPECT_TRUE(pmm_checker_test_with_fill_size(512));
  EXPECT_TRUE(pmm_checker_test_with_fill_size(PAGE_SIZE));

  END_TEST;
}

static bool pmm_checker_action_from_string_test() {
  BEGIN_TEST;

  EXPECT_FALSE(PmmChecker::ActionFromString(""));
  EXPECT_FALSE(PmmChecker::ActionFromString("blah"));
  EXPECT_EQ(static_cast<uint32_t>(PmmChecker::Action::OOPS),
            static_cast<uint32_t>(PmmChecker::ActionFromString("oops").value()));
  EXPECT_EQ(static_cast<uint32_t>(PmmChecker::Action::PANIC),
            static_cast<uint32_t>(PmmChecker::ActionFromString("panic").value()));

  END_TEST;
}

static bool pmm_checker_is_valid_fill_size_test() {
  BEGIN_TEST;

  EXPECT_FALSE(PmmChecker::IsValidFillSize(0));
  EXPECT_FALSE(PmmChecker::IsValidFillSize(7));
  EXPECT_FALSE(PmmChecker::IsValidFillSize(9));
  EXPECT_FALSE(PmmChecker::IsValidFillSize(PAGE_SIZE + 8));
  EXPECT_FALSE(PmmChecker::IsValidFillSize(PAGE_SIZE * 2));

  EXPECT_TRUE(PmmChecker::IsValidFillSize(8));
  EXPECT_TRUE(PmmChecker::IsValidFillSize(16));
  EXPECT_TRUE(PmmChecker::IsValidFillSize(24));
  EXPECT_TRUE(PmmChecker::IsValidFillSize(512));
  EXPECT_TRUE(PmmChecker::IsValidFillSize(PAGE_SIZE));

  END_TEST;
}

static bool pmm_get_arena_info_test() {
  BEGIN_TEST;

  const size_t num_arenas = pmm_num_arenas();
  ASSERT_GT(num_arenas, 0u);

  fbl::AllocChecker ac;
  auto buffer = ktl::unique_ptr<pmm_arena_info_t[]>(new (&ac) pmm_arena_info_t[num_arenas]);
  ASSERT(ac.check());
  const size_t buffer_size = num_arenas * sizeof(pmm_arena_info_t);

  // Not enough room for one.
  zx_status_t status = pmm_get_arena_info(1, 0, buffer.get(), sizeof(pmm_arena_info_t) - 1);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);

  // Asking for none.
  status = pmm_get_arena_info(0, 0, buffer.get(), buffer_size);
  ASSERT_EQ(status, ZX_ERR_OUT_OF_RANGE);

  // Asking for more than exist.
  status = pmm_get_arena_info(num_arenas + 1, 0, buffer.get(), buffer_size);
  ASSERT_EQ(status, ZX_ERR_OUT_OF_RANGE);

  // Attempting to skip them all.
  status = pmm_get_arena_info(1, num_arenas, buffer.get(), buffer_size);
  ASSERT_EQ(status, ZX_ERR_OUT_OF_RANGE);

  // Asking for one.
  status = pmm_get_arena_info(1, 0, buffer.get(), buffer_size);
  ASSERT_EQ(status, ZX_OK);

  // Asking for them all.
  status = pmm_get_arena_info(num_arenas, 0, buffer.get(), buffer_size);
  ASSERT_EQ(status, ZX_OK);

  // See they are in ascending order by base.
  paddr_t prev = 0;
  for (unsigned i = 0; i < num_arenas; ++i) {
    if (i == 0) {
      ASSERT_GE(buffer[i].base, prev);
    } else {
      ASSERT_GT(buffer[i].base, prev);
    }
    prev = buffer[i].base;
    ASSERT_GT(buffer[i].size, 0u);
  }

  END_TEST;
}

static bool pq_add_remove() {
  BEGIN_TEST;

  PageQueues pq;

  // Pretend we have an allocated page
  vm_page_t test_page = {};
  test_page.set_state(VM_PAGE_STATE_OBJECT);

  // Need a VMO to claim our pager backed page is in
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(0, 0, PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  // Put the page in each queue and make sure it shows up
  pq.SetWired(&test_page);
  EXPECT_TRUE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 1, 0}));

  pq.Remove(&test_page);
  EXPECT_FALSE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 0, 0}));

  pq.SetUnswappable(&test_page);
  EXPECT_TRUE(pq.DebugPageIsUnswappable(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 1, 0, 0}));

  pq.Remove(&test_page);
  EXPECT_FALSE(pq.DebugPageIsUnswappable(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 0, 0}));

  // Pretend we have some kind of pointer to a VmObjectPaged (this will never get dereferenced)
  pq.SetPagerBacked(&test_page, vmo->DebugGetCowPages().get(), 0);
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{1, 0, 0, 0}, 0, 0, 0}));

  pq.Remove(&test_page);
  EXPECT_FALSE(pq.DebugPageIsPagerBacked(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 0, 0}));

  END_TEST;
}

static bool pq_move_queues() {
  BEGIN_TEST;

  PageQueues pq;

  // Pretend we have an allocated page
  vm_page_t test_page = {};
  test_page.set_state(VM_PAGE_STATE_OBJECT);

  // Need a VMO to claim our pager backed page is in
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(0, 0, PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  // Move the page between queues.
  pq.SetWired(&test_page);
  EXPECT_TRUE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 1, 0}));

  pq.MoveToUnswappable(&test_page);
  EXPECT_FALSE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugPageIsUnswappable(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 1, 0, 0}));

  pq.MoveToPagerBacked(&test_page, vmo->DebugGetCowPages().get(), 0);
  EXPECT_FALSE(pq.DebugPageIsUnswappable(&test_page));
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{1, 0, 0, 0}, 0, 0, 0}));

  pq.MoveToWired(&test_page);
  EXPECT_FALSE(pq.DebugPageIsPagerBacked(&test_page));
  EXPECT_TRUE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 1, 0}));

  pq.Remove(&test_page);
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 0, 0}));

  END_TEST;
}

static bool pq_move_self_queue() {
  BEGIN_TEST;

  PageQueues pq;

  // Pretend we have an allocated page
  vm_page_t test_page = {};
  test_page.set_state(VM_PAGE_STATE_OBJECT);

  // Move the page into the queue it is already in.
  pq.SetWired(&test_page);
  EXPECT_TRUE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 1, 0}));

  pq.MoveToWired(&test_page);
  EXPECT_TRUE(pq.DebugPageIsWired(&test_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 1, 0}));

  pq.Remove(&test_page);
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0}, 0, 0, 0}));

  END_TEST;
}

static bool pq_rotate_queue() {
  BEGIN_TEST;

  PageQueues pq;

  // Pretend we have a couple of allocated pages.
  vm_page_t wired_page = {};
  vm_page_t pager_page = {};
  wired_page.set_state(VM_PAGE_STATE_OBJECT);
  pager_page.set_state(VM_PAGE_STATE_OBJECT);

  // Need a VMO to claim our pager backed page is in.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(0, 0, PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  // Put the pages in and validate initial state.
  pq.SetWired(&wired_page);
  pq.SetPagerBacked(&pager_page, vmo->DebugGetCowPages().get(), 0);
  EXPECT_TRUE(pq.DebugPageIsWired(&wired_page));
  size_t queue;
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&pager_page, &queue));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{1, 0, 0, 0}, 0, 1, 0}));
  EXPECT_EQ(queue, 0u);

  // Gradually rotate the queue.
  pq.RotatePagerBackedQueues();
  EXPECT_TRUE(pq.DebugPageIsWired(&wired_page));
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&pager_page, &queue));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0, 1, 0, 0}, 0, 1, 0}));
  EXPECT_EQ(queue, 1u);

  pq.RotatePagerBackedQueues();
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0, 0, 1, 0}, 0, 1, 0}));
  pq.RotatePagerBackedQueues();
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0, 0, 0, 1}, 0, 1, 0}));

  // Further rotations should not move the page.
  pq.RotatePagerBackedQueues();
  EXPECT_TRUE(pq.DebugPageIsWired(&wired_page));
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&pager_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0, 0, 0, 1}, 0, 1, 0}));

  // Moving the page should bring it back to the first queue.
  pq.MoveToPagerBacked(&pager_page, vmo->DebugGetCowPages().get(), 0);
  EXPECT_TRUE(pq.DebugPageIsWired(&wired_page));
  EXPECT_TRUE(pq.DebugPageIsPagerBacked(&pager_page));
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{1, 0, 0, 0}, 0, 1, 0}));

  // Just double check one rotation.
  pq.RotatePagerBackedQueues();
  EXPECT_TRUE(pq.DebugQueueCounts() == ((PageQueues::Counts){{0, 1, 0, 0}, 0, 1, 0}));

  pq.Remove(&wired_page);
  pq.Remove(&pager_page);

  END_TEST;
}

static bool physmap_for_each_gap_test() {
  BEGIN_TEST;

  struct Gap {
    vaddr_t base;
    size_t size;
  };

  fbl::Vector<Gap> actual_gaps;
  fbl::AllocChecker ac;
  auto PushBack = [&](vaddr_t base, size_t size) {
    actual_gaps.push_back({base, size}, &ac);
    ASSERT(ac.check());
  };

  {
    // No arenas, [ ].
    actual_gaps.reset();
    physmap_for_each_gap(PushBack, nullptr, 0);
    // One gap covering the entire physmap.
    ASSERT_EQ(actual_gaps.size(), 1u);
    ASSERT_EQ(actual_gaps[0].base, PHYSMAP_BASE);
    ASSERT_EQ(actual_gaps[0].size, PHYSMAP_SIZE);
  }

  {
    // One arena, no gaps, [A].
    actual_gaps.reset();
    pmm_arena_info_t arenas[] = {
        {"test-arena", 0, PHYSMAP_BASE_PHYS, PHYSMAP_SIZE},
    };
    physmap_for_each_gap(PushBack, arenas, ktl::size(arenas));
    // No gaps.
    ASSERT_EQ(actual_gaps.size(), 0u);
  }

  {
    // One arena, gap at bottom, [ A].
    actual_gaps.reset();
    const size_t gap_size = 0x1000;
    const size_t arena_size = PHYSMAP_SIZE - gap_size;
    pmm_arena_info_t arenas[] = {
        {"test-arena", 0, PHYSMAP_BASE_PHYS + gap_size, arena_size},
    };
    physmap_for_each_gap(PushBack, arenas, ktl::size(arenas));
    // One gap.
    ASSERT_EQ(actual_gaps.size(), 1u);
    ASSERT_EQ(actual_gaps[0].base, PHYSMAP_BASE);
    ASSERT_EQ(actual_gaps[0].size, gap_size);
  }

  {
    // One arena, gap at top, [A ].
    actual_gaps.reset();
    const size_t gap_size = 0x5000;
    const size_t arena_size = PHYSMAP_SIZE - gap_size;
    pmm_arena_info_t arenas[] = {
        {"test-arena", 0, PHYSMAP_BASE_PHYS, arena_size},
    };
    physmap_for_each_gap(PushBack, arenas, ktl::size(arenas));
    // One gap.
    ASSERT_EQ(actual_gaps.size(), 1u);
    ASSERT_EQ(actual_gaps[0].base, PHYSMAP_BASE + arena_size);
    ASSERT_EQ(actual_gaps[0].size, gap_size);
  }

  {
    // Two arenas, no gaps, [AA].
    actual_gaps.reset();
    const size_t size = PHYSMAP_SIZE / 2;
    pmm_arena_info_t arenas[] = {
        {"test-arena", 0, PHYSMAP_BASE_PHYS, size},
        {"test-arena", 0, PHYSMAP_BASE_PHYS + size, size},
    };
    physmap_for_each_gap(PushBack, arenas, ktl::size(arenas));
    // No gaps.
    ASSERT_EQ(actual_gaps.size(), 0u);
  }

  {
    // Two arenas, three gaps, [ A A ].
    actual_gaps.reset();
    const size_t gap1_size = 0x300000;
    const size_t arena1_offset = gap1_size;
    const size_t arena1_size = 0x1000000;
    const size_t gap2_size = 0x35000;
    const size_t arena2_offset = gap1_size + arena1_size + gap2_size;
    const size_t arena2_size = 0xff1000000;
    pmm_arena_info_t arenas[] = {
        {"test-arena", 0, PHYSMAP_BASE_PHYS + arena1_offset, arena1_size},
        {"test-arena", 0, PHYSMAP_BASE_PHYS + arena2_offset, arena2_size},
    };
    physmap_for_each_gap(PushBack, arenas, ktl::size(arenas));
    // Three gaps.
    ASSERT_EQ(actual_gaps.size(), 3u);
    ASSERT_EQ(actual_gaps[0].base, PHYSMAP_BASE);
    ASSERT_EQ(actual_gaps[0].size, gap1_size);
    ASSERT_EQ(actual_gaps[1].base, PHYSMAP_BASE + arena1_offset + arena1_size);
    ASSERT_EQ(actual_gaps[1].size, gap2_size);
    const size_t arena3_offset = gap1_size + arena1_size + gap2_size + arena2_size;
    ASSERT_EQ(actual_gaps[2].base, PHYSMAP_BASE + arena3_offset);
    ASSERT_EQ(actual_gaps[2].size, PHYSMAP_SIZE - arena3_offset);
  }

  END_TEST;
}

#if __has_feature(address_sanitizer)
static bool kasan_detects_use_after_free() {
  BEGIN_TEST;
  ManagedPmmNode node;

  vm_page_t* page;
  paddr_t paddr;
  zx_status_t status = node.node().AllocPage(PMM_ALLOC_DELAY_OK, &page, &paddr);
  ASSERT_EQ(ZX_OK, status, "pmm_alloc_page one page");
  ASSERT_NE(paddr, 0UL);
  EXPECT_EQ(0UL, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(paddr_to_physmap(paddr)),
                                         PAGE_SIZE));
  node.node().FreePage(page);
  EXPECT_TRUE(asan_entire_region_is_poisoned(reinterpret_cast<uintptr_t>(paddr_to_physmap(paddr)),
                                             PAGE_SIZE));
  END_TEST;
}
#endif  // __has_feature(address_sanitizer)

UNITTEST_START_TESTCASE(pmm_tests)
VM_UNITTEST(pmm_smoke_test)
VM_UNITTEST(pmm_alloc_contiguous_one_test)
VM_UNITTEST(pmm_node_multi_alloc_test)
VM_UNITTEST(pmm_node_singlton_list_test)
VM_UNITTEST(pmm_node_oversized_alloc_test)
VM_UNITTEST(pmm_node_watermark_level_test)
VM_UNITTEST(pmm_node_multi_watermark_level_test)
VM_UNITTEST(pmm_node_multi_watermark_level_test2)
VM_UNITTEST(pmm_node_oom_sync_alloc_failure_test)
VM_UNITTEST(pmm_node_delayed_alloc_test)
VM_UNITTEST(pmm_node_delayed_alloc_no_lowmem_test)
VM_UNITTEST(pmm_node_delayed_alloc_swap_early_test)
VM_UNITTEST(pmm_node_delayed_alloc_swap_late_test)
VM_UNITTEST(pmm_node_delayed_alloc_clear_early_test)
VM_UNITTEST(pmm_node_delayed_alloc_clear_late_test)
VM_UNITTEST(pmm_checker_test)
VM_UNITTEST(pmm_checker_action_from_string_test)
VM_UNITTEST(pmm_checker_is_valid_fill_size_test)
VM_UNITTEST(pmm_get_arena_info_test)
UNITTEST_END_TESTCASE(pmm_tests, "pmm", "Physical memory manager tests")

UNITTEST_START_TESTCASE(page_queues_tests)
VM_UNITTEST(pq_add_remove)
VM_UNITTEST(pq_move_queues)
VM_UNITTEST(pq_move_self_queue)
VM_UNITTEST(pq_rotate_queue)
UNITTEST_END_TESTCASE(page_queues_tests, "pq", "PageQueues tests")

UNITTEST_START_TESTCASE(physmap_tests)
VM_UNITTEST(physmap_for_each_gap_test)
UNITTEST_END_TESTCASE(physmap_tests, "physmap", "physmap tests")

#if __has_feature(address_sanitizer)
UNITTEST_START_TESTCASE(kasan_pmm_tests)
VM_UNITTEST(kasan_detects_use_after_free)
UNITTEST_END_TESTCASE(kasan_pmm_tests, "kasan_pmm", "kasan pmm tests")
#endif  // __has_feature(address_sanitizer)

}  // namespace vm_unittest
