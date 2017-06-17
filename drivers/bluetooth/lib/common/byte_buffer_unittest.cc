// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/common/byte_buffer.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/test_helpers.h"

namespace bluetooth {
namespace common {
namespace {

TEST(ByteBufferTest, StaticByteBuffer) {
  constexpr size_t kBufferSize = 5;
  StaticByteBuffer<kBufferSize> buffer;

  EXPECT_EQ(kBufferSize, buffer.size());
  buffer.SetToZeros();
  buffer[3] = 3;

  constexpr std::array<uint8_t, kBufferSize> kExpected{{0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will result in a copy.
  StaticByteBuffer<kBufferSize> buffer_copy = std::move(buffer);
  EXPECT_EQ(kBufferSize, buffer.size());
  EXPECT_EQ(kBufferSize, buffer_copy.size());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_copy));

  // Transfer contents into raw buffer.
  auto contents = buffer.CopyContents();
  EXPECT_TRUE(ContainersEqual(kExpected, contents.get(), kBufferSize));
  EXPECT_EQ(kBufferSize, buffer.size());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

TEST(ByteBufferTest, StaticByteBufferVariadicConstructor) {
  constexpr size_t kBufferSize = 3;
  StaticByteBuffer<kBufferSize> buffer0;
  buffer0[0] = 0x01;
  buffer0[1] = 0x02;
  buffer0[2] = 0x03;

  StaticByteBuffer<kBufferSize> buffer1{0x01, 0x02, 0x03};
  auto buffer2 = CreateStaticByteBuffer(0x01, 0x02, 0x03);

  EXPECT_TRUE(ContainersEqual(buffer0, buffer1));
  EXPECT_TRUE(ContainersEqual(buffer0, buffer2));
  EXPECT_TRUE(ContainersEqual(buffer1, buffer2));
}

TEST(ByteBufferTest, DynamicByteBuffer) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.size());
  buffer.SetToZeros();
  buffer[3] = 3;

  constexpr std::array<uint8_t, kBufferSize> kExpected{{0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will invalidate the source buffer.
  DynamicByteBuffer buffer_moved = std::move(buffer);
  EXPECT_EQ(0u, buffer.size());
  EXPECT_EQ(kBufferSize, buffer_moved.size());
  EXPECT_EQ(nullptr, buffer.data());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_moved));

  // Test CopyContents().
  auto contents = buffer_moved.CopyContents();
  EXPECT_TRUE(ContainersEqual(buffer_moved, contents.get(), kBufferSize));
  EXPECT_EQ(buffer.cbegin(), buffer.cend());
}

TEST(ByteBufferTest, DynamicByteBufferConstructFromBuffer) {
  constexpr size_t kBufferSize = 3;
  StaticByteBuffer<kBufferSize> buffer({1, 2, 3});

  DynamicByteBuffer dyn_buffer(buffer);
  EXPECT_EQ(kBufferSize, dyn_buffer.size());
  EXPECT_TRUE(ContainersEqual(buffer, dyn_buffer));
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

TEST(ByteBufferTest, BufferViewTest) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.size());
  buffer.SetToZeros();

  BufferView view(buffer);
  EXPECT_EQ(0x00, buffer[0]);
  EXPECT_EQ(0x00, view[0]);
  EXPECT_EQ(kBufferSize, buffer.size());
  EXPECT_EQ(kBufferSize, view.size());
}

TEST(ByteBufferTest, MutableBufferViewTest) {
  constexpr size_t kBufferSize = 5;
  constexpr size_t kViewSize = 3;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.size());
  buffer.SetToZeros();

  MutableBufferView view(buffer.mutable_data(), kViewSize);
  view[0] = 0x01;
  EXPECT_EQ(0x01, buffer[0]);
  EXPECT_EQ(0x01, view[0]);
  EXPECT_EQ(kBufferSize, buffer.size());
  EXPECT_EQ(kViewSize, view.size());

  MutableBufferView view2(view);
  view2[0] = 0x00;
  EXPECT_EQ(0x00, buffer[0]);
  EXPECT_EQ(0x00, view[0]);
  EXPECT_EQ(0x00, view2[0]);
  EXPECT_EQ(kBufferSize, buffer.size());
  EXPECT_EQ(kViewSize, view.size());
}

TEST(ByteBufferTest, AsString) {
  auto buffer = common::CreateStaticByteBuffer('T', 'e', 's', 't');
  EXPECT_EQ("Test", buffer.AsString());
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
