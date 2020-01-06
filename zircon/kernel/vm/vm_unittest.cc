// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <kernel/semaphore.h>
#include <ktl/move.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#include "pmm_node.h"

static const uint kArchRwFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

namespace {

// Helper class for managing a PmmNode with fake pages. AllocRange and AllocContiguous are not
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
    for (auto& p : pages_) {
      list_add_tail(&list, &p.queue_node);
    }
    node_.AddFreePages(&list);

    ASSERT(instance_ == nullptr);
    instance_ = this;

    ZX_ASSERT(node_.InitReclamation(watermarks, watermark_count, debounce * PAGE_SIZE,
                                    StateCallback) == ZX_OK);
    node_.InitRequestThread();
  }

  ~ManagedPmmNode() {
    list_node list = LIST_INITIAL_VALUE(list);
    zx_status_t status = node_.AllocPages(kNumPages, 0, &list);
    ASSERT(status == ZX_OK);

    ASSERT(instance_ == this);
    instance_ = nullptr;
  }

  uint8_t cur_level() const { return cur_level_; }
  PmmNode& node() { return node_; }

 private:
  PmmNode node_;
  vm_page_t pages_[kNumPages] = {};
  uint8_t cur_level_ = MAX_WATERMARK_COUNT + 1;

  static void StateCallback(uint8_t level) { instance_->cur_level_ = level; }
  static ManagedPmmNode* instance_;

  static constexpr uint64_t kDefaultArray[1] = {kDefaultWatermark * PAGE_SIZE};
};

ManagedPmmNode* ManagedPmmNode::instance_ = nullptr;

class TestPageRequest {
 public:
  TestPageRequest(PmmNode* node, uint64_t off, uint64_t len)
      : node_(node), request_({off, len, pages_available_cb, drop_ref_cb, this, {}}) {}

  ~TestPageRequest() {
    ASSERT(drop_ref_evt_.Wait(Deadline::no_slack(ZX_TIME_INFINITE_PAST)) == ZX_OK);
  }

  void WaitForAvailable(uint64_t* expected_off, uint64_t* expected_len, uint64_t* actual_supplied) {
    expected_off_ = expected_off;
    expected_len_ = expected_len;
    actual_supplied_ = actual_supplied;
    avail_sem_.Post();

    wait_for_avail_sem_.Wait(Deadline::infinite());
  }

  bool Cancel() {
    bool res = node_->ClearRequest(&request_);
    actual_supplied_ = nullptr;
    avail_sem_.Post();
    return res;
  }

  page_request_t* request() { return &request_; }
  Event& drop_ref_evt() { return drop_ref_evt_; }
  list_node* page_list() { return &page_list_; }
  Event& on_pages_avail_evt() { return on_pages_avail_evt_; }

 private:
  void OnPagesAvailable(uint64_t offset, uint64_t count, uint64_t* actual_supplied) {
    on_pages_avail_evt_.Signal();
    avail_sem_.Wait(Deadline::infinite());

    if (actual_supplied_) {
      *expected_off_ = offset;
      *expected_len_ = count;
      *actual_supplied = 0;

      while (count) {
        vm_page_t* page;
        zx_status_t status = node_->AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
        if (status != ZX_OK) {
          break;
        }

        count--;
        *actual_supplied += 1;
        list_add_tail(&page_list_, &page->queue_node);
      }
      *actual_supplied_ = *actual_supplied;
    } else {
      *actual_supplied = count;
    }

    wait_for_avail_sem_.Post();
    on_pages_avail_evt_.Unsignal();
  }

  void OnDropRef() { drop_ref_evt_.Signal(); }

  PmmNode* node_;
  page_request_t request_;

  list_node page_list_ = LIST_INITIAL_VALUE(page_list_);

  Semaphore wait_for_avail_sem_;
  Semaphore avail_sem_;
  Event on_pages_avail_evt_;
  uint64_t* expected_off_;
  uint64_t* expected_len_;
  uint64_t* actual_supplied_;

  Event drop_ref_evt_;

  static void pages_available_cb(void* ctx, uint64_t offset, uint64_t count,
                                 uint64_t* actual_supplied) {
    static_cast<TestPageRequest*>(ctx)->OnPagesAvailable(offset, count, actual_supplied);
  }
  static void drop_ref_cb(void* ctx) { static_cast<TestPageRequest*>(ctx)->OnDropRef(); }
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
    uint64_t expected = fbl::min(static_cast<uint64_t>(MAX_WATERMARK_COUNT),
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
    uint64_t expected = fbl::min(static_cast<uint64_t>(MAX_WATERMARK_COUNT),
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

static uint32_t test_rand(uint32_t seed) { return (seed = seed * 1664525 + 1013904223); }

// fill a region of memory with a pattern based on the address of the region
static void fill_region(uintptr_t seed, void* _ptr, size_t len) {
  uint32_t* ptr = (uint32_t*)_ptr;

  ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

  uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
  val ^= (uint32_t)(seed >> 32);
#endif
  for (size_t i = 0; i < len / 4; i++) {
    ptr[i] = val;

    val = test_rand(val);
  }
}

// test a region of memory against a known pattern
static bool test_region(uintptr_t seed, void* _ptr, size_t len) {
  uint32_t* ptr = (uint32_t*)_ptr;

  ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

  uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
  val ^= (uint32_t)(seed >> 32);
#endif
  for (size_t i = 0; i < len / 4; i++) {
    if (ptr[i] != val) {
      unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", &ptr[i], i, ptr[i], val);
      return false;
    }

    val = test_rand(val);
  }

  return true;
}

static bool fill_and_test(void* ptr, size_t len) {
  BEGIN_TEST;

  // fill it with a pattern
  fill_region((uintptr_t)ptr, ptr, len);

  // test that the pattern is read back properly
  auto result = test_region((uintptr_t)ptr, ptr, len);
  EXPECT_TRUE(result, "testing region for corruption");

  END_TEST;
}

// Allocates a region in kernel space, reads/writes it, then destroys it.
static bool vmm_alloc_smoke_test() {
  BEGIN_TEST;
  static const size_t alloc_size = 256 * 1024;

  // allocate a region of memory
  void* ptr;
  auto kaspace = VmAspace::kernel_aspace();
  auto err = kaspace->Alloc("test", alloc_size, &ptr, 0, 0, kArchRwFlags);
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
  void* ptr;
  static const size_t alloc_size = 16 * 1024;

  fbl::RefPtr<VmAspace> aspace = VmAspace::Create(0, "test aspace");
  ASSERT_NONNULL(aspace, "VmAspace::Create pointer");

  vmm_aspace_t* old_aspace = get_current_thread()->aspace;
  vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(aspace.get()));

  // allocate region 0
  zx_status_t err = aspace->Alloc("test0", alloc_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");
  ASSERT_NONNULL(ptr, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // allocate region 1
  err = aspace->Alloc("test1", 16384, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");
  ASSERT_NONNULL(ptr, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // allocate region 2
  err = aspace->Alloc("test2", 16384, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "VmAspace::Alloc region of memory");
  ASSERT_NONNULL(ptr, "VmAspace::Alloc region of memory");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
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
  auto aspace = VmAspace::Create(0, "test aspace");
  zx_status_t err = aspace->Destroy();
  EXPECT_EQ(ZX_OK, err, "VmAspace::Destroy");
  END_TEST;
}

// Allocates a vm address space object directly, maps something on it,
// allows it to go out of scope.
static bool vmaspace_alloc_smoke_test() {
  BEGIN_TEST;
  auto aspace = VmAspace::Create(0, "test aspace2");

  void* ptr;
  auto err = aspace->Alloc("test", PAGE_SIZE, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "allocating region\n");

  // destroy the aspace, which should drop all the internal refs to it
  err = aspace->Destroy();
  EXPECT_EQ(ZX_OK, err, "VmAspace::Destroy");

  // drop the ref held by this pointer
  aspace.reset();
  END_TEST;
}

// Doesn't do anything, just prints all aspaces.
// Should be run after all other tests so that people can manually comb
// through the output for leaked test aspaces.
static bool dump_all_aspaces() {
  BEGIN_TEST;
  unittest_printf("verify there are no test aspaces left around\n");
  DumpAllAspaces(/*verbose*/ true);
  END_TEST;
}

// Creates a vm object.
static bool vmo_create_test() {
  BEGIN_TEST;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE(vmo);
  EXPECT_FALSE(vmo->is_contiguous(), "vmo is not contig\n");
  EXPECT_FALSE(vmo->is_resizable(), "vmo is not resizable\n");
  END_TEST;
}

static bool vmo_create_maximum_size() {
  BEGIN_TEST;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0xfffffffffffe0000, &vmo);
  EXPECT_EQ(status, ZX_OK, "should be ok\n");

  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0xfffffffffffe1000, &vmo);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "should be too large\n");
  END_TEST;
}

// Creates a vm object, commits memory.
static bool vmo_commit_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ret = vmo->CommitRange(0, alloc_size);
  ASSERT_EQ(ZX_OK, ret, "committing vm object\n");
  EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), PAGE_SIZE * vmo->AttributedPages(),
            "committing vm object\n");
  END_TEST;
}

