// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.linearized.test/cpp/wire.h>
#include <lib/fidl/llcpp/message.h>

#include <gtest/gtest.h>

namespace fidl_linearized = ::fidl_llcpp_linearized_test;

// Successful encode must include the inline and out of line objects in the buffer.
constexpr size_t kSizeJustRight = FIDL_ALIGN(sizeof(fidl_linearized::wire::FullyLinearizedStruct) +
                                             sizeof(fidl_linearized::wire::InnerStruct));
// ZX_ERR_INVALID_ARGS failures happen when the buffer size is less than the size of all objects.
constexpr size_t kSizeTooSmall = sizeof(fidl_linearized::wire::FullyLinearizedStruct);

TEST(Encoded, CallerAllocateEncoded) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  uint8_t bytes[kSizeJustRight];
  fidl::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      bytes, std::size(bytes), &input);
  EXPECT_TRUE(encoded.ok());

  auto message_bytes = encoded.GetOutgoingMessage().CopyBytes();
  auto encoded_obj =
      reinterpret_cast<const fidl_linearized::wire::FullyLinearizedStruct*>(message_bytes.data());
  ASSERT_NE(nullptr, encoded_obj);
  EXPECT_NE(encoded_obj, &input);
  EXPECT_EQ(*reinterpret_cast<const uintptr_t*>(&encoded_obj->ptr), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(reinterpret_cast<const fidl_linearized::wire::InnerStruct*>(encoded_obj + 1)->x,
            input.ptr->x);
}

TEST(Encoded, BufferTooSmall) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  uint8_t bytes[kSizeTooSmall];
  fidl::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      bytes, std::size(bytes), &input);
  // TODO(fxbug.dev/74362) This test and the EarlyCatch* version should use the same error.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, encoded.status());
}

TEST(Encoded, EarlyCatchBufferTooSmall) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  // Allocate a buffer that follows FIDL alignment.
  FIDL_ALIGNDECL uint8_t bytes[kSizeTooSmall];
  constexpr size_t kEarlyCatchSizeTooSmall = 0;
  fidl::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      bytes, kEarlyCatchSizeTooSmall, &input);
  // ZX_ERR_BUFFER_TOO_SMALL failures only happen when the buffer size is less than the inline size.
  // TODO(fxbug.dev/74362) This should use the same error as the non EarlyCatch* test.
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, encoded.status());
}
