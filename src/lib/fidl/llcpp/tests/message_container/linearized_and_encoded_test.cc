// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/message.h>

#include <fidl/llcpp/linearized/test/llcpp/fidl.h>
#include <gtest/gtest.h>

namespace fidl_linearized = ::fidl_llcpp_linearized_test;

TEST(LinearizedAndEncoded, FullyLinearizedAndEncoded) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  fidl::OwnedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(&input);
  EXPECT_TRUE(encoded.ok());

  auto message_bytes = encoded.GetOutgoingMessage().CopyBytes();
  auto encoded_obj =
      reinterpret_cast<const fidl_linearized::wire::FullyLinearizedStruct*>(message_bytes.data());
  EXPECT_NE(encoded_obj, &input);
  EXPECT_EQ(*reinterpret_cast<const uintptr_t*>(&encoded_obj->ptr), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(reinterpret_cast<const fidl_linearized::wire::InnerStruct*>(encoded_obj + 1)->x,
            input.ptr->x);
}
