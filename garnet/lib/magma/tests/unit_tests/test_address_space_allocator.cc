// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include "gtest/gtest.h"
#include "magma_util/dlog.h"
#include "magma_util/retry_allocator.h"
#include "magma_util/simple_allocator.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

namespace {

class Region {
 public:
  Region(magma::AddressSpaceAllocator* allocator, uint64_t addr)
      : allocator_(allocator), addr_(addr) {}
  ~Region() { EXPECT_EQ(true, allocator_->Free(addr_)); }

  magma::AddressSpaceAllocator* allocator_;
  uint64_t addr_;
};
}  // namespace

static void test_simple_allocator(magma::SimpleAllocator* allocator, uint8_t align_pow2) {
  DLOG("test_simple_allocator align_pow2 0x%x\n", align_pow2);

  uint64_t expected_addr = allocator->base();
  uint64_t addr;
  size_t size;
  bool ret;

  std::list<Region> allocs;

  // size zero invalid
  ret = allocator->Alloc(0, align_pow2, &addr);
  EXPECT_EQ(ret, false);

  ret = allocator->GetSize(0, &size);
  EXPECT_EQ(ret, false);

  // the maximum size
  ret = allocator->Alloc(allocator->size(), align_pow2, &addr);
  EXPECT_EQ(ret, true);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    allocs.emplace_back(allocator, addr);

    ret = allocator->GetSize(addr, &size);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(size, allocator->size());

    ret = allocator->Alloc(1, align_pow2, &addr);
    EXPECT_EQ(ret, false);

    // free the big kahuna
    allocs.clear();
  }

  ret = allocator->Alloc(PAGE_SIZE, align_pow2, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + PAGE_SIZE, 1 << align_pow2);
    allocs.emplace_back(allocator, addr);
  }

  ret = allocator->Alloc(PAGE_SIZE - 1, align_pow2, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + PAGE_SIZE, 1 << align_pow2);
    allocs.emplace_back(allocator, addr);
  }

  ret = allocator->Alloc(PAGE_SIZE + 1, align_pow2, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + 2 * PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + 2 * PAGE_SIZE, 1 << align_pow2);
    allocs.emplace_back(allocator, addr);
  }

  ret = allocator->Alloc(PAGE_SIZE * 20, align_pow2, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + 20 * PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    allocs.emplace_back(allocator, addr);
  }
}

static void stress_test_allocator(magma::AddressSpaceAllocator* allocator, uint8_t align_pow2,
                                  unsigned int num_iterations, size_t max_alloc_size) {
  unsigned int num_init = allocator->size() / max_alloc_size * 3 / 2;

  DLOG("test_allocator align_pow2 0x%x num_iterations %d num_init %u\n", align_pow2, num_iterations,
       num_init);

  std::vector<std::unique_ptr<Region>> allocs;
  uint64_t addr;

  srand(1);

  for (unsigned int n = 0; n < num_init; n++) {
    size_t size = rand() % max_alloc_size + 1;
    bool ret = allocator->Alloc(size, align_pow2, &addr);
    EXPECT_EQ(ret, true);
    if (ret)
      allocs.emplace_back(new Region(allocator, addr));
  }

  for (unsigned int n = 0; n < num_iterations; n++) {
    size_t size = rand() % max_alloc_size + 1;
    bool ret = allocator->Alloc(size, align_pow2, &addr);
    EXPECT_EQ(ret, true);
    if (ret) {
      unsigned int index = rand() % allocs.size();
      DASSERT(index < allocs.size());
      allocs[index] = std::unique_ptr<Region>(new Region(allocator, addr));
    }
  }
}

static void test_retry_allocator(magma::RetryAllocator* allocator, uint8_t align_pow2) {
  DLOG("test_retry_allocator align_pow2 0x%x\n", align_pow2);

  uint64_t expected_addr = allocator->base();
  uint64_t addr;
  bool ret;

  std::list<Region> allocs;

  // size zero invalid
  ret = allocator->Alloc(
      0, align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_FALSE(ret);

  // the maximum size
  ret = allocator->Alloc(
      allocator->size(), align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_TRUE(ret);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);

    ret = allocator->Alloc(
        1, align_pow2, [](uint64_t addr) { return true; }, &addr);
    EXPECT_EQ(ret, false);

    allocator->Free(addr, allocator->size());
  }

  ret = allocator->Alloc(
      PAGE_SIZE, align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + PAGE_SIZE, 1 << align_pow2);
  }

  ret = allocator->Alloc(
      PAGE_SIZE - 1, align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + PAGE_SIZE, 1 << align_pow2);
  }

  ret = allocator->Alloc(
      PAGE_SIZE + 1, align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + 2 * PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    expected_addr = ALIGN(expected_addr + 2 * PAGE_SIZE, 1 << align_pow2);
  }

  ret = allocator->Alloc(
      PAGE_SIZE * 20, align_pow2, [](uint64_t addr) { return true; }, &addr);
  EXPECT_EQ(ret, allocator->size() >= expected_addr + 20 * PAGE_SIZE);
  if (ret) {
    EXPECT_EQ(addr, expected_addr);
    EXPECT_TRUE(addr % (1 << align_pow2) == 0);
  }

  for (uint32_t i = 0; i < 10; i++) {
    ret = allocator->Alloc(
        PAGE_SIZE, align_pow2, [](uint64_t addr) { return addr % (PAGE_SIZE * 2) == 0; }, &addr);
    EXPECT_EQ(ret, allocator->size() >= expected_addr + PAGE_SIZE);
    if (ret) {
      EXPECT_EQ(0u, addr % (2 * PAGE_SIZE));
      EXPECT_TRUE(addr % (1 << align_pow2) == 0);
    }
  }
}

TEST(AddressSpaceAllocator, SimpleAllocator) {
  test_simple_allocator(magma::SimpleAllocator::Create(0, 4 * PAGE_SIZE).get(), 0);

  const size_t _4g = 4ULL * 1024 * 1024 * 1024;
  test_simple_allocator(magma::SimpleAllocator::Create(0, _4g).get(), 0);
  test_simple_allocator(magma::SimpleAllocator::Create(0, _4g).get(), 1);
  test_simple_allocator(magma::SimpleAllocator::Create(0, _4g).get(), 12);
  test_simple_allocator(magma::SimpleAllocator::Create(0, _4g).get(), 13);

  stress_test_allocator(magma::SimpleAllocator::Create(0, _4g).get(), 0, 100000,
                        16ULL * 1024 * 1024);
}

TEST(AddressSpaceAllocator, RetryAllocator) {
  test_retry_allocator(magma::RetryAllocator::Create(0, 4 * PAGE_SIZE).get(), 0);

  const size_t _4g = 4ULL * 1024 * 1024 * 1024;
  test_retry_allocator(magma::RetryAllocator::Create(0, _4g).get(), 0);
  test_retry_allocator(magma::RetryAllocator::Create(0, _4g).get(), 1);
  test_retry_allocator(magma::RetryAllocator::Create(0, _4g).get(), 12);
  test_retry_allocator(magma::RetryAllocator::Create(0, _4g).get(), 13);
}
