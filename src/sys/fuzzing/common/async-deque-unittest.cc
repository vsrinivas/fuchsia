// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-deque.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixture.

class AsyncDequeTest : public AsyncTest {};

// Unit tests.

TEST_F(AsyncDequeTest, TryReceive) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  auto result = pipe.TryReceive();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(pipe.Send(hello.Duplicate()), ZX_OK);
  result = pipe.TryReceive();
  EXPECT_EQ(result.value(), hello);
}

TEST_F(AsyncDequeTest, SendBeforeReceive) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  Input world("world");
  EXPECT_EQ(pipe.Send(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(pipe.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(hello));
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(world));
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, ReceiveBeforeSend) {
  AsyncDeque<Input> pipe;
  Input hello_world("hello world!");
  FUZZING_EXPECT_OK(pipe.Receive(), hello_world.Duplicate());
  EXPECT_EQ(pipe.Send(std::move(hello_world)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, ReceiveAfterCancel) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  Input world("world");
  {
    // Discarding a promise shouldn't drop data.
    FUZZING_EXPECT_OK(pipe.Receive(), hello.Duplicate());
    auto discarded = pipe.Receive();
    FUZZING_EXPECT_OK(pipe.Receive(), world.Duplicate());
  }
  EXPECT_EQ(pipe.Send(std::move(hello)), ZX_OK);
  EXPECT_EQ(pipe.Send(std::move(world)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Resend) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  Input world("world");
  EXPECT_EQ(pipe.Resend(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(pipe.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(pipe.Receive(), hello.Duplicate());
  EXPECT_EQ(pipe.Resend(hello.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(hello));
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(world));
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Close) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Close with items in the queue. Items sent before closing are still received.
  FUZZING_EXPECT_OK(pipe1.Receive(), hello.Duplicate());
  FUZZING_EXPECT_ERROR(pipe1.Receive());
  EXPECT_EQ(pipe1.Send(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kOpen);
  pipe1.Close();
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kClosing);
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_ERR_BAD_STATE);
  RunUntilIdle();

  // Close with promises waiting to receive. Items resent after closing are still received.
  FUZZING_EXPECT_OK(pipe2.Receive(), world.Duplicate());
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kOpen);
  pipe2.Close();
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kClosing);
  EXPECT_EQ(pipe2.Send(std::move(hello)), ZX_ERR_BAD_STATE);
  EXPECT_EQ(pipe2.Resend(std::move(world)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Clear) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Clear with items in the queue.
  FUZZING_EXPECT_ERROR(pipe1.Receive());
  EXPECT_EQ(pipe1.Resend(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kOpen);
  pipe1.Clear();
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kClosed);
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_ERR_BAD_STATE);
  RunUntilIdle();

  // Clear with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kOpen);
  pipe2.Clear();
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kClosed);
  RunUntilIdle();

  EXPECT_EQ(pipe2.Send(std::move(world)), ZX_ERR_BAD_STATE);
  EXPECT_EQ(pipe2.Resend(std::move(hello)), ZX_ERR_BAD_STATE);
}

TEST_F(AsyncDequeTest, Reset) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Reset with items in the queue.
  EXPECT_EQ(pipe1.Resend(hello.Duplicate()), ZX_OK);
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kOpen);
  pipe1.Reset();
  EXPECT_EQ(pipe1.state(), AsyncDequeState::kOpen);
  FUZZING_EXPECT_OK(pipe1.Receive(), world.Duplicate());
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_OK);
  RunUntilIdle();

  // Reset with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kOpen);
  pipe2.Reset();
  EXPECT_EQ(pipe2.state(), AsyncDequeState::kOpen);
  FUZZING_EXPECT_OK(pipe2.Receive(), hello.Duplicate());
  FUZZING_EXPECT_OK(pipe2.Receive(), world.Duplicate());
  EXPECT_EQ(pipe2.Send(std::move(world)), ZX_OK);
  EXPECT_EQ(pipe2.Resend(std::move(hello)), ZX_OK);
  RunUntilIdle();
}

}  // namespace fuzzing