// Creates a paged VMO, pins it, and tries operations that should unpin it.
static bool vmo_pin_test() {
  BEGIN_TEST;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->Pin(PAGE_SIZE, alloc_size);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "pinning out of range\n");
  status = vmo->Pin(PAGE_SIZE, 0);
  EXPECT_EQ(ZX_OK, status, "pinning range of len 0\n");
  status = vmo->Pin(alloc_size + PAGE_SIZE, 0);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "pinning out-of-range of len 0\n");

  status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
  status = vmo->Pin(0, alloc_size);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");

  status = vmo->CommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "committing range\n");

  status = vmo->Pin(0, alloc_size);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
  status = vmo->Pin(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
  status = vmo->Pin(0, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");

  status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning committed range\n");

  status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(3 * PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");

  vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);

  status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  status = vmo->CommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "committing range\n");
  status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning committed range\n");

  status = vmo->Resize(0);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "resizing pinned range\n");

  vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);

  status = vmo->Resize(0);
  EXPECT_EQ(ZX_OK, status, "resizing unpinned range\n");

  END_TEST;
}

// Creates a page VMO and pins the same pages multiple times
static bool vmo_multiple_pin_test() {
  BEGIN_TEST;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->CommitRange(0, alloc_size);
  EXPECT_EQ(ZX_OK, status, "committing range\n");

  status = vmo->Pin(0, alloc_size);
  EXPECT_EQ(ZX_OK, status, "pinning whole range\n");
  status = vmo->Pin(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning subrange\n");

  for (unsigned int i = 1; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
    status = vmo->Pin(0, PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "pinning first page max times\n");
  }
  status = vmo->Pin(0, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "page is pinned too much\n");

  vmo->Unpin(0, alloc_size);
  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(5 * PAGE_SIZE, alloc_size - 5 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  vmo->Unpin(PAGE_SIZE, 4 * PAGE_SIZE);
  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  for (unsigned int i = 2; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
    vmo->Unpin(0, PAGE_SIZE);
  }
  status = vmo->DecommitRange(0, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting unpinned range\n");

  vmo->Unpin(0, PAGE_SIZE);
  status = vmo->DecommitRange(0, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  END_TEST;
}

// Creates a vm object, commits odd sized memory.
static bool vmo_odd_size_commit_test() {
  BEGIN_TEST;
  static const size_t alloc_size = 15;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ret = vmo->CommitRange(0, alloc_size);
  EXPECT_EQ(ZX_OK, ret, "committing vm object\n");
  EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), PAGE_SIZE * vmo->AttributedPages(),
            "committing vm object\n");
  END_TEST;
}

static bool vmo_create_physical_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* vm_page;
  zx_status_t status = pmm_alloc_page(0, &vm_page, &pa);
  uint32_t cache_policy;

  ASSERT_EQ(ZX_OK, status, "vm page allocation\n");
  ASSERT_TRUE(vm_page);

  fbl::RefPtr<VmObject> vmo;
  status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");
  cache_policy = vmo->GetMappingCachePolicy();
  EXPECT_EQ(ARCH_MMU_FLAG_UNCACHED, cache_policy, "check initial cache policy");
  EXPECT_TRUE(vmo->is_contiguous(), "check contiguous");

  pmm_free_page(vm_page);

  END_TEST;
}

// Creates a vm object that commits contiguous memory.
static bool vmo_create_contiguous_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  EXPECT_TRUE(vmo->is_contiguous(), "vmo is contig\n");

  paddr_t last_pa;
  auto lookup_func = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
    paddr_t* last_pa = static_cast<paddr_t*>(ctx);
    if (index != 0 && *last_pa + PAGE_SIZE != pa) {
      return ZX_ERR_BAD_STATE;
    }
    *last_pa = pa;
    return ZX_OK;
  };
  status = vmo->Lookup(0, alloc_size, lookup_func, &last_pa);
  EXPECT_EQ(status, ZX_OK, "vmo lookup\n");

  END_TEST;
}

// Make sure decommitting is disallowed
static bool vmo_contiguous_decommit_test() {
  BEGIN_TEST;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
  status = vmo->DecommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
  status = vmo->DecommitRange(alloc_size - PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");

  // Make sure all pages are still present and contiguous
  paddr_t last_pa;
  auto lookup_func = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
    paddr_t* last_pa = static_cast<paddr_t*>(ctx);
    if (index != 0 && *last_pa + PAGE_SIZE != pa) {
      return ZX_ERR_BAD_STATE;
    }
    *last_pa = pa;
    return ZX_OK;
  };
  status = vmo->Lookup(0, alloc_size, lookup_func, &last_pa);
  ASSERT_EQ(status, ZX_OK, "vmo lookup\n");

  END_TEST;
}

