// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/comparison.h>

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

namespace fidl {
namespace {

TEST(Table, IsEmpty) {
  test::misc::SimpleTable input;
  EXPECT_TRUE(input.IsEmpty());
  input.set_x(42);
  EXPECT_FALSE(input.IsEmpty());
}

TEST(Table, ChainSimpleSetters) {
  // Chain setters of simple types.
  test::misc::SimpleTable simple;
  simple.set_x(10);
  simple.set_y(20);
  EXPECT_EQ(10, simple.x());
  EXPECT_EQ(20, simple.y());

  // Chaining the setters gives you the same result:
  EXPECT_TRUE(fidl::Equals(simple, test::misc::SimpleTable().set_x(10).set_y(20)));
}

TEST(Table, ChainComplexSetters) {
  test::misc::SimpleTable simple;
  simple.set_x(10);

  test::misc::SampleXUnion u1, u2;
  u1.set_i(32);
  u1.Clone(&u2);

  test::misc::ComplexTable table;
  table.set_simple(std::move(simple));
  table.set_u(std::move(u1));
  table.set_strings({"a", "b"});

  EXPECT_TRUE(fidl::Equals(table, test::misc::ComplexTable()
                                      .set_simple(std::move(test::misc::SimpleTable().set_x(10)))
                                      .set_u(std::move(u2))
                                      .set_strings({"a", "b"})));
}

}  // namespace
}  // namespace fidl
