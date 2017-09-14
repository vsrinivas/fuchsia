// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/transport/shared_buffer_set_allocator.h"

#include <vector>

#include "gtest/gtest.h"

namespace media {
namespace {

static const uint64_t kSmallAlloc = 100;
static const uint64_t kLargeAlloc = 256 * 1024;

uint32_t VerifyBufferAdd(SharedBufferSetAllocator* under_test) {
  uint32_t buffer_id;
  zx::vmo vmo;
  EXPECT_TRUE(under_test->PollForBufferUpdate(&buffer_id, &vmo));
  EXPECT_TRUE(vmo);
  return buffer_id;
}

void VerifyNoBufferUpdate(SharedBufferSetAllocator* under_test) {
  uint32_t buffer_id;
  zx::vmo vmo;
  EXPECT_FALSE(under_test->PollForBufferUpdate(&buffer_id, &vmo));
}

void* AllocateRegion(SharedBufferSetAllocator* under_test,
                     uint64_t size,
                     uint32_t* new_buffer_id_out) {
  // Do an allocation.
  void* region = under_test->AllocateRegion(size);
  EXPECT_TRUE(region);

  *new_buffer_id_out = VerifyBufferAdd(under_test);

  // Make sure no additional buffer was created unexpectedly.
  VerifyNoBufferUpdate(under_test);

  SharedBufferSet::Locator locator = under_test->LocatorFromPtr(region);
  EXPECT_EQ(*new_buffer_id_out, locator.buffer_id());

  return region;
}

void* AllocateRegion(SharedBufferSetAllocator* under_test,
                     uint64_t size,
                     uint32_t expected_existing_buffer) {
  // Do an allocation.
  void* region = under_test->AllocateRegion(size);
  EXPECT_TRUE(region);

  // Make sure no buffer was created unexpectedly.
  VerifyNoBufferUpdate(under_test);

  SharedBufferSet::Locator locator = under_test->LocatorFromPtr(region);
  EXPECT_EQ(expected_existing_buffer, locator.buffer_id());

  return region;
}

uint32_t VerifyBufferRemove(SharedBufferSetAllocator* under_test,
                            uint32_t expected_buffer_id) {
  uint32_t buffer_id;
  zx::vmo vmo;
  EXPECT_TRUE(under_test->PollForBufferUpdate(&buffer_id, &vmo));
  EXPECT_EQ(expected_buffer_id, buffer_id);
  EXPECT_FALSE(vmo);
  return buffer_id;
}

// Tests SharedBufferSetAllocator::AllocateRegion and ReleaseRegion for small
// allocations.
TEST(SharedBufferSetAllocatorTest, TwoSmallAllocations) {
  SharedBufferSetAllocator under_test(
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_MAP);

  uint32_t buffer_id;
  void* region_0 = AllocateRegion(&under_test, kSmallAlloc, &buffer_id);
  void* region_1 = AllocateRegion(&under_test, kSmallAlloc, buffer_id);

  under_test.ReleaseRegion(region_0);
  under_test.ReleaseRegion(region_1);

  // Shouldn't see any buffer updates.
  VerifyNoBufferUpdate(&under_test);
}

// Tests SharedBufferSetAllocator::AllocateRegion and ReleaseRegion for large
// allocations.
TEST(SharedBufferSetAllocatorTest, TwoLargeAllocations) {
  SharedBufferSetAllocator under_test(
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_MAP);

  uint32_t buffer_id_0;
  void* region_0 = AllocateRegion(&under_test, kLargeAlloc, &buffer_id_0);

  uint32_t buffer_id_1;
  void* region_1 = AllocateRegion(&under_test, kLargeAlloc, &buffer_id_1);

  under_test.ReleaseRegion(region_0);
  under_test.ReleaseRegion(region_1);

  // Shouldn't see any buffer updates.
  VerifyNoBufferUpdate(&under_test);
}

// Tests SharedBufferSetAllocator::AllocateRegion and ReleaseRegion for small
// allocations that require a new buffer.
TEST(SharedBufferSetAllocatorTest, ManySmallAllocations) {
  SharedBufferSetAllocator under_test(
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_MAP);

  std::vector<void*> first_buffer_allocations;
  uint32_t first_buffer_id;
  uint32_t second_buffer_id;

  first_buffer_allocations.push_back(
      AllocateRegion(&under_test, kSmallAlloc, &first_buffer_id));

  // Allocate small regions until a new buffer is required.
  while (true) {
    void* region = under_test.AllocateRegion(kSmallAlloc);

    uint32_t buffer_id;
    zx::vmo vmo;
    if (under_test.PollForBufferUpdate(&buffer_id, &vmo)) {
      EXPECT_TRUE(vmo);
      EXPECT_NE(first_buffer_id, buffer_id);
      second_buffer_id = buffer_id;
      SharedBufferSet::Locator locator = under_test.LocatorFromPtr(region);
      EXPECT_EQ(second_buffer_id, locator.buffer_id());
      break;
    } else {
      SharedBufferSet::Locator locator = under_test.LocatorFromPtr(region);
      EXPECT_EQ(first_buffer_id, locator.buffer_id());
      first_buffer_allocations.push_back(region);
      // We shouldn't have to allocate a lot before a buffer transition.
      EXPECT_GT(1000u, first_buffer_allocations.size());
    }
  }

  // Release regions from the first buffer and verify that the buffer is
  // removed.
  while (true) {
    under_test.ReleaseRegion(first_buffer_allocations.back());
    first_buffer_allocations.pop_back();
    if (first_buffer_allocations.empty()) {
      VerifyBufferRemove(&under_test, first_buffer_id);
      break;
    } else {
      VerifyNoBufferUpdate(&under_test);
    }
  }

  // Do another allocation from the new buffer.
  AllocateRegion(&under_test, kSmallAlloc, second_buffer_id);
}

}  // namespace
}  // namespace media
