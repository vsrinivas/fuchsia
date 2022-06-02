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
  Input foo("foo");
  Input bar("bar");
  {
    // Discarding a promise shouldn't drop data.
    FUZZING_EXPECT_OK(pipe.Receive(), foo.Duplicate());
    auto discarded = pipe.Receive();
    FUZZING_EXPECT_OK(pipe.Receive(), bar.Duplicate());
  }
  EXPECT_EQ(pipe.Send(std::move(foo)), ZX_OK);
  EXPECT_EQ(pipe.Send(std::move(bar)), ZX_OK);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Resend) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  Input world("world");
  pipe.Resend(hello.Duplicate());
  EXPECT_EQ(pipe.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(pipe.Receive(), hello.Duplicate());
  pipe.Resend(hello.Duplicate());
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(hello));
  FUZZING_EXPECT_OK(pipe.Receive(), std::move(world));
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Close) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Close with items in the queue.
  pipe1.Resend(hello.Duplicate());
  EXPECT_FALSE(pipe1.is_closed());
  EXPECT_FALSE(pipe1.is_empty());
  pipe1.Close();
  EXPECT_TRUE(pipe1.is_closed());
  EXPECT_FALSE(pipe1.is_empty());
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_ERR_BAD_STATE);
  FUZZING_EXPECT_OK(pipe1.Receive(), hello.Duplicate());
  FUZZING_EXPECT_ERROR(pipe1.Receive());
  RunUntilIdle();

  // Close with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_FALSE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  pipe2.Close();
  EXPECT_TRUE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  EXPECT_EQ(pipe2.Send(std::move(world)), ZX_ERR_BAD_STATE);
  pipe2.Resend(hello.Duplicate());
  FUZZING_EXPECT_OK(pipe2.Receive(), std::move(hello));
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Clear) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Clear with items in the queue.
  pipe1.Resend(hello.Duplicate());
  EXPECT_FALSE(pipe1.is_closed());
  EXPECT_FALSE(pipe1.is_empty());
  pipe1.Clear();
  EXPECT_TRUE(pipe1.is_closed());
  EXPECT_TRUE(pipe1.is_empty());
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_ERR_BAD_STATE);
  FUZZING_EXPECT_ERROR(pipe1.Receive());
  RunUntilIdle();

  // Clear with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_FALSE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  pipe2.Clear();
  EXPECT_TRUE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  EXPECT_EQ(pipe2.Send(world.Duplicate()), ZX_ERR_BAD_STATE);
  pipe2.Resend(hello.Duplicate());
  FUZZING_EXPECT_OK(pipe2.Receive(), std::move(hello));
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Reset) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Reset with items in the queue.
  pipe1.Resend(hello.Duplicate());
  EXPECT_FALSE(pipe1.is_closed());
  EXPECT_FALSE(pipe1.is_empty());
  pipe1.Reset();
  EXPECT_FALSE(pipe1.is_closed());
  EXPECT_TRUE(pipe1.is_empty());
  EXPECT_EQ(pipe1.Send(world.Duplicate()), ZX_OK);
  FUZZING_EXPECT_OK(pipe1.Receive(), std::move(world));
  RunUntilIdle();

  // Reset with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  EXPECT_FALSE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  pipe2.Reset();
  EXPECT_FALSE(pipe2.is_closed());
  EXPECT_TRUE(pipe2.is_empty());
  EXPECT_EQ(pipe2.Send(world.Duplicate()), ZX_OK);
  pipe2.Resend(hello.Duplicate());
  FUZZING_EXPECT_OK(pipe2.Receive(), std::move(hello));
  FUZZING_EXPECT_OK(pipe2.Receive(), std::move(world));
  RunUntilIdle();
}

}  // namespace fuzzing
