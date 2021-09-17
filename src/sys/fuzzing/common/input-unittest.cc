// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/input.h"

#include <thread>

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

TEST(InputTest, DefaultConstructor) {
  Input input;
  EXPECT_EQ(input.size(), 0U);
  EXPECT_EQ(input.data(), nullptr);
}

TEST(InputTest, VectorConstructor) {
  std::vector<uint8_t> bytes = {0xde, 0xad, 0xbe, 0xef};
  Input input(bytes);

  ASSERT_EQ(input.size(), bytes.size());
  EXPECT_EQ(memcmp(input.data(), bytes.data(), bytes.size()), 0);
}

TEST(InputTest, Equality) {
  Input input({0xde, 0xad, 0xbe, 0xef});
  Input input1({0xde, 0xad, 0xbe});
  Input input2({0xde, 0xad, 0xbe, 0xef, 0x00});
  Input input3({0xde, 0xad, 0xbe, 0xfe});
  Input input4({0xde, 0xad, 0xbe, 0xef});
  EXPECT_NE(input, input1);
  EXPECT_NE(input, input2);
  EXPECT_NE(input, input3);
  EXPECT_EQ(input, input4);
}

TEST(InputTest, ToHex) {
  Input input({0xde, 0xad, 0xbe, 0xef});
  EXPECT_EQ(input.ToHex(), std::string("deadbeef"));
}

TEST(InputTest, Duplicate) {
  Input input1({0xfe, 0xed, 0xfa, 0xce});
  input1.set_num_features(5);
  auto input2 = input1.Duplicate();
  EXPECT_EQ(input1.ToHex(), input2.ToHex());
  EXPECT_EQ(input2.num_features(), 5U);
}

TEST(InputTest, StringConstructor) {
  Input input1({0xde, 0xad, 0xbe, 0xef});
  Input input2("\xde\xad\xbe\xef");
  EXPECT_EQ(input1.ToHex(), input2.ToHex());
}

TEST(InputTest, SharedMemoryConstructor) {
  Input input1({0xde, 0xad, 0xbe, 0xef});
  SharedMemory shmem;
  shmem.Reserve(input1.size());
  shmem.Write(input1.data(), input1.size());
  Input input2(shmem);
  EXPECT_EQ(input1.ToHex(), input2.ToHex());
}

TEST(InputTest, MoveAssignment) {
  Input input1({0xde, 0xad, 0xbe, 0xef});
  auto input2 = input1.Duplicate();
  input2.set_num_features(7);
  Input input3;
  input3 = std::move(input2);
  EXPECT_EQ(input1.ToHex(), input3.ToHex());
  EXPECT_EQ(input3.num_features(), 7U);
}

TEST(InputTest, MoveConstructor) {
  Input input1({0xde, 0xad, 0xbe, 0xef});
  input1.set_num_features(11);
  auto input2 = input1.Duplicate();
  Input input3(std::move(input1));
  EXPECT_EQ(input2.ToHex(), input3.ToHex());
  EXPECT_EQ(input3.num_features(), 11U);
}

TEST(InputTest, ReserveWriteAndTruncate) {
  Input input1({0xfe, 0xed, 0xfa, 0xce});

  Input input2;
  input2.Reserve(1);
  EXPECT_EQ(input2.capacity(), 1U);
  input2.Write(input1.data()[0]);
  auto input3 = input1.Duplicate();
  input3.Truncate(1);
  EXPECT_EQ(input2.ToHex(), input3.ToHex());
}

TEST(InputTest, ReserveWriteAndShrink) {
  Input input1({0xfe, 0xed, 0xfa, 0xce});

  Input input2;
  input2.Reserve(8);
  EXPECT_EQ(input2.capacity(), 8U);
  input2.Write(input1.data(), input1.size());
  EXPECT_EQ(input1.ToHex(), input2.ToHex());

  input2.ShrinkToFit();
  EXPECT_EQ(input2.capacity(), 4U);
  EXPECT_EQ(input1.ToHex(), input2.ToHex());
}

TEST(InputTest, Clear) {
  Input input({0xfe, 0xed, 0xfa, 0xce});
  input.Clear();

  // Sets size of valid data to 0...
  EXPECT_EQ(input.size(), 0U);

  // ..but doesn't touch capacity or the actual allocation.
  EXPECT_NE(input.data(), nullptr);
  EXPECT_EQ(input.capacity(), 4U);
}

}  // namespace
}  // namespace fuzzing