// Creats a vm object, maps it, precommitted.
static bool vmo_precommitted_map_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                   kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, demand paged.
static bool vmo_demand_paged_map_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, drops ref before unmapping.
static bool vmo_dropped_ref_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(ktl::move(vmo), "test", 0, alloc_size, &ptr, 0,
                                   VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  EXPECT_NULL(vmo, "dropped ref to object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, fills it with data, unmaps,
// maps again somewhere else.
static bool vmo_remap_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                   kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");

  // map it again
  ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                              kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  // test that the pattern is still valid
  bool result = test_region((uintptr_t)ptr, ptr, alloc_size);
  EXPECT_TRUE(result, "testing region for corruption");

  err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, fills it with data, maps it a second time and
// third time somwehere else.
static bool vmo_double_remap_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test0", 0, alloc_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // map it again
  void* ptr2;
  ret = ka->MapObjectInternal(vmo, "test1", 0, alloc_size, &ptr2, 0, 0, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object second time");
  EXPECT_NE(ptr, ptr2, "second mapping is different");

  // test that the pattern is still valid
  bool result = test_region((uintptr_t)ptr, ptr2, alloc_size);
  EXPECT_TRUE(result, "testing region for corruption");

  // map it a third time with an offset
  void* ptr3;
  static const size_t alloc_offset = PAGE_SIZE;
  ret = ka->MapObjectInternal(vmo, "test2", alloc_offset, alloc_size - alloc_offset, &ptr3, 0, 0,
                              kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object third time");
  EXPECT_NE(ptr3, ptr2, "third mapping is different");
  EXPECT_NE(ptr3, ptr, "third mapping is different");

  // test that the pattern is still valid
  int mc = memcmp((uint8_t*)ptr + alloc_offset, ptr3, alloc_size - alloc_offset);
  EXPECT_EQ(0, mc, "testing region for corruption");

  ret = ka->FreeRegion((vaddr_t)ptr3);
  EXPECT_EQ(ZX_OK, ret, "unmapping object third time");

  ret = ka->FreeRegion((vaddr_t)ptr2);
  EXPECT_EQ(ZX_OK, ret, "unmapping object second time");

  ret = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, ret, "unmapping object");
  END_TEST;
}

static bool vmo_read_write_smoke_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;

  // create object
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  // create test buffer
  fbl::AllocChecker ac;
  fbl::Array<uint8_t> a(new (&ac) uint8_t[alloc_size], alloc_size);
  ASSERT_TRUE(ac.check());
  fill_region(99, a.data(), alloc_size);

  // write to it, make sure it seems to work with valid args
  zx_status_t err = vmo->Write(a.data(), 0, 0);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  err = vmo->Write(a.data(), 0, 37);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  err = vmo->Write(a.data(), 99, 37);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  // can't write past end
  err = vmo->Write(a.data(), 0, alloc_size + 47);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // can't write past end
  err = vmo->Write(a.data(), 31, alloc_size + 47);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // should return an error because out of range
  err = vmo->Write(a.data(), alloc_size + 99, 42);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // map the object
  auto ka = VmAspace::kernel_aspace();
  uint8_t* ptr;
  err = ka->MapObjectInternal(vmo, "test", 0, alloc_size, (void**)&ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "mapping object");

  // write to it at odd offsets
  err = vmo->Write(a.data(), 31, 4197);
  EXPECT_EQ(ZX_OK, err, "writing to object");
  int cmpres = memcmp(ptr + 31, a.data(), 4197);
  EXPECT_EQ(0, cmpres, "reading from object");

  // write to it, filling the object completely
  err = vmo->Write(a.data(), 0, alloc_size);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  // test that the data was actually written to it
  bool result = test_region(99, ptr, alloc_size);
  EXPECT_TRUE(result, "writing to object");

  // unmap it
  ka->FreeRegion((vaddr_t)ptr);

  // test that we can read from it
  fbl::Array<uint8_t> b(new (&ac) uint8_t[alloc_size], alloc_size);
  ASSERT_TRUE(ac.check(), "can't allocate buffer");

  err = vmo->Read(b.data(), 0, alloc_size);
  EXPECT_EQ(ZX_OK, err, "reading from object");

  // validate the buffer is valid
  cmpres = memcmp(b.data(), a.data(), alloc_size);
  EXPECT_EQ(0, cmpres, "reading from object");

  // read from it at an offset
  err = vmo->Read(b.data(), 31, 4197);
  EXPECT_EQ(ZX_OK, err, "reading from object");
  cmpres = memcmp(b.data(), a.data() + 31, 4197);
  EXPECT_EQ(0, cmpres, "reading from object");
  END_TEST;
}

static bool vmo_cache_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* vm_page;
  zx_status_t status = pmm_alloc_page(0, &vm_page, &pa);
  auto ka = VmAspace::kernel_aspace();
  uint32_t cache_policy = ARCH_MMU_FLAG_UNCACHED_DEVICE;
  uint32_t cache_policy_get;
  void* ptr;

  ASSERT_TRUE(vm_page);
  // Test that the flags set/get properly
  {
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    cache_policy_get = vmo->GetMappingCachePolicy();
    EXPECT_NE(cache_policy, cache_policy_get, "check initial cache policy");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try set");
    cache_policy_get = vmo->GetMappingCachePolicy();
    EXPECT_EQ(cache_policy, cache_policy_get, "compare flags");
  }

  // Test valid flags
  for (uint32_t i = 0; i <= ARCH_MMU_FLAG_CACHE_MASK; i++) {
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try setting valid flags");
  }

  // Test invalid flags
  for (uint32_t i = ARCH_MMU_FLAG_CACHE_MASK + 1; i < 32; i++) {
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(i), "try set with invalid flags");
  }

  // Test valid flags with invalid flags
  {
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x5), "bad 0x5");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xA), "bad 0xA");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x55), "bad 0x55");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xAA), "bad 0xAA");
  }

  // Test that changing policy while mapped is blocked
  {
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    ASSERT_EQ(ZX_OK,
              ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0, kArchRwFlags),
              "map vmo");
    EXPECT_EQ(ZX_ERR_BAD_STATE, vmo->SetMappingCachePolicy(cache_policy), "set flags while mapped");
    EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "set flags after unmapping");
    ASSERT_EQ(ZX_OK,
              ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0, kArchRwFlags),
              "map vmo again");
    EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
  }

  pmm_free_page(vm_page);
  END_TEST;
}

