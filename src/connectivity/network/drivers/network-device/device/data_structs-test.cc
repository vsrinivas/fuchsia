// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_structs.h"

#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace network {
namespace testing {

using IndexedSlab = network::internal::IndexedSlab<uint64_t>;
using RingQueue = network::internal::RingQueue<uint64_t>;

TEST(DataStructsTest, RingQueue) {
  constexpr uint32_t kCapacity = 16;
  zx::result queue_creation = RingQueue::Create(kCapacity);
  ASSERT_OK(queue_creation.status_value());
  std::unique_ptr queue = std::move(queue_creation.value());
  ASSERT_EQ(queue->count(), 0u);
  queue->Push(1);
  queue->Push(2);
  queue->Push(3);
  ASSERT_EQ(queue->Peek(), 1u);
  ASSERT_EQ(queue->count(), 3u);
  ASSERT_EQ(queue->Pop(), 1u);
  ASSERT_EQ(queue->Pop(), 2u);
  ASSERT_EQ(queue->Pop(), 3u);
  ASSERT_DEATH_IF_SUPPORTED(queue->Pop(), "ASSERT FAILED");
}

TEST(DataStructsTest, RingQueueOverCapacity) {
  constexpr uint32_t kCapacity = 2;
  zx::result queue_creation = RingQueue::Create(kCapacity);
  ASSERT_OK(queue_creation.status_value());
  std::unique_ptr queue = std::move(queue_creation.value());
  queue->Push(1);
  queue->Push(2);
  ASSERT_DEATH_IF_SUPPORTED(queue->Push(3), "ASSERT FAILED");
}

TEST(DataStructsTest, IndexedSlab) {
  constexpr uint32_t kCapacity = 16;
  zx::result slab_creation = IndexedSlab::Create(kCapacity);
  ASSERT_OK(slab_creation.status_value());
  std::unique_ptr slab = std::move(slab_creation.value());
  ASSERT_EQ(slab->available(), kCapacity);
  uint32_t a = slab->Push(1);
  uint32_t b = slab->Push(2);
  uint32_t c = slab->Push(3);
  ASSERT_EQ(slab->available(), kCapacity - 3);
  ASSERT_EQ(slab->Get(a), 1u);
  ASSERT_EQ(slab->Get(b), 2u);
  ASSERT_EQ(slab->Get(c), 3u);
  slab->Free(a);
  slab->Free(b);
  slab->Free(c);
  ASSERT_EQ(slab->available(), kCapacity);
}

TEST(DataStructsTest, IndexedSlabOverCapacity) {
  constexpr uint32_t kCapacity = 2;
  zx::result slab_creation = IndexedSlab::Create(kCapacity);
  ASSERT_OK(slab_creation.status_value());
  std::unique_ptr slab = std::move(slab_creation.value());
  slab->Push(1);
  slab->Push(2);
  ASSERT_DEATH_IF_SUPPORTED(slab->Push(3), "ASSERT FAILED");
}

TEST(DataStructsTest, IndexedSlabDoubleFree) {
  constexpr uint32_t kCapacity = 2;
  zx::result slab_creation = IndexedSlab::Create(kCapacity);
  ASSERT_OK(slab_creation.status_value());
  std::unique_ptr slab = std::move(slab_creation.value());
  slab->Push(1);
  uint32_t b = slab->Push(2);
  slab->Free(b);
  ASSERT_EQ(slab->available(), kCapacity - 1);
  ASSERT_DEATH_IF_SUPPORTED(slab->Free(b), "ASSERT FAILED");
}

void VerifyIterator(IndexedSlab& slab, const std::vector<uint64_t>& expect) {
  std::stringstream context_stream;
  for (auto e = expect.begin(); e != expect.end(); e++) {
    if (e != expect.begin()) {
      context_stream << ", ";
    }
    context_stream << *e;
  }
  auto context = context_stream.str();
  auto i = slab.begin();
  for (auto& e : expect) {
    ASSERT_EQ(slab.Get(*i), e) << ": " << context.c_str();
    ++i;
  }
  ASSERT_EQ(i, slab.end()) << ": " << context.c_str();
}

TEST(DataStructsTest, IndexedSlabIterator) {
  constexpr uint32_t kCapacity = 4;
  zx::result slab_creation = IndexedSlab::Create(kCapacity);
  ASSERT_OK(slab_creation.status_value());
  std::unique_ptr slab = std::move(slab_creation.value());
  // If we're empty, the iterator should be empty:
  ASSERT_EQ(slab->begin(), slab->end());
  uint32_t i1 = slab->Push(1);

  VerifyIterator(*slab, {1});
  uint32_t i2 = slab->Push(2);
  uint32_t i3 = slab->Push(3);
  slab->Push(4);
  VerifyIterator(*slab, {1, 2, 3, 4});
  slab->Free(i2);
  slab->Free(i3);
  VerifyIterator(*slab, {1, 4});
  slab->Push(2);
  VerifyIterator(*slab, {1, 2, 4});
  slab->Free(i1);
  VerifyIterator(*slab, {2, 4});
}

}  // namespace testing
}  // namespace network
