// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>

#include <gtest/gtest.h>

#include "src/media/vnext/lib/stream_sink/stream_queue.h"

namespace fmlib::test {

// Tests the |pull| method.
TEST(StreamQueueTest, Pull) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;
  static const size_t kElements = 10;

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Push some elements.
  for (size_t i = 0; i < kElements; ++i) {
    under_test.push(i);
    EXPECT_FALSE(under_test.empty());
    EXPECT_EQ(i + 1, under_test.size());
  }

  // Pull those elements.
  size_t exec_count = 0;
  for (size_t i = 0; i < kElements; ++i) {
    executor.schedule_task(
        under_test.pull().and_then([i, &exec_count](StreamQueue<size_t, float>::Element& element) {
          EXPECT_TRUE(element.is_packet());
          EXPECT_EQ(i, element.packet());
          ++exec_count;
        }));
  }

  loop.RunUntilIdle();

  // Expect the tasks actually ran.
  EXPECT_EQ(kElements, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  executor.schedule_task(
      under_test.pull().and_then([&exec_count](StreamQueue<size_t, float>::Element& element) {
        EXPECT_TRUE(element.is_packet());
        EXPECT_EQ(kElements, element.packet());
        ++exec_count;
      }));

  // Expect the task hasn't run yet.
  EXPECT_EQ(kElements, exec_count);

  // Push one more element.
  under_test.push(kElements);
  loop.RunUntilIdle();

  // Expect the task ran once more.
  EXPECT_EQ(kElements + 1, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());
}

// Tests the |pull| method when the stream is ended.
TEST(StreamQueueTest, PullEnded) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;
  static const size_t kElements = 10;

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Push some elements.
  for (size_t i = 0; i < kElements; ++i) {
    under_test.push(i);
    EXPECT_FALSE(under_test.empty());
    EXPECT_EQ(i + 1, under_test.size());
  }

  // End the stream.
  under_test.end();

  // Pull those elements.
  size_t exec_count = 0;
  for (size_t i = 0; i < kElements; ++i) {
    executor.schedule_task(
        under_test.pull().and_then([i, &exec_count](StreamQueue<size_t, float>::Element& element) {
          EXPECT_TRUE(element.is_packet());
          EXPECT_EQ(i, element.packet());
          ++exec_count;
        }));
  }

  // Attempt to pull one more...expect |kEnded|.
  executor.schedule_task(
      under_test.pull().and_then([&exec_count](StreamQueue<size_t, float>::Element& element) {
        EXPECT_TRUE(element.is_ended());
        ++exec_count;
      }));

  loop.RunUntilIdle();

  // Expect the tasks actually ran.
  EXPECT_EQ(kElements + 1, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());
}

// Tests the |pull| method when the stream is ended asynchronously.
TEST(StreamQueueTest, PullEndedAsync) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;
  static const size_t kElements = 10;

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Push some elements.
  for (size_t i = 0; i < kElements; ++i) {
    under_test.push(i);
    EXPECT_FALSE(under_test.empty());
    EXPECT_EQ(i + 1, under_test.size());
  }

  // Pull those elements.
  size_t exec_count = 0;
  for (size_t i = 0; i < kElements; ++i) {
    executor.schedule_task(
        under_test.pull().and_then([i, &exec_count](StreamQueue<size_t, float>::Element& element) {
          EXPECT_TRUE(element.is_packet());
          EXPECT_EQ(i, element.packet());
          ++exec_count;
        }));
  }

  // Attempt to pull one more...expect |kEnded|.
  executor.schedule_task(
      under_test.pull().and_then([&exec_count](StreamQueue<size_t, float>::Element& element) {
        EXPECT_TRUE(element.is_ended());
        ++exec_count;
      }));

  loop.RunUntilIdle();

  // Expect the initial tasks actually ran.
  EXPECT_EQ(kElements, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // End the stream.
  under_test.end();

  loop.RunUntilIdle();

  // Expect the final task actually ran.
  EXPECT_EQ(kElements + 1, exec_count);
}

