// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include <gtest/gtest.h>

#include "src/media/lib/mpsc_queue/mpsc_queue.h"

TEST(MpscQueueTest, Sanity) {
  MpscQueue<int> under_test;
  std::queue<int> expectation;
  const int kElements = 10;

  for (int i = 0; i < kElements; ++i) {
    under_test.Push(i);
    expectation.push(i);
  }

  for (int i = 0; i < kElements; ++i) {
    std::optional<int> maybe_elem = under_test.Pop();
    EXPECT_TRUE(maybe_elem.has_value());
    if (maybe_elem.has_value()) {
      EXPECT_EQ(expectation.front(), *maybe_elem);
      expectation.pop();
    }
  }
}

TEST(MpscQueueTest, TwoThreads) {
  MpscQueue<int> under_test;
  std::set<int> expectation;

  const int kElements = 100;
  for (int i = 0; i < kElements; ++i) {
    expectation.insert(i);
  }

  async::Loop producer_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(producer_loop.dispatcher(), [&under_test] {
    for (int i = 0; i < kElements; ++i) {
      under_test.Push(i);
    }
  });

  producer_loop.StartThread("Test Producer thread", nullptr);

  int element_count = 0;
  while (element_count < kElements) {
    std::optional<int> maybe_elem = under_test.Pop();
    if (maybe_elem.has_value()) {
      ++element_count;
      expectation.erase(*maybe_elem);
    }
  }

  EXPECT_EQ(expectation.size(), 0u);
}

TEST(BlockingMpscQueueTest, TwoThreads) {
  BlockingMpscQueue<int> under_test;
  std::set<int> expectation;

  const int kElements = 100;
  for (int i = 0; i < kElements; ++i) {
    expectation.insert(i);
  }

  async::Loop producer_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(producer_loop.dispatcher(), [&under_test] {
    for (int i = 0; i < kElements; ++i) {
      under_test.Push(i);
    }
  });

  producer_loop.StartThread("Test Producer thread", nullptr);

  int element_count = 0;
  while (element_count < kElements) {
    std::optional<int> maybe_element = under_test.WaitForElement();
    if (maybe_element) {
      ++element_count;
      expectation.erase(*maybe_element);
    }
  }

  EXPECT_EQ(expectation.size(), 0u);
}

TEST(BlockingMpscQueueTest, Clear) {
  BlockingMpscQueue<int> under_test;

  const int kElements = 100;
  for (int i = 0; i < kElements; ++i) {
    under_test.Push(i);
  }

  under_test.WaitForElement();
  under_test.Push(0);
  under_test.Reset();
  std::queue<int> extracted = BlockingMpscQueue<int>::Extract(std::move(under_test));
  EXPECT_EQ(extracted.size(), 0u);
}

TEST(BlockingMpscQueueTest, Extract) {
  BlockingMpscQueue<int> under_test;
  std::set<int> expectation;

  const int kElements = 100;
  for (int i = 0; i < kElements; ++i) {
    expectation.insert(i);
    under_test.Push(i);
  }

  std::queue<int> extracted = BlockingMpscQueue<int>::Extract(std::move(under_test));
  int element_count = 0;
  while (element_count < kElements && !extracted.empty()) {
    ++element_count;
    expectation.erase(extracted.front());
    extracted.pop();
  }

  EXPECT_EQ(expectation.size(), 0u);
}

TEST(BlockingMpscQueueTest, ManyThreads) {
  BlockingMpscQueue<int> under_test;

  const int kElements = 1000;
  const int kThreads = 10;

  // Order is not gauranteed when multiple producers contend, so we just test
  // here that the implementation is stable and all elements are yielded.
  std::unique_ptr<async::Loop> producer_loops[kThreads];
  for (auto& producer_loop : producer_loops) {
    producer_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    async::PostTask(producer_loop->dispatcher(), [&under_test] {
      for (int j = 0; j < kElements; ++j) {
        under_test.Push(j);
      }
    });
    producer_loop->StartThread(nullptr, nullptr);
  }

  int element_count = 0;
  while (element_count < kElements * kThreads) {
    std::optional<int> maybe_element = under_test.WaitForElement();
    if (maybe_element) {
      ++element_count;
    }
  }
}

TEST(BlockingMpscQueueTest, Signaled) {
  BlockingMpscQueue<int> under_test;

  EXPECT_FALSE(under_test.Signaled());

  under_test.Push(0);
  EXPECT_TRUE(under_test.Signaled());

  under_test.WaitForElement();
  EXPECT_FALSE(under_test.Signaled());
}
