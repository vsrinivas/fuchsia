// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "cmpctmalloc.h"

#include <lib/heap.h>
#include <lib/zircon-internal/align.h>
#include <math.h>

#include <algorithm>
#include <random>
#include <vector>

#include <zxtest/zxtest.h>

#include "page_manager.h"

namespace {

constexpr uint32_t kRandomSeed = 101;

// In the tests below, we wish to allocate until a certain threshold is met.
// Expressing this threshold in terms of the number of allocations made is not
// particularily meaningful, especially as the allocation sizes are random and
// are sensitive to the above seed. Instead, we express this in terms of the
// number of times that we see the heap grow.
//
// The current value is picked due to its roundness and the fact that an order
// more in magnitude would make for a test too slow for automation.
constexpr size_t kHeapGrowthCount = 10;

// A convenience class that allows us to allocate memory of random sizes, and
// then free that memory in various orders.
class RandomAllocator {
 public:
  enum FreeOrder {
    kChronological = 0,
    kReverseChronological = 1,
    kRandom = 2,
  };

  RandomAllocator()
      : generator_(kRandomSeed),
        sizes_(1, kHeapMaxAllocSize),
        alignment_exponents_(kMinAlignmentExponent, kMaxAlignmentExponent) {}
  ~RandomAllocator() { ZX_ASSERT(allocated_.empty()); }

  void Allocate() {
    size_t size = sizes_(generator_);
    void* p = cmpct_alloc(size);
    ASSERT_NOT_NULL(p);
    EXPECT_TRUE(ZX_IS_ALIGNED(p, HEAP_DEFAULT_ALIGNMENT));
    memset(p, kAllocFill, size);
    allocated_.push_back(p);
  }

  void AllocateAligned() {
    size_t size = sizes_(generator_);
    size_t exponent = alignment_exponents_(generator_);
    size_t alignment = 1 << exponent;
    void* p = cmpct_memalign(alignment, size);
    ASSERT_NOT_NULL(p);
    EXPECT_TRUE(ZX_IS_ALIGNED(p, alignment));
    memset(p, kAllocFill, size);
    allocated_.push_back(p);
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
  // Filling allocated buffers with this value helps ensure that cmpctmaloc
  // is indeed giving us a buffer large enough. For example, were it to give us
  // anything with overlap with its internal data structures, this fill would
  // stomp on that and likely result in a large crash.
  static constexpr int kAllocFill = 0x51;
  // memalign is only required to accept alignment specifications that are
  // powers of two and multiples of sizeof(void*) (guaranteed itself to be a
  // power of 2).
  const size_t kMinAlignmentExponent = static_cast<size_t>(log2(sizeof(void*)));
  const size_t kMaxAlignmentExponent = static_cast<size_t>(log2(ZX_PAGE_SIZE));

  std::default_random_engine generator_;
  std::uniform_int_distribution<size_t> sizes_;
  std::uniform_int_distribution<size_t> alignment_exponents_;
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
    // Allocate until we grow the heap a sufficient number of times.
    size_t times_grown = 0;
    while (times_grown < kHeapGrowthCount) {
      size_t before = heap_used_bytes();
      ra.Allocate();
      size_t after = heap_used_bytes();
      times_grown += (after > before);
    }
    ra.Free(order);
  }
}

TEST_F(CmpctmallocTest, CanMemalignAndFree) {
  RandomAllocator::FreeOrder orders[] = {
      RandomAllocator::FreeOrder::kChronological,
      RandomAllocator::FreeOrder::kReverseChronological,
      RandomAllocator::FreeOrder::kRandom,
  };
  for (const auto& order : orders) {
    RandomAllocator ra;
    // Allocate until we grow the heap a sufficient number of times.
    size_t times_grown = 0;
    while (times_grown < kHeapGrowthCount) {
      size_t before = heap_used_bytes();
      ra.AllocateAligned();
      size_t after = heap_used_bytes();
      times_grown += (after > before);
    }
    ra.Free(order);
  }
}

TEST_F(CmpctmallocTest, LargeAllocsAreNull) {
  void* p = cmpct_alloc(kHeapMaxAllocSize);
  EXPECT_NOT_NULL(p);
  cmpct_free(p);
  p = cmpct_alloc(kHeapMaxAllocSize + 1);
  EXPECT_NULL(p);
}

TEST_F(CmpctmallocTest, CachedAllocationIsEfficientlyUsed) {
  constexpr size_t kAllocSize = 1000;
  std::vector<void*> allocations;
  bool grown = false;
  while (!grown) {
    size_t before = heap_used_bytes();
    allocations.push_back(cmpct_alloc(kAllocSize));
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

    allocations.push_back(cmpct_alloc(kAllocSize));
    EXPECT_EQ(0, heap_cached_bytes());
  }

  // Ditto if we now free everything.
  while (!allocations.empty()) {
    cmpct_free(allocations.back());
    allocations.pop_back();
  }
  EXPECT_GT(heap_cached_bytes(), 0);
}

}  // namespace
