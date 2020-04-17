// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "cmpctmalloc.h"

#include <lib/heap.h>
#include <lib/zircon-internal/align.h>

#include <zxtest/zxtest.h>

#include "page_manager.h"

//
// Heap implementation.
//
static PageManager* page_manager;

void* heap_page_alloc(size_t pages) {
  ZX_ASSERT(page_manager != nullptr);
  return page_manager->AllocatePages(pages);
}
void heap_page_free(void* ptr, size_t pages) {
  ZX_ASSERT(page_manager != nullptr);
  page_manager->FreePages(ptr, pages);
}

class CmpctmallocTest : public zxtest::Test {
 public:
  void SetUp() final {
    page_manager = &page_manager_;
    cmpct_init();
  }

  void TearDown() final { page_manager = nullptr; }

 private:
  PageManager page_manager_;
};

TEST_F(CmpctmallocTest, ZeroAllocIsNull) { EXPECT_NULL(cmpct_alloc(0)); }

TEST_F(CmpctmallocTest, NullCanBeFreed) { cmpct_free(nullptr); }

// TODO(fxbug.dev/49123): Replace this test case with a more robust series of
// cases in which we pseudorandomly generate sizes of memory to allocate, which
// are then freed in three ways: allocation order, reverse-allocation order, and
// a pseudorandom order (e.g., by reusing the initial seed).
TEST_F(CmpctmallocTest, CanAllocAndFree) {
  for (int i = 1; (1 << i) < kHeapMaxAllocSize; ++i) {
    void* addr = cmpct_alloc(1 << i);
    ASSERT_NOT_NULL(addr);
    EXPECT_TRUE(ZX_IS_ALIGNED(addr, HEAP_DEFAULT_ALIGNMENT));
    cmpct_free(addr);
  }
}

TEST_F(CmpctmallocTest, LargeAllocsAreNull) {
  void* addr = cmpct_alloc(kHeapMaxAllocSize);
  EXPECT_NOT_NULL(addr);
  addr = cmpct_alloc(kHeapMaxAllocSize + 1);
  EXPECT_NULL(addr);
}

// TODO(fxbug.dev/49123): Add two test cases for coverage of the behavior of
// the cached allocation:
//
// In the first, we find the threshold at which repeated calls to, say,
// |cmpct_alloc(1000| trigger a request to the heap for more pages. We then
// alternatingly alloc and free across this threshold and expect no more
// pages sent back to the heap. Finally, freeing all alloc'ed addresses
// should also result in no further pages sent back to the heap.
//
// In the second, we verify with continual allocation up until pages have been
// requested of the heap, say, 10 times, that on freeing everying we only see
// 9 requests to the heap to free pages, which means we would have held onto
// a cached allocation of pages.

// TODO(fxbug.dev/49123): Add cases to cover
// * cmpct_get_info();
// * cmpct_realloc();
// * cmpct_memalign();
