// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzz_input.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <cstdlib>

namespace {

using namespace fuzzing;

TEST(FuzzInputTest, CantCopyBytesToNull) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput src(static_cast<uint8_t*>(data), 4);
  EXPECT_FALSE(src.CopyBytes(nullptr, 4));
}

TEST(FuzzInputTest, CantCopyObjectToNull) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput src(static_cast<uint8_t*>(data), 4);
  uint8_t* out = nullptr;
  EXPECT_FALSE(src.CopyObject(out));
}

TEST(FuzzInputTest, CanTakeBytes) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput src(static_cast<uint8_t*>(data), 4);
  const uint8_t* ret = src.TakeBytes(4);
  EXPECT_NE(nullptr, ret);
}

TEST(FuzzInputTest, CantTakeBytesTooBig) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  FuzzInput src(static_cast<uint8_t*>(data), 4);
  const uint8_t* ret = src.TakeBytes(5);
  EXPECT_EQ(nullptr, ret);
}

TEST(FuzzInputTest, CanCopy) {
  int value = 1;
  int* data = reinterpret_cast<int*>(malloc(sizeof(value)));
  *data = 2;
  FuzzInput src(reinterpret_cast<uint8_t*>(data), sizeof(value));
  EXPECT_TRUE(src.CopyObject(&value));
  EXPECT_EQ(2, value);
  free(data);
}

TEST(FuzzInputTest, CantCopyTooBig) {
  int value = 1;
  uint8_t* data = reinterpret_cast<uint8_t*>(malloc(sizeof(value) - 1));
  FuzzInput src(data, sizeof(value) - 1);
  EXPECT_FALSE(src.CopyObject(&value));
  free(data);
}

TEST(FuzzInputTest, TakeBytesAndCopyAccumulate) {
  int i;
  long l;
  const uint8_t* three_bytes_ptr = nullptr;
  uint8_t one_byte;
  const uint8_t* one_byte_ptr = nullptr;
  uint8_t* data = reinterpret_cast<uint8_t*>(malloc(sizeof(int) + sizeof(long) + 3));
  FuzzInput src(data, sizeof(int) + sizeof(long) + 3);
  EXPECT_TRUE(src.CopyObject(&i));
  EXPECT_TRUE(src.CopyObject(&l));
  three_bytes_ptr = src.TakeBytes(3);
  EXPECT_NE(nullptr, three_bytes_ptr);
  one_byte_ptr = src.TakeBytes(1);
  EXPECT_EQ(nullptr, one_byte_ptr);
  EXPECT_FALSE(src.CopyObject(&one_byte));
  free(data);
}

}  // namespace