static bool vmo_lookup_test() {
  BEGIN_TEST;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  size_t pages_seen = 0;
  auto lookup_fn = [](void* context, size_t offset, size_t index, paddr_t pa) {
    size_t* pages_seen = static_cast<size_t*>(context);
    (*pages_seen)++;
    return ZX_OK;
  };
  status = vmo->Lookup(0, alloc_size, lookup_fn, &pages_seen);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on uncommitted pages\n");
  EXPECT_EQ(0u, pages_seen, "lookup on uncommitted pages\n");
  pages_seen = 0;

  status = vmo->CommitRange(PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "committing vm object\n");
  EXPECT_EQ(static_cast<size_t>(1), vmo->AttributedPages(), "committing vm object\n");

  // Should fail, since first page isn't mapped
  status = vmo->Lookup(0, alloc_size, lookup_fn, &pages_seen);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on partially committed pages\n");
  EXPECT_EQ(0u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  // Should fail, but see the mapped page
  status = vmo->Lookup(PAGE_SIZE, alloc_size - PAGE_SIZE, lookup_fn, &pages_seen);
  EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on partially committed pages\n");
  EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  // Should succeed
  status = vmo->Lookup(PAGE_SIZE, PAGE_SIZE, lookup_fn, &pages_seen);
  EXPECT_EQ(ZX_OK, status, "lookup on partially committed pages\n");
  EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  // Commit the rest
  status = vmo->CommitRange(0, alloc_size);
  EXPECT_EQ(ZX_OK, status, "committing vm object\n");
  EXPECT_EQ(alloc_size, PAGE_SIZE * vmo->AttributedPages(), "committing vm object\n");

  status = vmo->Lookup(0, alloc_size, lookup_fn, &pages_seen);
  EXPECT_EQ(ZX_OK, status, "lookup on partially committed pages\n");
  EXPECT_EQ(alloc_size / PAGE_SIZE, pages_seen, "lookup on partially committed pages\n");

  END_TEST;
}

static bool vmo_lookup_clone_test() {
  BEGIN_TEST;
  static const size_t page_count = 4;
  static const size_t alloc_size = PAGE_SIZE * page_count;
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  vmo->set_user_id(ZX_KOID_KERNEL);

  // Commit the whole original VMO and the first and last page of the clone.
  status = vmo->CommitRange(0, alloc_size);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");

  fbl::RefPtr<VmObject> clone;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, alloc_size,
                            false, &clone);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  ASSERT_TRUE(clone, "vmobject creation\n");

  clone->set_user_id(ZX_KOID_KERNEL);

  status = clone->CommitRange(0, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  status = clone->CommitRange(alloc_size - PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");

  // Lookup the paddrs for both VMOs.
  paddr_t vmo_lookup[page_count] = {};
  paddr_t clone_lookup[page_count] = {};
  auto lookup_func = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
    static_cast<paddr_t*>(ctx)[index] = pa;
    return ZX_OK;
  };
  status = vmo->Lookup(0, alloc_size, lookup_func, &vmo_lookup);
  EXPECT_EQ(ZX_OK, status, "vmo lookup\n");
  status = clone->Lookup(0, alloc_size, lookup_func, &clone_lookup);
  EXPECT_EQ(ZX_OK, status, "vmo lookup\n");

  // Check that lookup returns a valid paddr for each index and that
  // they match/don't match when appropriate.
  for (unsigned i = 0; i < page_count; i++) {
    EXPECT_NE(0ul, vmo_lookup[i], "Bad paddr\n");
    EXPECT_NE(0ul, clone_lookup[i], "Bad paddr\n");
    if (i == 0 || i == page_count - 1) {
      EXPECT_NE(vmo_lookup[i], clone_lookup[i], "paddr mismatch");
    } else {
      EXPECT_EQ(vmo_lookup[i], clone_lookup[i], "paddr mismatch");
    }
  }

  END_TEST;
}

static bool vmo_clone_removes_write_test() {
  BEGIN_TEST;

  // Create and map a VMO.
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "vmo create");
  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  status = ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                 kArchRwFlags);
  EXPECT_EQ(ZX_OK, status, "map vmo");

  // Query the aspace and validate there is a writable mapping.
  paddr_t paddr_writable;
  uint mmu_flags;
  status = ka->arch_aspace().Query(reinterpret_cast<vaddr_t>(ptr), &paddr_writable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status, "query aspace");

  EXPECT_TRUE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE, "mapping is writable check");

  // Clone the VMO, which causes the parent to have to downgrade any mappings to read-only so that
  // copy-on-write can take place. Need to set a fake user id so that the COW creation code is
  // happy.
  vmo->set_user_id(42);
  fbl::RefPtr<VmObject> clone;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, true,
                            &clone);
  EXPECT_EQ(ZX_OK, status, "create clone");

  // Aspace should now have a read only mapping with the same underlying page.
  paddr_t paddr_readable;
  status = ka->arch_aspace().Query(reinterpret_cast<vaddr_t>(ptr), &paddr_readable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status, "query aspace");
  EXPECT_FALSE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE, "mapping is read only check");
  EXPECT_EQ(paddr_writable, paddr_readable, "mapping has same page");

  // Cleanup.
  status = ka->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(ZX_OK, status, "unmapping object");

  END_TEST;
}

static bool vmo_zero_scan_test() {
  BEGIN_TEST;

  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status);

  // Initially uncommitted, which should not count as having zero pages.
  EXPECT_EQ(0u, vmo->ScanForZeroPages(false));

  // Create a user mapping that we can read/write from.
  fbl::RefPtr<VmAspace> user_aspace = fbl::RefPtr(vmm_aspace_to_obj(get_current_thread()->aspace));
  fbl::RefPtr<VmAddressRegion> root_user_vmar = user_aspace->RootVmar();
  fbl::RefPtr<VmMapping> mapping;
  status = root_user_vmar->CreateVmMapping(
      0, PAGE_SIZE, 0, VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE, vmo, 0,
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, "unittest", &mapping);
  EXPECT_EQ(ZX_OK, status);
  auto unmap_user = fbl::MakeAutoCall([&]() {
    if (mapping) {
      mapping->Unmap(mapping->base(), mapping->size());
    }
  });
  volatile int32_t* addr = reinterpret_cast<volatile int32_t*>(mapping->base());

  // Validate that this mapping reads as zeros
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mapping->base(), 0u));
  EXPECT_EQ(0, *addr);

  // Reading from the page should not have committed anything, zero or otherwise.
  EXPECT_EQ(0u, vmo->ScanForZeroPages(false));

  // IF we write to the page, this should make it committed.
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mapping->base(), VMM_PF_FLAG_WRITE));
  *addr = 0;
  EXPECT_EQ(1u, vmo->ScanForZeroPages(false));

  // Check that changing the contents effects the zero page count.
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mapping->base(), VMM_PF_FLAG_WRITE));
  *addr = 42;
  EXPECT_EQ(0u, vmo->ScanForZeroPages(false));
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mapping->base(), VMM_PF_FLAG_WRITE));
  *addr = 0;
  EXPECT_EQ(1u, vmo->ScanForZeroPages(false));

  // Scanning should drop permissions in the hardware page table from write to read-only.
  paddr_t paddr_readable;
  uint mmu_flags;
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mapping->base(), VMM_PF_FLAG_WRITE));
  *addr = 0;
  status = user_aspace->arch_aspace().Query(reinterpret_cast<vaddr_t>(addr), &paddr_readable,
                                            &mmu_flags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE);
  vmo->ScanForZeroPages(false);
  status = user_aspace->arch_aspace().Query(reinterpret_cast<vaddr_t>(addr), &paddr_readable,
                                            &mmu_flags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_FALSE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE);

  // Pinning the page should prevent it from being counted.
  EXPECT_EQ(1u, vmo->ScanForZeroPages(false));
  EXPECT_EQ(ZX_OK, vmo->Pin(0, PAGE_SIZE));
  EXPECT_EQ(0u, vmo->ScanForZeroPages(false));
  vmo->Unpin(0, PAGE_SIZE);
  EXPECT_EQ(1u, vmo->ScanForZeroPages(false));

  // Creating a kernel mapping should prevent any counting from occurring.
  VmAspace* kernel_aspace = VmAspace::kernel_aspace();
  void* ptr;
  status = kernel_aspace->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, &ptr, 0,
                                            VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(0u, vmo->ScanForZeroPages(false));
  kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(1u, vmo->ScanForZeroPages(false));

  END_TEST;
}

