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

namespace {

constexpr uint32_t kRandomSeed = 101;

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

size_t heap_used_bytes() {
  size_t used_bytes;
  cmpct_get_info(&used_bytes, nullptr, nullptr);
  return used_bytes;
}

size_t heap_free_bytes() {
  size_t free_bytes;
  cmpct_get_info(nullptr, &free_bytes, nullptr);
  return free_bytes;
}

size_t heap_cached_bytes() {
  size_t cached_bytes;
  cmpct_get_info(nullptr, nullptr, &cached_bytes);
  return cached_bytes;
}

PageManager* page_manager;

}  // namespace

//
// Heap implementation.
//
void* heap_page_alloc(size_t pages) {
  ZX_ASSERT(page_manager != nullptr);
  return page_manager->AllocatePages(pages);
}
void heap_page_free(void* ptr, size_t pages) {
  ZX_ASSERT(page_manager != nullptr);
  page_manager->FreePages(ptr, pages);
}

namespace {

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

TEST_F(CmpctmallocTest, HeapIsProperlyInitialized) {
  // Assumes that we have called |cmpct_init|, which was done in the test case
  // set-up.

  // The heap should have space.
  EXPECT_GT(heap_used_bytes(), 0);
  EXPECT_GT(heap_free_bytes(), 0);
  // Nothing should have been cached at this point.
  EXPECT_EQ(heap_cached_bytes(), 0);
}

TEST_F(CmpctmallocTest, CanAllocAndFree) {
  RandomAllocator::FreeOrder orders[] = {
      RandomAllocator::FreeOrder::kChronological,
      RandomAllocator::FreeOrder::kReverseChronological,
      RandomAllocator::FreeOrder::kRandom,
  };
  for (const auto& order : orders) {
    RandomAllocator ra;
    // Allocate until we grow the heap ten times.
    size_t times_grown = 0;
    while (times_grown < 10) {
      size_t before = heap_used_bytes();
      ra.Allocate(1);
      size_t after = heap_used_bytes();
      times_grown += (after > before);
    }
    ra.Free(order);
  }
}

TEST_F(CmpctmallocTest, LargeAllocsAreNull) {
  void* p = cmpct_alloc(kHeapMaxAllocSize);
  EXPECT_NOT_NULL(p);
  p = cmpct_alloc(kHeapMaxAllocSize + 1);
  EXPECT_NULL(p);
}

TEST_F(CmpctmallocTest, CachedAllocationIsEfficientlyUsed) {
  std::vector<void*> allocations;
  bool grown = false;
  while (!grown) {
    size_t before = heap_used_bytes();
    allocations.push_back(cmpct_alloc(1000));
    size_t after = heap_used_bytes();
    grown = (after > before);
  }

  // As we alternatingly allocate and free across the threshold at which we
  // saw a request to the heap for more pages, we expect to only be using
  // our cached allocation.
  for (int i = 0; i < 1000; i++) {
    cmpct_free(allocations.back());
    allocations.pop_back();
    EXPECT_GT(heap_cached_bytes(), 0);

    allocations.push_back(cmpct_alloc(1000));
    EXPECT_EQ(0, heap_cached_bytes());
  }

  // Ditto if we now free everything.
  while (!allocations.empty()) {
    cmpct_free(allocations.back());
    allocations.pop_back();
  }
  EXPECT_GT(heap_cached_bytes(), 0);
}

// TODO(fxbug.dev/49123): Add cases to cover
// * cmpct_realloc();
// * cmpct_memalign();

}  // namespace
