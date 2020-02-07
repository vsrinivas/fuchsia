// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_structs.h"

#include <sstream>
#include <vector>

#include <zxtest/zxtest.h>

template <>
fbl::String zxtest::PrintValue(const network::internal::IndexedSlab<uint64_t>::Iterator& value) {
  return fbl::StringPrintf("Iterator<%d>", *value);
}

namespace network {
namespace testing {

using IndexedSlab = network::internal::IndexedSlab<uint64_t>;
using RingQueue = network::internal::RingQueue<uint64_t>;

TEST(DataStructsTest, RingQueue) {
  constexpr uint32_t kCapacity = 16;
  std::unique_ptr<RingQueue> queue;
  ASSERT_OK(RingQueue::Create(kCapacity, &queue));
  ASSERT_EQ(queue->count(), 0);
  queue->Push(1);
  queue->Push(2);
  queue->Push(3);
  ASSERT_EQ(queue->Peek(), 1);
  ASSERT_EQ(queue->count(), 3);
  ASSERT_EQ(queue->Pop(), 1);
  ASSERT_EQ(queue->Pop(), 2);
  ASSERT_EQ(queue->Pop(), 3);
  ASSERT_DEATH([&queue]() { queue->Pop(); });
}

TEST(DataStructsTest, RingQueueOverCapacity) {
  constexpr uint32_t kCapacity = 2;
  std::unique_ptr<RingQueue> queue;
  ASSERT_OK(RingQueue::Create(kCapacity, &queue));
  queue->Push(1);
  queue->Push(2);
  ASSERT_DEATH([&queue]() { queue->Push(3); });
}

TEST(DataStructsTest, IndexedSlab) {
  constexpr uint32_t kCapacity = 16;
  std::unique_ptr<IndexedSlab> slab;
  ASSERT_OK(IndexedSlab::Create(kCapacity, &slab));
  ASSERT_EQ(slab->available(), kCapacity);
  auto a = slab->Push(1);
  auto b = slab->Push(2);
  auto c = slab->Push(3);
  ASSERT_EQ(slab->available(), kCapacity - 3);
  ASSERT_EQ(slab->Get(a), 1);
  ASSERT_EQ(slab->Get(b), 2);
  ASSERT_EQ(slab->Get(c), 3);
  slab->Free(a);
  slab->Free(b);
  slab->Free(c);
  ASSERT_EQ(slab->available(), kCapacity);
}

TEST(DataStructsTest, IndexedSlabOverCapacity) {
  constexpr uint32_t kCapacity = 2;
  std::unique_ptr<IndexedSlab> slab;
  ASSERT_OK(IndexedSlab::Create(kCapacity, &slab));
  slab->Push(1);
  slab->Push(2);
  ASSERT_DEATH([&slab]() { slab->Push(3); });
}

TEST(DataStructsTest, IndexedSlabDoubleFree) {
  constexpr uint32_t kCapacity = 2;
  std::unique_ptr<IndexedSlab> slab;
  ASSERT_OK(IndexedSlab::Create(kCapacity, &slab));
  slab->Push(1);
  uint32_t b = slab->Push(2);
  slab->Free(b);
  ASSERT_EQ(slab->available(), kCapacity - 1);
  ASSERT_DEATH(([b, &slab]() { slab->Free(b); }));
}

void VerifyIterator(IndexedSlab* slab, const std::vector<uint64_t>& expect) {
  std::stringstream context_stream;
  for (auto e = expect.begin(); e != expect.end(); e++) {
    if (e != expect.begin()) {
      context_stream << ", ";
    }
    context_stream << *e;
  }
  auto context = context_stream.str();
  auto i = slab->begin();
  for (auto& e : expect) {
    ASSERT_EQ(slab->Get(*i), e, ": %s", context.c_str());
    ++i;
  }
  ASSERT_EQ(i, slab->end(), ": %s", context.c_str());
}

TEST(DataStructsTest, IndexedSlabIterator) {
  constexpr uint32_t kCapacity = 4;
  std::unique_ptr<IndexedSlab> slab;
  ASSERT_OK(IndexedSlab::Create(kCapacity, &slab));
  // If we're empty, the iterator should be empty:
  ASSERT_EQ(slab->begin(), slab->end());
  auto i1 = slab->Push(1);

  VerifyIterator(slab.get(), {1});
  auto i2 = slab->Push(2);
  auto i3 = slab->Push(3);
  slab->Push(4);
  VerifyIterator(slab.get(), {1, 2, 3, 4});
  slab->Free(i2);
  slab->Free(i3);
  VerifyIterator(slab.get(), {1, 4});
  slab->Push(2);
  VerifyIterator(slab.get(), {1, 2, 4});
  slab->Free(i1);
  VerifyIterator(slab.get(), {2, 4});
}

}  // namespace testing
}  // namespace network
