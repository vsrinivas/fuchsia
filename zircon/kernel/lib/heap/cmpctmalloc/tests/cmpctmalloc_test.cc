// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "cmpctmalloc.h"

#include <lib/heap.h>
#include <lib/zircon-internal/align.h>

#include <algorithm>
#include <random>
#include <vector>

#include <zxtest/zxtest.h>

#include "page_manager.h"

constexpr uint32_t kRandomSeed = 101;

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

// A convenience class that allows us to allocate memory of random sizes, and
// then free that memory in various orders.
class RandomAllocator {
 public:
  enum FreeOrder {
    kChronological = 0,
    kReverseChronological = 1,
    kRandom = 2,
  };

  RandomAllocator() : generator_(kRandomSeed), distribution_(1, kHeapMaxAllocSize) {}
  ~RandomAllocator() { ZX_ASSERT(allocated_.empty()); }

  void Allocate(size_t num) {
    for (size_t i = 0; i < num; i++) {
      void* p = cmpct_alloc(distribution_(generator_));
      ASSERT_NOT_NULL(p);
      EXPECT_TRUE(ZX_IS_ALIGNED(p, HEAP_DEFAULT_ALIGNMENT));
      allocated_.push_back(p);
    }
  }

  void Free(FreeOrder order) {
    switch (order) {
      case FreeOrder::kChronological:
        break;
      case FreeOrder::kReverseChronological:
        std::reverse(allocated_.begin(), allocated_.end());
        break;
      case FreeOrder::kRandom:
        std::shuffle(allocated_.begin(), allocated_.end(), generator_);
        break;
    }
    for (void* p : allocated_) {
      cmpct_free(p);
    }
    allocated_.clear();
  }

 private:
  std::default_random_engine generator_;
  std::uniform_int_distribution<size_t> distribution_;
  std::vector<void*> allocated_;
};

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

TEST_F(CmpctmallocTest, CanAllocAndFree) {
  RandomAllocator::FreeOrder orders[] = {
      RandomAllocator::FreeOrder::kChronological,
      RandomAllocator::FreeOrder::kReverseChronological,
      RandomAllocator::FreeOrder::kRandom,
  };
  for (const auto& order : orders) {
    RandomAllocator ra;
    // TODO(fxbug.dev/49123): Rephrase as allocating until N requests are made
    // of the page manager.
    ra.Allocate(100);
    ra.Free(order);
  }
}

TEST_F(CmpctmallocTest, LargeAllocsAreNull) {
  void* p = cmpct_alloc(kHeapMaxAllocSize);
  EXPECT_NOT_NULL(p);
  p = cmpct_alloc(kHeapMaxAllocSize + 1);
  EXPECT_NULL(p);
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
