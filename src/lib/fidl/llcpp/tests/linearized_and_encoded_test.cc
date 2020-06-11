// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/linearized_and_encoded.h>
#include <lib/fidl/llcpp/memory.h>

#include <fidl/llcpp/linearized/test/llcpp/fidl.h>
#include <gtest/gtest.h>

namespace fidl_linearized = ::llcpp::fidl::llcpp::linearized::test;

TEST(LinearizedAndEncoded, EncodeOnly) {
  fidl_linearized::NoOpLinearizedStruct input = {.x = 1};
  auto encoded =
      fidl::internal::LinearizedAndEncoded<fidl_linearized::NoOpLinearizedStruct>(&input);
  EXPECT_EQ(encoded.result().status, ZX_OK);
  auto data = encoded.result().message.bytes().data();
  EXPECT_EQ(data, reinterpret_cast<uint8_t*>(&input));
}

TEST(LinearizedAndEncoded, FullyLinearizedAndEncoded) {
  fidl_linearized::InnerStruct inner = {.x = 1};
  fidl_linearized::FullyLinearizedStruct input{.ptr = fidl::unowned_ptr(&inner)};
  auto encoded =
      fidl::internal::LinearizedAndEncoded<fidl_linearized::FullyLinearizedStruct>(&input);
  EXPECT_EQ(encoded.result().status, ZX_OK);

  auto encoded_obj = reinterpret_cast<fidl_linearized::FullyLinearizedStruct*>(
      encoded.result().message.bytes().data());
  EXPECT_NE(encoded_obj, &input);
  EXPECT_EQ(*reinterpret_cast<uintptr_t*>(&encoded_obj->ptr), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(reinterpret_cast<fidl_linearized::InnerStruct*>(encoded_obj + 1)->x, input.ptr->x);
}
