// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/linearized.h>
#include <lib/fidl/llcpp/memory.h>

#include <fidl/llcpp/linearized/test/llcpp/fidl.h>
#include <gtest/gtest.h>

namespace fidl_linearized = ::llcpp::fidl::llcpp::linearized::test;

TEST(Linearized, NoOpLinearized) {
  fidl_linearized::NoOpLinearizedStruct input = {.x = 1};
  auto linearized = fidl::internal::Linearized<fidl_linearized::NoOpLinearizedStruct>(&input);
  EXPECT_EQ(linearized.result().status, ZX_OK);
  auto data = linearized.result().message.Release().data();
  EXPECT_EQ(data, reinterpret_cast<uint8_t*>(&input));
}

TEST(Linearized, FullyLinearized) {
  fidl_linearized::InnerStruct inner = {.x = 1};
  fidl_linearized::FullyLinearizedStruct input{.ptr = fidl::unowned_ptr(&inner)};
  auto linearized = fidl::internal::Linearized<fidl_linearized::FullyLinearizedStruct>(&input);
  EXPECT_EQ(linearized.result().status, ZX_OK);

  auto linearized_obj = reinterpret_cast<fidl_linearized::FullyLinearizedStruct*>(
      linearized.result().message.Release().data());
  EXPECT_NE(linearized_obj, &input);
  EXPECT_EQ(linearized_obj->ptr->x, input.ptr->x);
}
