// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzz_input.h"

#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

namespace {

using namespace fuzzing;

TEST(FuzzInputTest, CanTakeBytes) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput fd(static_cast<uint8_t*>(data), 4);
  const uint8_t* ret = fd.TakeBytes(4);
  EXPECT_NE(nullptr, ret);
}

TEST(FuzzInputTest, CantTakeBytesTooBig) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput fd(static_cast<uint8_t*>(data), 4);
  const uint8_t* ret = fd.TakeBytes(5);
  EXPECT_EQ(nullptr, ret);
}

TEST(FuzzInputTest, CanCopy) {
  int value = 1;
  int* data = reinterpret_cast<int*>(malloc(sizeof(value)));
  *data = 2;
  FuzzInput fd(reinterpret_cast<uint8_t*>(data), sizeof(value));
  EXPECT_TRUE(fd.CopyObject(&value));
  EXPECT_EQ(2, value);
  free(data);
}

TEST(FuzzInputTest, CantCopyTooBig) {
  int value = 1;
  uint8_t* data = reinterpret_cast<uint8_t*>(malloc(sizeof(value) - 1));
  FuzzInput fd(data, sizeof(value) - 1);
  EXPECT_FALSE(fd.CopyObject(&value));
  free(data);
}

TEST(FuzzInputTest, TakeBytesAndCopyAccumulate) {
  int i;
  long l;
  const uint8_t* three_bytes_ptr = nullptr;
  uint8_t one_byte;
  const uint8_t* one_byte_ptr = nullptr;
  uint8_t* data = reinterpret_cast<uint8_t*>(malloc(sizeof(int) + sizeof(long) + 3));
  FuzzInput fd(data, sizeof(int) + sizeof(long) + 3);
  EXPECT_TRUE(fd.CopyObject(&i));
  EXPECT_TRUE(fd.CopyObject(&l));
  three_bytes_ptr = fd.TakeBytes(3);
  EXPECT_NE(nullptr, three_bytes_ptr);
  one_byte_ptr = fd.TakeBytes(1);
  EXPECT_EQ(nullptr, one_byte_ptr);
  EXPECT_FALSE(fd.CopyObject(&one_byte));
  free(data);
}

}  // namespace
