// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/common/byte_buffer.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/common/test_helpers.h"

namespace bluetooth {
namespace common {
namespace {

TEST(ByteBufferTest, StaticByteBuffer) {
  constexpr size_t kBufferSize = 5;
  StaticByteBuffer<kBufferSize> buffer;

  EXPECT_EQ(kBufferSize, buffer.GetSize());
  buffer.SetToZeros();
  buffer.GetMutableData()[3] = 3;

  constexpr std::array<uint8_t, kBufferSize> kExpected{
      {0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will result in a copy.
  StaticByteBuffer<kBufferSize> buffer_copy = std::move(buffer);
  EXPECT_EQ(kBufferSize, buffer.GetSize());
  EXPECT_EQ(kBufferSize, buffer_copy.GetSize());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_copy));

  // Move contents into raw buffer. Calling MoveContents() should invalidate the
  // buffer contents.
  auto moved_contents = buffer.MoveContents();
  EXPECT_TRUE(ContainersEqual(kExpected, moved_contents.get(), kBufferSize));
  EXPECT_EQ(nullptr, buffer.GetData());
  EXPECT_EQ(nullptr, buffer.GetMutableData());
  EXPECT_EQ(0u, buffer.GetSize());
  EXPECT_EQ(buffer.cbegin(), buffer.cend());
}

TEST(ByteBufferTest, DynamicByteBuffer) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.GetSize());
  buffer.SetToZeros();
  buffer.GetMutableData()[3] = 3;

  constexpr std::array<uint8_t, kBufferSize> kExpected{
      {0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will invalidate the source buffer.
  DynamicByteBuffer buffer_moved = std::move(buffer);
  EXPECT_EQ(0u, buffer.GetSize());
  EXPECT_EQ(kBufferSize, buffer_moved.GetSize());
  EXPECT_EQ(nullptr, buffer.GetData());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_moved));

  // Move contents into raw buffer. Calling MoveContents() should invalidate the
  // buffer contents.
  auto moved_contents = buffer_moved.MoveContents();
  EXPECT_TRUE(ContainersEqual(kExpected, moved_contents.get(), kBufferSize));
  EXPECT_EQ(nullptr, buffer_moved.GetData());
  EXPECT_EQ(nullptr, buffer_moved.GetMutableData());
  EXPECT_EQ(0u, buffer_moved.GetSize());
  EXPECT_EQ(buffer.cbegin(), buffer.cend());
}

TEST(ByteBufferTest, DynamicByteBufferConstructFromBytes) {
  constexpr size_t kBufferSize = 3;
  std::array<uint8_t, kBufferSize> kExpected{{0, 1, 2}};

  auto bytes = std::make_unique<uint8_t[]>(kBufferSize);
  std::memcpy(bytes.get(), kExpected.data(), kBufferSize);

  DynamicByteBuffer buffer(kBufferSize, std::move(bytes));
  EXPECT_EQ(nullptr, bytes.get());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
