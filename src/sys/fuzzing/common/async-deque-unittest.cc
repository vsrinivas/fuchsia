// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-deque.h"

#include <stddef.h>

#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixture.

class AsyncDequeTest : public AsyncTest {};

// Unit tests.

TEST_F(AsyncDequeTest, SendBeforeReceive) {
  AsyncSender<Input> sender;
  AsyncReceiver<Input> receiver(&sender);
  Input hello("hello");
  Input world("world");
  EXPECT_EQ(sender.Send(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(sender.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(receiver.Receive(), std::move(hello));
  FUZZING_EXPECT_OK(receiver.Receive(), std::move(world));
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, ReceiveBeforeSend) {
  AsyncSender<Input> sender;
  AsyncReceiver<Input> receiver(&sender);
  Input hello_world("hello world!");
  FUZZING_EXPECT_OK(receiver.Receive(), hello_world.Duplicate());
  EXPECT_EQ(sender.Send(std::move(hello_world)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, ReceiveAfterCancel) {
  AsyncSender<Input> sender;
  AsyncReceiver<Input> receiver(&sender);
  Input hello("hello");
  Input world("world");
  {
    // Discarding a promise shouldn't drop data.
    FUZZING_EXPECT_OK(receiver.Receive(), hello.Duplicate());
    auto discarded = receiver.Receive();
    FUZZING_EXPECT_OK(receiver.Receive(), world.Duplicate());
  }
  EXPECT_EQ(sender.Send(std::move(hello)), ZX_OK);
  EXPECT_EQ(sender.Send(std::move(world)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Close) {
  AsyncSender<Input> sender;
  Input hello("hello");
  Input world("world");

  // Close with items in the queue. Items sent before closing are still received.
  AsyncReceiver<Input> receiver1(&sender);
  FUZZING_EXPECT_OK(receiver1.Receive(), hello.Duplicate());
  FUZZING_EXPECT_ERROR(receiver1.Receive());
  EXPECT_EQ(sender.Send(hello.Duplicate()), ZX_OK);
  receiver1.Close();
  EXPECT_EQ(sender.Send(world.Duplicate()), ZX_ERR_PEER_CLOSED);
  RunUntilIdle();

  // Close with promises waiting to receive.
  AsyncReceiver<Input> receiver2(&sender);
  FUZZING_EXPECT_ERROR(receiver2.Receive());
  receiver2.Close();
  EXPECT_EQ(sender.Send(std::move(hello)), ZX_ERR_PEER_CLOSED);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Clear) {
  AsyncSender<Input> sender;
  Input sample("sample");

  // Clear with items in the queue.
  AsyncReceiver<Input> receiver1(&sender);
  EXPECT_EQ(sender.Send(sample.Duplicate()), ZX_OK);
  receiver1.Clear();
  FUZZING_EXPECT_ERROR(receiver1.Receive());
  RunUntilIdle();

  // Clear with promises waiting to receive.
  AsyncReceiver<Input> receiver2(&sender);
  FUZZING_EXPECT_ERROR(receiver2.Receive());
  receiver2.Clear();
  RunUntilIdle();

  EXPECT_EQ(sender.Send(std::move(sample)), ZX_ERR_PEER_CLOSED);
}

TEST_F(AsyncDequeTest, Reset) {
  AsyncSender<Input> sender;
  Input hello("hello");
  Input world("world");

  // Reset with items in the queue.
  AsyncReceiver<Input> receiver1(&sender);
  EXPECT_EQ(sender.Send(hello.Duplicate()), ZX_OK);
  receiver1.Reset();
  EXPECT_EQ(sender.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(receiver1.Receive(), world.Duplicate());
  RunUntilIdle();

  // Reset with promises waiting to receive.
  AsyncReceiver<Input> receiver2(&sender);
  FUZZING_EXPECT_ERROR(receiver2.Receive());
  receiver2.Reset();
  FUZZING_EXPECT_OK(receiver2.Receive(), hello.Duplicate());
  EXPECT_EQ(sender.Send(std::move(hello)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, MultipleThreads) {
  AsyncSender<size_t> sender;
  auto receiver = AsyncReceiver<size_t>::MakePtr(&sender);

  size_t num_ones = 0;
  size_t num_twos = 0;
  auto task = [receiver = std::move(receiver), &num_ones, &num_twos,
               receive = Future<size_t>()](Context& context) mutable -> Result<> {
    while (true) {
      if (!receive) {
        receive = receiver->Receive();
      }
      if (!receive(context)) {
        return fpromise::pending();
      }
      if (receive.is_error()) {
        return fpromise::ok();
      }
      switch (receive.take_value()) {
        case 1:
          ++num_ones;
          break;
        case 2:
          ++num_twos;
          break;
        default:
          FX_NOTREACHED();
      }
    }
  };
  FUZZING_EXPECT_OK(std::move(task));

  constexpr const size_t kNumOnes = 300;
  std::thread t1([sender = sender.Clone()]() mutable {
    for (auto i = 0u; i < kNumOnes; ++i) {
      EXPECT_EQ(sender.Send(1), ZX_OK);
    }
  });

  constexpr const size_t kNumTwos = 500;
  std::thread t2([sender = std::move(sender)]() mutable {
    for (auto i = 0u; i < kNumTwos; ++i) {
      EXPECT_EQ(sender.Send(2), ZX_OK);
    }
  });

  RunUntilIdle();
  t1.join();
  t2.join();

  EXPECT_EQ(num_ones, kNumOnes);
  EXPECT_EQ(num_twos, kNumTwos);
}

}  // namespace fuzzing