// Tests the |pull| method when the stream is drained.
TEST(StreamQueueTest, PullDrained) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;
  static const size_t kElements = 10;

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Push some elements.
  for (size_t i = 0; i < kElements; ++i) {
    under_test.push(i);
    EXPECT_FALSE(under_test.empty());
    EXPECT_EQ(i + 1, under_test.size());
  }

  // Drain the stream.
  under_test.drain();

  // Pull those elements.
  size_t exec_count = 0;
  for (size_t i = 0; i < kElements; ++i) {
    executor.schedule_task(
        under_test.pull().and_then([i, &exec_count](StreamQueue<size_t, float>::Element& element) {
          EXPECT_TRUE(element.is_packet());
          EXPECT_EQ(i, element.packet());
          ++exec_count;
        }));
  }

  // Attempt to pull one more...expect drained.
  executor.schedule_task(
      under_test.pull().then([&exec_count](StreamQueue<size_t, float>::PullResult& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(StreamQueueError::kDrained, result.error());
        ++exec_count;
      }));

  loop.RunUntilIdle();

  // Expect the tasks actually ran.
  EXPECT_EQ(kElements + 1, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());
}

// Tests the |pull| method when the stream is drained asynchronously.
TEST(StreamQueueTest, PullDrainedAsync) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;
  static const size_t kElements = 10;

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Push some elements.
  for (size_t i = 0; i < kElements; ++i) {
    under_test.push(i);
    EXPECT_FALSE(under_test.empty());
    EXPECT_EQ(i + 1, under_test.size());
  }

  // Pull those elements.
  size_t exec_count = 0;
  for (size_t i = 0; i < kElements; ++i) {
    executor.schedule_task(
        under_test.pull().and_then([i, &exec_count](StreamQueue<size_t, float>::Element& element) {
          EXPECT_TRUE(element.is_packet());
          EXPECT_EQ(i, element.packet());
          ++exec_count;
        }));
  }

  // Attempt to pull one more...expect drained.
  executor.schedule_task(
      under_test.pull().then([&exec_count](StreamQueue<size_t, float>::PullResult& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(StreamQueueError::kDrained, result.error());
        ++exec_count;
      }));

  loop.RunUntilIdle();

  // Expect the initial tasks actually ran.
  EXPECT_EQ(kElements, exec_count);

  // Expect the queue is empty.
  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());

  // Drain the stream.
  under_test.drain();

  loop.RunUntilIdle();

  // Expect the final task actually ran.
  EXPECT_EQ(kElements + 1, exec_count);
}

// Tests the |pull| method when the queue is cleared.
TEST(StreamQueueTest, PullClear) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;

  // Try to pull an element.
  size_t exec_count = 0;
  executor.schedule_task(
      under_test.pull().and_then([&exec_count](StreamQueue<size_t, float>::Element& element) {
        EXPECT_TRUE(element.is_clear_request());
        EXPECT_EQ(0.0f, element.clear_request());
        ++exec_count;
      }));

  loop.RunUntilIdle();

  // Expect the task hasn't run yet.
  EXPECT_EQ(0u, exec_count);

  // Clear the queue.
  under_test.clear(0.0f);
  loop.RunUntilIdle();

  // Expect the task ran once.
  EXPECT_EQ(1u, exec_count);
}

// Tests the |cancel_pull| method.
TEST(StreamQueueTest, CancelPull) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  StreamQueue<size_t, float> under_test;

  // Expect |cancel_pull| to return false, because there's no |pull| pending.
  EXPECT_FALSE(under_test.cancel_pull());

  // Attempt to pull.
  bool task_ran = false;
  executor.schedule_task(
      under_test.pull().then([&task_ran](StreamQueue<size_t, float>::PullResult& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(StreamQueueError::kCanceled, result.error());
        task_ran = true;
      }));
  loop.RunUntilIdle();

  // Expect that the task didn't run.
  EXPECT_FALSE(task_ran);

  // Abandon the pull. Expect |cancel_pull| to return true, because there's a |pull| pending.
  EXPECT_TRUE(under_test.cancel_pull());
  loop.RunUntilIdle();

  // Expect that the task ran (returning StreamQueueError::kCanceled).
  EXPECT_TRUE(task_ran);
}

}  // namespace fmlib::test