// TODO(ZX-1431): The ARM code's error codes are always ZX_ERR_INTERNAL, so
// special case that.
#if ARCH_ARM64
#define MMU_EXPECT_EQ(exp, act, msg) EXPECT_EQ(ZX_ERR_INTERNAL, act, msg)
#else
#define MMU_EXPECT_EQ(exp, act, msg) EXPECT_EQ(exp, act, msg)
#endif

static bool arch_noncontiguous_map() {
  BEGIN_TEST;

  // Get some phys pages to test on
  paddr_t phys[3];
  struct list_node phys_list = LIST_INITIAL_VALUE(phys_list);
  zx_status_t status = pmm_alloc_pages(fbl::count_of(phys), 0, &phys_list);
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
    ArchVmAspace aspace;
    status = aspace.Init(USER_ASPACE_BASE, USER_ASPACE_SIZE, 0);
    ASSERT_EQ(ZX_OK, status, "failed to init aspace\n");

    // Attempt to map a set of vm_page_t
    size_t mapped;
    vaddr_t base = USER_ASPACE_BASE + 10 * PAGE_SIZE;
    status = aspace.Map(base, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ, &mapped);
    ASSERT_EQ(ZX_OK, status, "failed first map\n");
    EXPECT_EQ(fbl::count_of(phys), mapped, "weird first map\n");
    for (size_t i = 0; i < fbl::count_of(phys); ++i) {
      paddr_t paddr;
      uint mmu_flags;
      status = aspace.Query(base + i * PAGE_SIZE, &paddr, &mmu_flags);
      EXPECT_EQ(ZX_OK, status, "bad first map\n");
      EXPECT_EQ(phys[i], paddr, "bad first map\n");
      EXPECT_EQ(ARCH_MMU_FLAG_PERM_READ, mmu_flags, "bad first map\n");
    }

    // Attempt to map again, should fail
    status = aspace.Map(base, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ, &mapped);
    MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

    // Attempt to map partially ovelapping, should fail
    status = aspace.Map(base + 2 * PAGE_SIZE, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ,
                        &mapped);
    MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");
    status = aspace.Map(base - 2 * PAGE_SIZE, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ,
                        &mapped);
    MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

    // No entries should have been created by the partial failures
    status = aspace.Query(base - 2 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base - PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base + 3 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
    status = aspace.Query(base + 4 * PAGE_SIZE, nullptr, nullptr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");

    status = aspace.Unmap(base, fbl::count_of(phys), &mapped);
    ASSERT_EQ(ZX_OK, status, "failed unmap\n");
    EXPECT_EQ(fbl::count_of(phys), mapped, "weird unmap\n");
    status = aspace.Destroy();
    EXPECT_EQ(ZX_OK, status, "failed to destroy aspace\n");
  }

  pmm_free(&phys_list);

  END_TEST;
}

namespace {
bool AddPage(VmPageList* pl, vm_page_t* page, uint64_t offset) {
  if (!pl) {
    return false;
  }
  VmPageOrMarker* slot = pl->LookupOrAllocate(offset);
  if (!slot) {
    return false;
  }
  if (!slot->IsEmpty()) {
    return false;
  }
  *slot = VmPageOrMarker::Page(page);
  return true;
}

bool AddMarker(VmPageList* pl, uint64_t offset) {
  if (!pl) {
    return false;
  }
  VmPageOrMarker* slot = pl->LookupOrAllocate(offset);
  if (!slot) {
    return false;
  }
  if (!slot->IsEmpty()) {
    return false;
  }
  *slot = VmPageOrMarker::Marker();
  return true;
}
}  // namespace

// Basic test that checks adding/removing a page
static bool vmpl_add_remove_page_test() {
  BEGIN_TEST;

  VmPageList pl;
  vm_page_t test_page{};

  EXPECT_TRUE(AddPage(&pl, &test_page, 0));

  EXPECT_EQ(&test_page, pl.Lookup(0)->Page(), "unexpected page\n");
  EXPECT_FALSE(pl.IsEmpty());
  EXPECT_FALSE(pl.HasNoPages());

  vm_page* remove_page = pl.RemovePage(0).ReleasePage();
  EXPECT_EQ(&test_page, remove_page, "unexpected page\n");
  EXPECT_TRUE(pl.RemovePage(0).IsEmpty(), "unexpected page\n");

  EXPECT_TRUE(pl.IsEmpty());
  EXPECT_TRUE(pl.HasNoPages());

  END_TEST;
}

// Basic test of setting and getting markers.
static bool vmpl_basic_marker_test() {
  BEGIN_TEST;

  VmPageList pl;

  EXPECT_TRUE(pl.IsEmpty());
  EXPECT_TRUE(pl.HasNoPages());

  EXPECT_TRUE(AddMarker(&pl, 0));

  EXPECT_TRUE(pl.Lookup(0)->IsMarker());

  EXPECT_FALSE(pl.IsEmpty());
  EXPECT_TRUE(pl.HasNoPages());

  END_TEST;
}

// Test for freeing a range of pages
static bool vmpl_free_pages_test() {
  BEGIN_TEST;

  VmPageList pl;
  constexpr uint32_t kCount = 3 * VmPageListNode::kPageFanOut;
  vm_page_t test_pages[kCount] = {};

  // Install alternating pages and markers.
  for (uint32_t i = 0; i < kCount; i++) {
    EXPECT_TRUE(AddPage(&pl, test_pages + i, i * 2 * PAGE_SIZE));
    EXPECT_TRUE(AddMarker(&pl, (i * 2 + 1) * PAGE_SIZE));
  }

  list_node_t list;
  list_initialize(&list);
  pl.RemovePages(PAGE_SIZE * 2, (kCount - 1) * 2 * PAGE_SIZE, &list);
  for (unsigned i = 1; i < kCount - 2; i++) {
    EXPECT_TRUE(list_in_list(&test_pages[i].queue_node), "Not in free list");
  }

  for (uint32_t i = 0; i < kCount; i++) {
    VmPageOrMarker remove_page = pl.RemovePage(i * 2 * PAGE_SIZE);
    VmPageOrMarker remove_marker = pl.RemovePage((i * 2 + 1) * PAGE_SIZE);
    if (i == 0 || i == kCount - 1) {
      EXPECT_TRUE(remove_page.IsPage(), "missing page\n");
      EXPECT_TRUE(remove_marker.IsMarker(), "missing marker\n");
      EXPECT_EQ(test_pages + i, remove_page.ReleasePage(), "unexpected page\n");
    } else {
      EXPECT_TRUE(remove_page.IsEmpty(), "extra page\n");
      EXPECT_TRUE(remove_marker.IsEmpty(), "extra marker\n");
    }
  }

  END_TEST;
}

// Tests freeing the last page in a list
static bool vmpl_free_pages_last_page_test() {
  BEGIN_TEST;

  vm_page_t page{};

  VmPageList pl;
  EXPECT_TRUE(AddPage(&pl, &page, 0));

  EXPECT_EQ(&page, pl.Lookup(0)->Page(), "unexpected page\n");

  list_node_t list;
  list_initialize(&list);
  pl.RemoveAllPages(&list);
  EXPECT_TRUE(pl.IsEmpty(), "not empty\n");

  EXPECT_EQ(list_length(&list), 1u, "too many pages");
  EXPECT_EQ(list_remove_head_type(&list, vm_page_t, queue_node), &page, "wrong page");

  END_TEST;
}

static bool vmpl_near_last_offset_free() {
  BEGIN_TEST;

  vm_page_t page = {};

  bool at_least_one = false;
  for (uint64_t addr = 0xfffffffffff00000; addr != 0; addr += PAGE_SIZE) {
    VmPageList pl;
    if (AddPage(&pl, &page, addr)) {
      at_least_one = true;
      EXPECT_EQ(&page, pl.Lookup(addr)->Page(), "unexpected page\n");

      list_node_t list;
      list_initialize(&list);
      pl.RemoveAllPages(&list);

      EXPECT_EQ(list_length(&list), 1u, "too many pages");
      EXPECT_EQ(list_remove_head_type(&list, vm_page_t, queue_node), &page, "wrong page");
      EXPECT_TRUE(pl.IsEmpty(), "non-empty list\n");
    }
  }
  EXPECT_TRUE(at_least_one, "starting address too large");

  VmPageList pl2;
  EXPECT_NULL(pl2.LookupOrAllocate(0xfffffffffffe0000), "unexpected offset addable\n");

  END_TEST;
}

// Tests taking a page from the start of a VmPageListNode
static bool vmpl_take_single_page_even_test() {
  BEGIN_TEST;

  VmPageList pl;
  vm_page_t test_page{};
  vm_page_t test_page2{};
  EXPECT_TRUE(AddPage(&pl, &test_page, 0));
  EXPECT_TRUE(AddPage(&pl, &test_page2, PAGE_SIZE));

  VmPageSpliceList splice = pl.TakePages(0, PAGE_SIZE);

  EXPECT_EQ(&test_page, splice.Pop().ReleasePage(), "wrong page\n");
  EXPECT_TRUE(splice.IsDone(), "extra page\n");
  EXPECT_TRUE(pl.Lookup(0) == nullptr || pl.Lookup(0)->IsEmpty(), "duplicate page\n");

  EXPECT_EQ(&test_page2, pl.RemovePage(PAGE_SIZE).ReleasePage(), "remove failure\n");

  END_TEST;
}

// Tests taking a page from the middle of a VmPageListNode
static bool vmpl_take_single_page_odd_test() {
  BEGIN_TEST;

  VmPageList pl;
  vm_page_t test_page{};
  vm_page_t test_page2{};
  EXPECT_TRUE(AddPage(&pl, &test_page, 0));
  EXPECT_TRUE(AddPage(&pl, &test_page2, PAGE_SIZE));

  VmPageSpliceList splice = pl.TakePages(PAGE_SIZE, PAGE_SIZE);

  EXPECT_EQ(&test_page2, splice.Pop().ReleasePage(), "wrong page\n");
  EXPECT_TRUE(splice.IsDone(), "extra page\n");
  EXPECT_TRUE(pl.Lookup(PAGE_SIZE) == nullptr || pl.Lookup(PAGE_SIZE)->IsEmpty(),
              "duplicate page\n");

  EXPECT_EQ(&test_page, pl.RemovePage(0).ReleasePage(), "remove failure\n");

  END_TEST;
}

// Tests taking all the pages from a range of VmPageListNodes
static bool vmpl_take_all_pages_test() {
  BEGIN_TEST;

  VmPageList pl;
  constexpr uint32_t kCount = 3 * VmPageListNode::kPageFanOut;
  vm_page_t test_pages[kCount] = {};
  for (uint32_t i = 0; i < kCount; i++) {
    EXPECT_TRUE(AddPage(&pl, test_pages + i, i * 2 * PAGE_SIZE));
    EXPECT_TRUE(AddMarker(&pl, (i * 2 + 1) * PAGE_SIZE));
  }

  VmPageSpliceList splice = pl.TakePages(0, kCount * 2 * PAGE_SIZE);
  EXPECT_TRUE(pl.IsEmpty(), "non-empty list\n");

  for (uint32_t i = 0; i < kCount; i++) {
    EXPECT_EQ(test_pages + i, splice.Pop().ReleasePage(), "wrong page\n");
    EXPECT_TRUE(splice.Pop().IsMarker(), "expected marker\n");
  }
  EXPECT_TRUE(splice.IsDone(), "extra pages\n");

  END_TEST;
}

// Tests taking the middle pages from a range of VmPageListNodes
static bool vmpl_take_middle_pages_test() {
  BEGIN_TEST;

  VmPageList pl;
  constexpr uint32_t kCount = 3 * VmPageListNode::kPageFanOut;
  vm_page_t test_pages[kCount] = {};
  for (uint32_t i = 0; i < kCount; i++) {
    EXPECT_TRUE(AddPage(&pl, test_pages + i, i * PAGE_SIZE));
  }

  constexpr uint32_t kTakeOffset = VmPageListNode::kPageFanOut - 1;
  constexpr uint32_t kTakeCount = VmPageListNode::kPageFanOut + 2;
  VmPageSpliceList splice = pl.TakePages(kTakeOffset * PAGE_SIZE, kTakeCount * PAGE_SIZE);
  EXPECT_FALSE(pl.IsEmpty(), "non-empty list\n");

  for (uint32_t i = 0; i < kCount; i++) {
    if (kTakeOffset <= i && i < kTakeOffset + kTakeCount) {
      EXPECT_EQ(test_pages + i, splice.Pop().ReleasePage(), "wrong page\n");
    } else {
      EXPECT_EQ(test_pages + i, pl.RemovePage(i * PAGE_SIZE).ReleasePage(), "remove failure\n");
    }
  }
  EXPECT_TRUE(splice.IsDone(), "extra pages\n");

  END_TEST;
}

// Tests that gaps are preserved in the list
static bool vmpl_take_gap_test() {
  BEGIN_TEST;

  VmPageList pl;
  constexpr uint32_t kCount = VmPageListNode::kPageFanOut;
  constexpr uint32_t kGapSize = 2;
  vm_page_t test_pages[kCount] = {};
  for (uint32_t i = 0; i < kCount; i++) {
    uint64_t offset = (i * (kGapSize + 1)) * PAGE_SIZE;
    EXPECT_TRUE(AddPage(&pl, test_pages + i, offset));
  }

  constexpr uint32_t kListStart = PAGE_SIZE;
  constexpr uint32_t kListLen = (kCount * (kGapSize + 1) - 2) * PAGE_SIZE;
  VmPageSpliceList splice = pl.TakePages(kListStart, kListLen);

  EXPECT_EQ(test_pages, pl.RemovePage(0).ReleasePage(), "wrong page\n");
  EXPECT_TRUE(pl.Lookup(kListLen) == nullptr || pl.Lookup(kListLen)->IsEmpty(), "wrong page\n");

  for (uint64_t offset = kListStart; offset < kListStart + kListLen; offset += PAGE_SIZE) {
    auto page_idx = offset / PAGE_SIZE;
    if (page_idx % (kGapSize + 1) == 0) {
      EXPECT_EQ(test_pages + (page_idx / (kGapSize + 1)), splice.Pop().ReleasePage(),
                "wrong page\n");
    } else {
      EXPECT_TRUE(splice.Pop().IsEmpty(), "wrong page\n");
    }
  }
  EXPECT_TRUE(splice.IsDone(), "extra pages\n");

  END_TEST;
}

// Tests that cleaning up a splice list doesn't blow up
static bool vmpl_take_cleanup_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* page;

  zx_status_t status = pmm_alloc_page(0, &page, &pa);
  ASSERT_EQ(ZX_OK, status, "pmm_alloc single page");
  ASSERT_NONNULL(page, "pmm_alloc single page");
  ASSERT_NE(0u, pa, "pmm_alloc single page");

  page->set_state(VM_PAGE_STATE_OBJECT);
  page->object.pin_count = 0;

  VmPageList pl;
  EXPECT_TRUE(AddPage(&pl, page, 0));

  VmPageSpliceList splice = pl.TakePages(0, PAGE_SIZE);
  EXPECT_TRUE(!splice.IsDone(), "missing page\n");

  END_TEST;
}

