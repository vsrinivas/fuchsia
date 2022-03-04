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
  pipe.Send(hello.Duplicate());
  pipe.Send(world.Duplicate());
  FUZZING_EXPECT_OK(pipe.Receive(), hello);
  FUZZING_EXPECT_OK(pipe.Receive(), world);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, ReceiveBeforeSend) {
  AsyncDeque<Input> pipe;
  Input hello_world("hello world!");
  FUZZING_EXPECT_OK(pipe.Receive(), hello_world);
  pipe.Send(hello_world.Duplicate());
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Resend) {
  AsyncDeque<Input> pipe;
  Input hello("hello");
  Input world("world");
  pipe.Resend(hello.Duplicate());
  pipe.Send(world.Duplicate());
  FUZZING_EXPECT_OK(pipe.Receive(), hello);
  pipe.Resend(hello.Duplicate());
  FUZZING_EXPECT_OK(pipe.Receive(), hello);
  FUZZING_EXPECT_OK(pipe.Receive(), world);
  RunUntilIdle();
}

TEST_F(AsyncDequeTest, Close) {
  AsyncDeque<Input> pipe1, pipe2;
  Input hello("hello");
  Input world("world");

  // Close with items in the queue.
  pipe1.Resend(hello.Duplicate());
  pipe1.Close();
  pipe1.Send(world.Duplicate());
  FUZZING_EXPECT_OK(pipe1.Receive(), hello);
  FUZZING_EXPECT_ERROR(pipe1.Receive());
  RunUntilIdle();

  // Close with promises waiting to receive.
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  pipe2.Close();
  pipe2.Send(hello.Duplicate());
  pipe2.Resend(world.Duplicate());
  FUZZING_EXPECT_ERROR(pipe2.Receive());
  RunUntilIdle();
}

}  // namespace fuzzing
