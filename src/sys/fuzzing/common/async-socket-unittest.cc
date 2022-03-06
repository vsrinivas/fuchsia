// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-socket.h"

#include <stddef.h>
#include <stdint.h>

#include <random>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixtures.

class AsyncSocketTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    prng_.seed(1);
  }

  Input Generate(size_t size) {
    Input input;
    input.Reserve(size);
    for (size_t i = 0; i < size; ++i) {
      input.Write(static_cast<uint8_t>(prng_()));
    }
    return input;
  }

 private:
  std::minstd_rand prng_;
};

// Unit tests.

TEST_F(AsyncSocketTest, ReadAndWriteInput) {
  auto input = Generate(1UL << 10);
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), input.Duplicate())),
                    input);
  RunUntilIdle();
}

TEST_F(AsyncSocketTest, ReadAndWriteEmptyInput) {
  Input input;
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), input.Duplicate())),
                    input);
  RunUntilIdle();
}

TEST_F(AsyncSocketTest, ReadAndWriteLargeInput) {
  auto input = Generate(1UL << 10);
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), input.Duplicate())),
                    input);
  RunUntilIdle();
}

TEST_F(AsyncSocketTest, ReadAndWriteArtifact) {
  Artifact artifact(FuzzResult::OOM, Generate(1UL << 10));
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), artifact.Duplicate())),
                    artifact);
  RunUntilIdle();
}

TEST_F(AsyncSocketTest, ReadAndWriteEmptyArtifact) {
  Artifact artifact(FuzzResult::OOM, Generate(1UL << 10));
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), artifact.Duplicate())),
                    artifact);
  RunUntilIdle();
}

TEST_F(AsyncSocketTest, ReadAndWriteLargeArtifact) {
  Artifact artifact(FuzzResult::OOM, Generate(1UL << 10));
  FUZZING_EXPECT_OK(AsyncSocketRead(executor(), AsyncSocketWrite(executor(), artifact.Duplicate())),
                    artifact);
  RunUntilIdle();
}

}  // namespace fuzzing