// Helper function which takes an array of pages, builds a VmPageList, and then verifies that
// ForEveryPageInRange is correct when ZX_ERR_NEXT is returned for the |stop_idx|th entry.
static bool vmpl_page_gap_iter_test_body(vm_page_t** pages, uint32_t count, uint32_t stop_idx) {
  BEGIN_TEST;

  VmPageList list;
  for (uint32_t i = 0; i < count; i++) {
    if (pages[i]) {
      EXPECT_TRUE(AddPage(&list, pages[i], i * PAGE_SIZE));
    }
  }

  uint32_t idx = 0;
  zx_status_t s = list.ForEveryPageAndGapInRange(
      [pages, stop_idx, &idx](const VmPageOrMarker& p, auto off) {
        if (off != idx * PAGE_SIZE || !p.IsPage() || pages[idx] != p.Page()) {
          return ZX_ERR_INTERNAL;
        }
        if (idx == stop_idx) {
          return ZX_ERR_STOP;
        }
        idx++;
        return ZX_ERR_NEXT;
      },
      [pages, stop_idx, &idx](uint64_t gap_start, uint64_t gap_end) {
        for (auto o = gap_start; o < gap_end; o += PAGE_SIZE) {
          if (o != idx * PAGE_SIZE || pages[idx] != nullptr) {
            return ZX_ERR_INTERNAL;
          }
          if (idx == stop_idx) {
            return ZX_ERR_STOP;
          }
          idx++;
        }
        return ZX_ERR_NEXT;
      },
      0, count * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, s);
  ASSERT_EQ(stop_idx, idx);

  list_node_t free_list;
  list_initialize(&free_list);
  list.RemoveAllPages(&free_list);
  ASSERT_TRUE(list.IsEmpty());

  END_TEST;
}

