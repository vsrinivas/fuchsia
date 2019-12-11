// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include "gtest/gtest.h"

TEST(Table, BuildTablePrimitive) {
  namespace test = llcpp::fidl::llcpp::types::test;
  uint8_t x = 3;
  uint8_t y = 100;
  auto builder = test::SampleTable::Build()
      .set_x(&x)
      .set_y(&y);
  const auto& table = builder.view();

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), x);
  ASSERT_EQ(table.y(), y);
}

TEST(Table, BuildTableVectorOfStruct) {
  namespace test = llcpp::fidl::llcpp::types::test;
  std::vector<test::CopyableStruct> structs = {
      {.x = 30},
      {.x = 42},
  };
  fidl::VectorView<test::CopyableStruct> vector_view(structs);
  auto builder = test::SampleTable::Build()
      .set_vector_of_struct(&vector_view);
  const auto& table = builder.view();

  ASSERT_FALSE(table.has_x());
  ASSERT_FALSE(table.has_y());
  ASSERT_TRUE(table.has_vector_of_struct());
  ASSERT_EQ(table.vector_of_struct().count(), structs.size());
  ASSERT_EQ(table.vector_of_struct()[0].x, structs[0].x);
  ASSERT_EQ(table.vector_of_struct()[1].x, structs[1].x);
}

TEST(Table, BuildEmptyTable) {
  namespace test = llcpp::fidl::llcpp::types::test;
  auto builder = test::SampleEmptyTable::Build();
  const auto& table = builder.view();
  ASSERT_TRUE(table.IsEmpty());
}
