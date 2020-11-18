// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test_helper.h"

namespace vm_unittest {

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
  pl.RemovePages(
      [&list](VmPageOrMarker* page_or_marker, uint64_t off) {
        if (page_or_marker->IsPage()) {
          vm_page_t* p = page_or_marker->ReleasePage();
          list_add_tail(&list, &p->queue_node);
        }
        *page_or_marker = VmPageOrMarker::Empty();
        return ZX_ERR_NEXT;
      },
      PAGE_SIZE * 2, (kCount - 1) * 2 * PAGE_SIZE);
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
  pl.RemoveAllPages([&list](vm_page_t* p) { list_add_tail(&list, &p->queue_node); });
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
      pl.RemoveAllPages([&list](vm_page_t* p) { list_add_tail(&list, &p->queue_node); });

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

// Tests that an empty page splice list can be created.
static bool vmpl_take_empty_test() {
  BEGIN_TEST;

  VmPageList pl;

  VmPageSpliceList splice = pl.TakePages(PAGE_SIZE, PAGE_SIZE);

  EXPECT_FALSE(splice.IsDone());
  EXPECT_TRUE(splice.Pop().IsEmpty());
  EXPECT_TRUE(splice.IsDone());

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
      [pages, stop_idx, &idx](const VmPageOrMarker* p, auto off) {
        if (off != idx * PAGE_SIZE || !p->IsPage() || pages[idx] != p->Page()) {
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
  list.RemoveAllPages([&free_list](vm_page_t* p) { list_add_tail(&free_list, &p->queue_node); });
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
          // Ensure pages are in an initialized state every iteration.
          pages[k] = (vm_page_t){};
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
        list_add_tail(&free_list, &page->queue_node);
      },
      [&](VmPageOrMarker* page_or_marker, uint64_t offset) {
        DEBUG_ASSERT(page_or_marker->IsPage());
        vm_page_t* page = page_or_marker->Page();
        DEBUG_ASSERT(page == test_pages + 1 || page == test_pages + 2 || page == test_pages + 3 ||
                     page == test_pages + 4);
        DEBUG_ASSERT(offset == offsets[1] || offset == offsets[2] || offset == offsets[3] ||
                     offsets[4]);
      });

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
        list_add_tail(&free_list, &page->queue_node);
      },
      [&](VmPageOrMarker* page_or_marker, uint64_t offset) {
        DEBUG_ASSERT(page_or_marker->IsPage());
        vm_page_t* page = page_or_marker->Page();
        DEBUG_ASSERT(page == test_pages + 1);
        DEBUG_ASSERT(offset == list2_offset + 2 * PAGE_SIZE);
      });

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

static bool vmpl_merge_marker_test() {
  BEGIN_TEST;

  VmPageList list1;
  VmPageList list2;

  // Put markers in our from list and one of marker, page and nothing in our destination list.
  // In all circumstances when doing a MergeFrom we should not have either our release or migrate
  // callbacks invoked, as they only get invoked for actual pages.
  EXPECT_TRUE(AddMarker(&list1, 0));
  EXPECT_TRUE(AddMarker(&list1, PAGE_SIZE));
  EXPECT_TRUE(AddMarker(&list1, PAGE_SIZE * 2));
  EXPECT_TRUE(AddMarker(&list2, PAGE_SIZE));
  vm_page_t test_page = {};
  EXPECT_TRUE(AddPage(&list2, &test_page, PAGE_SIZE * 2));

  int release_calls = 0;
  int migrate_calls = 0;
  list2.MergeFrom(
      list1, 0, PAGE_SIZE * 3,
      [&release_calls](vm_page_t* page, uint64_t offset) { release_calls++; },
      [&migrate_calls](VmPageOrMarker* page, uint64_t offset) { migrate_calls++; });

  EXPECT_EQ(0, release_calls);
  EXPECT_EQ(0, migrate_calls);

  // Remove the page from our list as its not a real page.
  EXPECT_EQ(list2.RemovePage(PAGE_SIZE * 2).ReleasePage(), &test_page);

  END_TEST;
}

static bool vmpl_for_every_page_test() {
  BEGIN_TEST;

  VmPageList list;
  list.InitializeSkew(0, PAGE_SIZE);
  vm_page_t test_pages[5] = {};

  uint64_t offsets[ktl::size(test_pages)] = {
      0,
      PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE - PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE,
      VmPageListNode::kPageFanOut * PAGE_SIZE + PAGE_SIZE,
  };

  for (unsigned i = 0; i < ktl::size(test_pages); i++) {
    if (i % 2) {
      EXPECT_TRUE(AddPage(&list, test_pages + i, offsets[i]));
    } else {
      EXPECT_TRUE(AddMarker(&list, offsets[i]));
    }
  }

  uint32_t idx = 0;
  auto iter_fn = [&](const auto* p, uint64_t off) -> zx_status_t {
    EXPECT_EQ(off, offsets[idx]);

    if (idx % 2) {
      EXPECT_TRUE(p->IsPage());
      EXPECT_EQ(p->Page(), test_pages + idx);
    } else {
      EXPECT_TRUE(p->IsMarker());
    }

    idx++;

    return ZX_ERR_NEXT;
  };

  list.ForEveryPage(iter_fn);
  ASSERT_EQ(idx, ktl::size(offsets));

  idx = 1;
  list.ForEveryPageInRange(iter_fn, offsets[1], offsets[ktl::size(test_pages) - 1]);
  ASSERT_EQ(idx, ktl::size(offsets) - 1);

  list_node_t free_list;
  list_initialize(&free_list);
  list.RemoveAllPages([&free_list](vm_page_t* p) { list_add_tail(&free_list, &p->queue_node); });

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

  list1.MergeOnto(list2, [&free_list](auto* p) { list_add_tail(&free_list, &p->queue_node); });

  // (test_pages + 0) should have covered this page
  EXPECT_EQ(1ul, list_length(&free_list));
  EXPECT_EQ(test_pages + 2, list_remove_head_type(&free_list, vm_page, queue_node));

  EXPECT_EQ(test_pages + 0, list2.Lookup(0)->Page());
  EXPECT_EQ(test_pages + 1,
            list2.Lookup(VmPageListNode::kPageFanOut * PAGE_SIZE + 2 * PAGE_SIZE)->Page());
  EXPECT_EQ(test_pages + 3,
            list2.Lookup(2 * VmPageListNode::kPageFanOut * PAGE_SIZE + PAGE_SIZE)->Page());

  list2.RemoveAllPages([&free_list](vm_page_t* p) { list_add_tail(&free_list, &p->queue_node); });
  EXPECT_EQ(3ul, list_length(&free_list));

  END_TEST;
}

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
VM_UNITTEST(vmpl_take_empty_test)
VM_UNITTEST(vmpl_take_cleanup_test)
VM_UNITTEST(vmpl_page_gap_iter_test)
VM_UNITTEST(vmpl_merge_offset_test)
VM_UNITTEST(vmpl_merge_overlap_test)
VM_UNITTEST(vmpl_for_every_page_test)
VM_UNITTEST(vmpl_merge_onto_test)
VM_UNITTEST(vmpl_merge_marker_test)
UNITTEST_END_TESTCASE(vm_page_list_tests, "vmpl", "VmPageList tests")

}  // namespace vm_unittest