// Test ForEveryPageInRange against all lists of size 4
static bool vmpl_page_gap_iter_test() {
  static constexpr uint32_t kCount = 4;
  static_assert((kCount & (kCount - 1)) == 0);

  vm_page_t pages[kCount] = {};
  vm_page_t* list[kCount] = {};
  for (unsigned i = 0; i < kCount; i++) {
    for (unsigned j = 0; j < (1 << kCount); j++) {
      for (unsigned k = 0; k < kCount; k++) {
        if (j & (1 << k)) {
          list[k] = pages + k;
        } else {
          list[k] = nullptr;
        }
      }

      if (!vmpl_page_gap_iter_test_body(list, kCount, i)) {
        return false;
      }
    }
  }
  return true;
}

static bool vmpl_merge_offset_test_helper(uint64_t list1_offset, uint64_t list2_offset) {
  BEGIN_TEST;

  VmPageList list;
  list.InitializeSkew(0, list1_offset);
  vm_page_t test_pages[6] = {};
  uint64_t offsets[6] = {
      VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset - PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset,
      3 * VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset - PAGE_SIZE,
      3 * VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset,
      5 * VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset - PAGE_SIZE,
      5 * VmPageListNode::kPageFanOut * PAGE_SIZE + list2_offset,
  };

  for (unsigned i = 0; i < 6; i++) {
    EXPECT_TRUE(AddPage(&list, test_pages + i, offsets[i]));
  }

  VmPageList list2;
  list2.InitializeSkew(list1_offset, list2_offset);

  list_node_t free_list;
  list_initialize(&free_list);
  list2.MergeFrom(
      list, offsets[1], offsets[5],
      [&](vm_page* page, uint64_t offset) {
        DEBUG_ASSERT(page == test_pages || page == test_pages + 5);
        DEBUG_ASSERT(offset == offsets[0] || offset == offsets[5]);
      },
      [&](vm_page* page, uint64_t offset) {
        DEBUG_ASSERT(page == test_pages + 1 || page == test_pages + 2 || page == test_pages + 3 ||
                     page == test_pages + 4);
        DEBUG_ASSERT(offset == offsets[1] || offset == offsets[2] || offset == offsets[3] ||
                     offsets[4]);
        return true;
      },
      &free_list);

  EXPECT_EQ(list_length(&free_list), 2ul);

  EXPECT_EQ(list2.RemovePage(0).ReleasePage(), test_pages + 1);
  EXPECT_EQ(list2.RemovePage(2 * VmPageListNode::kPageFanOut * PAGE_SIZE - PAGE_SIZE).ReleasePage(),
            test_pages + 2);
  EXPECT_EQ(list2.RemovePage(2 * VmPageListNode::kPageFanOut * PAGE_SIZE).ReleasePage(),
            test_pages + 3);
  EXPECT_EQ(list2.RemovePage(4 * VmPageListNode::kPageFanOut * PAGE_SIZE - PAGE_SIZE).ReleasePage(),
            test_pages + 4);

  EXPECT_TRUE(list2.HasNoPages());

  END_TEST;
}

static bool vmpl_merge_offset_test() {
  for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
    for (unsigned j = 0; j < VmPageListNode::kPageFanOut; j++) {
      if (!vmpl_merge_offset_test_helper(i * PAGE_SIZE, j * PAGE_SIZE)) {
        return false;
      }
    }
  }
  return true;
}

static bool vmpl_merge_overlap_test_helper(uint64_t list1_offset, uint64_t list2_offset) {
  BEGIN_TEST;

  VmPageList list;
  list.InitializeSkew(0, list1_offset);
  vm_page_t test_pages[4] = {};

  EXPECT_TRUE(AddPage(&list, test_pages, list2_offset));
  EXPECT_TRUE(AddPage(&list, test_pages + 1, list2_offset + 2 * PAGE_SIZE));

  VmPageList list2;
  list2.InitializeSkew(list1_offset, list2_offset);

  EXPECT_TRUE(AddPage(&list2, test_pages + 2, 0));
  EXPECT_TRUE(AddPage(&list2, test_pages + 3, PAGE_SIZE));

  list_node_t free_list;
  list_initialize(&free_list);
  list2.MergeFrom(
      list, list2_offset, list2_offset + 4 * PAGE_SIZE,
      [&](vm_page* page, uint64_t offset) {
        DEBUG_ASSERT(page == test_pages);
        DEBUG_ASSERT(offset == list2_offset);
      },
      [&](vm_page* page, uint64_t offset) {
        DEBUG_ASSERT(page == test_pages + 1);
        DEBUG_ASSERT(offset == list2_offset + 2 * PAGE_SIZE);
        return true;
      },
      &free_list);

  EXPECT_EQ(list_length(&free_list), 1ul);

  EXPECT_EQ(list2.RemovePage(0).ReleasePage(), test_pages + 2);
  EXPECT_EQ(list2.RemovePage(PAGE_SIZE).ReleasePage(), test_pages + 3);
  EXPECT_EQ(list2.RemovePage(2 * PAGE_SIZE).ReleasePage(), test_pages + 1);

  EXPECT_TRUE(list2.IsEmpty());

  END_TEST;
}

