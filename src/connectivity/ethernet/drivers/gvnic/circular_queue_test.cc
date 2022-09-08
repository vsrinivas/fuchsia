// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/circular_queue.h"

#include <stdint.h>
#include <string.h>

#include <zxtest/zxtest.h>

namespace gvnic {

TEST(CircularQueueTest, BasicUsage) {
  CircularQueue<char> q;

  EXPECT_EQ(0U, q.Count());  // It is born empty.

  q.Init(4);
  EXPECT_EQ(0U, q.Count());  // Still empty.

  // Queue 4 things.
  q.Enqueue('f');
  EXPECT_EQ(1U, q.Count());
  EXPECT_EQ('f', q.Front());

  q.Enqueue('n');
  EXPECT_EQ(2U, q.Count());
  EXPECT_EQ('f', q.Front());

  q.Enqueue('o');
  EXPECT_EQ(3U, q.Count());
  EXPECT_EQ('f', q.Front());

  q.Enqueue('r');
  EXPECT_EQ(4U, q.Count());
  EXPECT_EQ('f', q.Front());

  // Dequeue 1, and then Enqueue another.
  q.Dequeue();
  EXPECT_EQ(3U, q.Count());
  EXPECT_EQ('n', q.Front());

  q.Enqueue('d');
  EXPECT_EQ(4U, q.Count());
  EXPECT_EQ('n', q.Front());

  // Dequeue 4 things.
  q.Dequeue();
  EXPECT_EQ(3U, q.Count());
  EXPECT_EQ('o', q.Front());

  q.Dequeue();
  EXPECT_EQ(2U, q.Count());
  EXPECT_EQ('r', q.Front());

  q.Dequeue();
  EXPECT_EQ(1U, q.Count());
  EXPECT_EQ('d', q.Front());

  q.Dequeue();
  EXPECT_EQ(0U, q.Count());
}

TEST(CircularQueueTest, CanInitAgainWhenEmppty) {
  CircularQueue<char> q;

  q.Init(5);
  EXPECT_EQ(0U, q.Count());

  q.Enqueue('*');
  EXPECT_EQ(1U, q.Count());
  EXPECT_EQ('*', q.Front());

  q.Dequeue();
  EXPECT_EQ(0U, q.Count());

  q.Init(2);
  EXPECT_EQ(0U, q.Count());

  q.Enqueue('!');
  EXPECT_EQ(1U, q.Count());
  EXPECT_EQ('!', q.Front());

  q.Dequeue();
  EXPECT_EQ(0U, q.Count());
}

TEST(CircularQueueTest, EmptyExceptions) {
  CircularQueue<char> q;

  EXPECT_EQ(0U, q.Count());
  ASSERT_DEATH([&q] { q.Front(); }, "Front when empty");
  ASSERT_DEATH([&q] { q.Dequeue(); }, "Dequeue when empty");
}

TEST(CircularQueueTest, FullExceptions) {
  CircularQueue<char> q;

  EXPECT_EQ(0U, q.Count());
  q.Init(4);
  q.Enqueue('d');
  q.Enqueue('e');
  q.Enqueue('r');
  q.Enqueue('p');
  ASSERT_DEATH([&q] { q.Enqueue('y'); }, "Enqueue when full");
}

TEST(CircularQueueTest, InitExceptions) {
  CircularQueue<char> q;

  EXPECT_EQ(0U, q.Count());
  q.Init(4);
  q.Enqueue('h');
  q.Enqueue('i');
  ASSERT_DEATH([&q] { q.Init(2); }, "Init when not empty.");
}

}  // namespace gvnic
