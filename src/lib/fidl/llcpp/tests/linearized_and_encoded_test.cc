// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/message.h>

#include <fidl/llcpp/linearized/test/llcpp/fidl.h>
#include <gtest/gtest.h>

namespace fidl_linearized = ::llcpp::fidl::llcpp::linearized::test;

TEST(LinearizedAndEncoded, FullyLinearizedAndEncoded) {
  fidl_linearized::InnerStruct inner = {.x = 1};
  fidl_linearized::FullyLinearizedStruct input{.ptr = fidl::unowned_ptr(&inner)};
  fidl::OwnedOutgoingMessage<fidl_linearized::FullyLinearizedStruct> encoded(&input);
  EXPECT_TRUE(encoded.ok());

  auto encoded_obj = reinterpret_cast<fidl_linearized::FullyLinearizedStruct*>(
      encoded.GetOutgoingMessage().bytes());
  EXPECT_NE(encoded_obj, &input);
  EXPECT_EQ(*reinterpret_cast<uintptr_t*>(&encoded_obj->ptr), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(reinterpret_cast<fidl_linearized::InnerStruct*>(encoded_obj + 1)->x, input.ptr->x);
}