static bool vmpl_merge_overlap_test() {
  for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
    for (unsigned j = 0; j < VmPageListNode::kPageFanOut; j++) {
      if (!vmpl_merge_overlap_test_helper(i * PAGE_SIZE, j * PAGE_SIZE)) {
        return false;
      }
    }
  }
  return true;
}

static bool vmpl_for_every_page_test() {
  BEGIN_TEST;

  VmPageList list;
  list.InitializeSkew(0, PAGE_SIZE);
  vm_page_t test_pages[5] = {};

  uint64_t offsets[fbl::count_of(test_pages)] = {
      0,
      PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE - PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE + PAGE_SIZE,
  };

  for (unsigned i = 0; i < fbl::count_of(test_pages); i++) {
    if (i % 2) {
      EXPECT_TRUE(AddPage(&list, test_pages + i, offsets[i]));
    } else {
      EXPECT_TRUE(AddMarker(&list, offsets[i]));
    }
  }

  uint32_t idx = 0;
  auto iter_fn = [&](const auto& p, uint64_t off) -> zx_status_t {
    EXPECT_EQ(off, offsets[idx]);

    if (idx % 2) {
      EXPECT_TRUE(p.IsPage());
      EXPECT_EQ(p.Page(), test_pages + idx);
    } else {
      EXPECT_TRUE(p.IsMarker());
    }

    idx++;

    return ZX_ERR_NEXT;
  };

  list.ForEveryPage(iter_fn);
  ASSERT_EQ(idx, fbl::count_of(offsets));

  idx = 1;
  list.ForEveryPageInRange(iter_fn, offsets[1], offsets[fbl::count_of(test_pages) - 1]);
  ASSERT_EQ(idx, fbl::count_of(offsets) - 1);

  list_node_t free_list;
  list_initialize(&free_list);
  list.RemoveAllPages(&free_list);

  END_TEST;
}

static bool vmpl_merge_onto_test() {
  BEGIN_TEST;

  VmPageList list1, list2;
  list1.InitializeSkew(0, 0);
  list2.InitializeSkew(0, 0);
  vm_page_t test_pages[4] = {};

  EXPECT_TRUE(AddPage(&list1, test_pages + 0, 0));
  EXPECT_TRUE(
      AddPage(&list1, test_pages + 1, VmPageListNode::kPageFanOut * PAGE_SIZE + 2 * PAGE_SIZE));
  EXPECT_TRUE(AddPage(&list2, test_pages + 2, 0));
  EXPECT_TRUE(
      AddPage(&list2, test_pages + 3, 2 * VmPageListNode::kPageFanOut * PAGE_SIZE + PAGE_SIZE));

  list_node_t free_list;
  list_initialize(&free_list);

  list1.MergeOnto(list2, &free_list);

  // (test_pages + 0) should have covered this page
  EXPECT_EQ(1ul, list_length(&free_list));
  EXPECT_EQ(test_pages + 2, list_remove_head_type(&free_list, vm_page, queue_node));

  EXPECT_EQ(test_pages + 0, list2.Lookup(0)->Page());
  EXPECT_EQ(test_pages + 1,
            list2.Lookup(VmPageListNode::kPageFanOut * PAGE_SIZE + 2 * PAGE_SIZE)->Page());
  EXPECT_EQ(test_pages + 3,
            list2.Lookup(2 * VmPageListNode::kPageFanOut * PAGE_SIZE + PAGE_SIZE)->Page());

  list2.RemoveAllPages(&free_list);
  EXPECT_EQ(3ul, list_length(&free_list));

  END_TEST;
}

// Use the function name as the test name
#define VM_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(vm_tests)
VM_UNITTEST(vmm_alloc_smoke_test)
VM_UNITTEST(vmm_alloc_contiguous_smoke_test)
VM_UNITTEST(multiple_regions_test)
VM_UNITTEST(vmm_alloc_zero_size_fails)
VM_UNITTEST(vmm_alloc_bad_specific_pointer_fails)
VM_UNITTEST(vmm_alloc_contiguous_missing_flag_commit_fails)
VM_UNITTEST(vmm_alloc_contiguous_zero_size_fails)
VM_UNITTEST(vmaspace_create_smoke_test)
VM_UNITTEST(vmaspace_alloc_smoke_test)
VM_UNITTEST(vmo_create_test)
VM_UNITTEST(vmo_create_maximum_size)
VM_UNITTEST(vmo_pin_test)
VM_UNITTEST(vmo_multiple_pin_test)
VM_UNITTEST(vmo_commit_test)
VM_UNITTEST(vmo_odd_size_commit_test)
VM_UNITTEST(vmo_create_physical_test)
VM_UNITTEST(vmo_create_contiguous_test)
VM_UNITTEST(vmo_contiguous_decommit_test)
VM_UNITTEST(vmo_precommitted_map_test)
VM_UNITTEST(vmo_demand_paged_map_test)
VM_UNITTEST(vmo_dropped_ref_test)
VM_UNITTEST(vmo_remap_test)
VM_UNITTEST(vmo_double_remap_test)
VM_UNITTEST(vmo_read_write_smoke_test)
VM_UNITTEST(vmo_cache_test)
VM_UNITTEST(vmo_lookup_test)
VM_UNITTEST(vmo_lookup_clone_test)
VM_UNITTEST(vmo_clone_removes_write_test)
VM_UNITTEST(vmo_zero_scan_test)
VM_UNITTEST(arch_noncontiguous_map)
// Uncomment for debugging
// VM_UNITTEST(dump_all_aspaces)  // Run last
UNITTEST_END_TESTCASE(vm_tests, "vm", "Virtual memory tests")

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
UNITTEST_END_TESTCASE(pmm_tests, "pmm", "Physical memory manager tests")

UNITTEST_START_TESTCASE(vm_page_list_tests)
VM_UNITTEST(vmpl_add_remove_page_test)
VM_UNITTEST(vmpl_basic_marker_test)
VM_UNITTEST(vmpl_free_pages_test)
VM_UNITTEST(vmpl_free_pages_last_page_test)
VM_UNITTEST(vmpl_near_last_offset_free)
VM_UNITTEST(vmpl_take_single_page_even_test)
VM_UNITTEST(vmpl_take_single_page_odd_test)
VM_UNITTEST(vmpl_take_all_pages_test)
VM_UNITTEST(vmpl_take_middle_pages_test)
VM_UNITTEST(vmpl_take_gap_test)
VM_UNITTEST(vmpl_take_cleanup_test)
VM_UNITTEST(vmpl_page_gap_iter_test)
VM_UNITTEST(vmpl_merge_offset_test)
VM_UNITTEST(vmpl_merge_overlap_test)
VM_UNITTEST(vmpl_for_every_page_test)
VM_UNITTEST(vmpl_merge_onto_test)
UNITTEST_END_TESTCASE(vm_page_list_tests, "vmpl", "VmPageList tests")
