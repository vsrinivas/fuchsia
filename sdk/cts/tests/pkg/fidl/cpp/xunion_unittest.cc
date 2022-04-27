// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/cpp/comparison.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "test/test_util.h"

namespace fidl {
namespace {

TEST(XUnion, SetterReturnsSelf) {
  using test::misc::SampleXUnion;
  using test::misc::SimpleTable;

  SimpleTable st;
  st.set_x(42);

  SampleXUnion u;
  u.set_st(std::move(st));

  EXPECT_TRUE(u.is_st());
  EXPECT_EQ(42, u.st().x());

  EXPECT_TRUE(fidl::Equals(u, SampleXUnion().set_st(std::move(SimpleTable().set_x(42)))));
}

TEST(XUnion, EmptyXUnionEquality) {
  using test::misc::SampleXUnion;

  EXPECT_TRUE(::fidl::Equals(SampleXUnion(), SampleXUnion()));
}

TEST(XUnion, XUnionFactoryFunctions) {
  using test::misc::SampleXUnion;
  using test::misc::SimpleTable;

  SampleXUnion prim_xu = SampleXUnion::WithI(123);

  EXPECT_EQ(prim_xu.i(), 123);
  EXPECT_EQ(prim_xu.Which(), SampleXUnion::Tag::kI);
  EXPECT_EQ(prim_xu.Ordinal(), SampleXUnion::Tag::kI);
  EXPECT_EQ(prim_xu.UnknownBytes(), nullptr);

  // Test passing an object with no copy constructor (only move) to the
  // factory function.
  SimpleTable tbl;
  SampleXUnion tbl_xu = SampleXUnion::WithSt(std::move(tbl));

  EXPECT_EQ(tbl_xu.Which(), SampleXUnion::Tag::kSt);
  EXPECT_EQ(tbl_xu.Ordinal(), SampleXUnion::Tag::kSt);
  EXPECT_EQ(tbl_xu.UnknownBytes(), nullptr);
}

// Confirms that an xunion can have a variant with both type and name identifiers as "empty" and not
// collide with any tags, internal or otherwise, indicating the xunion is in its unknown monostate.
TEST(XUnion, XUnionContainingEmptyStruct) {
  using test::misc::Empty;
  using test::misc::XUnionContainingEmptyStruct;

  XUnionContainingEmptyStruct xu;

  EXPECT_EQ(xu.Which(), XUnionContainingEmptyStruct::Tag::Invalid);
  EXPECT_FALSE(xu.is_empty());

  Empty empty;
  xu.set_empty(std::move(empty));
  EXPECT_EQ(xu.Which(), XUnionContainingEmptyStruct::Tag::kEmpty);
  EXPECT_TRUE(xu.is_empty());
}

}  // namespace
}  // namespace fidl
