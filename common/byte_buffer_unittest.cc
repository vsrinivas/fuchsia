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

  constexpr std::array<uint8_t, kBufferSize> kExpected{{0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will result in a copy.
  StaticByteBuffer<kBufferSize> buffer_copy = std::move(buffer);
  EXPECT_EQ(kBufferSize, buffer.GetSize());
  EXPECT_EQ(kBufferSize, buffer_copy.GetSize());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_copy));

  // Transfer contents into raw buffer.
  auto contents = buffer.TransferContents();
  EXPECT_TRUE(ContainersEqual(kExpected, contents.get(), kBufferSize));
  EXPECT_EQ(kBufferSize, buffer.GetSize());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

TEST(ByteBufferTest, StaticByteBufferVariadicConstructor) {
  constexpr size_t kBufferSize = 3;
  StaticByteBuffer<kBufferSize> buffer0;
  buffer0.GetMutableData()[0] = 0x01;
  buffer0.GetMutableData()[1] = 0x02;
  buffer0.GetMutableData()[2] = 0x03;

  StaticByteBuffer<kBufferSize> buffer1{0x01, 0x02, 0x03};
  auto buffer2 = CreateStaticByteBuffer(0x01, 0x02, 0x03);

  EXPECT_TRUE(ContainersEqual(buffer0, buffer1));
  EXPECT_TRUE(ContainersEqual(buffer0, buffer2));
  EXPECT_TRUE(ContainersEqual(buffer1, buffer2));
}

TEST(ByteBufferTest, DynamicByteBuffer) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.GetSize());
  buffer.SetToZeros();
  buffer.GetMutableData()[3] = 3;

  constexpr std::array<uint8_t, kBufferSize> kExpected{{0x00, 0x00, 0x00, 0x03, 0x00}};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));

  // Moving will invalidate the source buffer.
  DynamicByteBuffer buffer_moved = std::move(buffer);
  EXPECT_EQ(0u, buffer.GetSize());
  EXPECT_EQ(kBufferSize, buffer_moved.GetSize());
  EXPECT_EQ(nullptr, buffer.GetData());
  EXPECT_TRUE(ContainersEqual(kExpected, buffer_moved));

  // Transfer contents into raw buffer. Calling TransferContents() should
  // invalidate the buffer contents.
  auto contents = buffer_moved.TransferContents();
  EXPECT_TRUE(ContainersEqual(kExpected, contents.get(), kBufferSize));
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

TEST(ByteBufferTest, BufferViewTest) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.GetSize());
  buffer.SetToZeros();

  BufferView view(buffer);
  EXPECT_EQ(0x00, buffer.GetData()[0]);
  EXPECT_EQ(0x00, view.GetData()[0]);
  EXPECT_EQ(kBufferSize, buffer.GetSize());
  EXPECT_EQ(kBufferSize, view.GetSize());
}

TEST(ByteBufferTest, MutableBufferViewTest) {
  constexpr size_t kBufferSize = 5;
  DynamicByteBuffer buffer(kBufferSize);

  EXPECT_EQ(kBufferSize, buffer.GetSize());
  buffer.SetToZeros();

  MutableBufferView view(&buffer);

  // It should be possible to mutate the contents of the underlying buffer.
  view.GetMutableData()[0] = 0xFF;
  EXPECT_EQ(0xFF, buffer.GetData()[0]);
  view.SetToZeros();
  EXPECT_EQ(0x00, buffer.GetData()[0]);

  // Calling TransferContents() should leave |buffer| untouched.
  auto contents = view.TransferContents();
  EXPECT_EQ(kBufferSize, buffer.GetSize());
  EXPECT_EQ(kBufferSize, view.GetSize());
}

TEST(ByteBufferTest, AsString) {
  auto buffer = common::CreateStaticByteBuffer('T', 'e', 's', 't');
  EXPECT_EQ("Test", buffer.AsString());
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
