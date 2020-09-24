// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/test/test_util.h>

#include <vector>

#include <fidl/test/misc/cpp/fidl.h>
#include <gtest/gtest.h>

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

TEST(XUnion, FlexibleXUnionsEquality) {
  using test::misc::SampleXUnion;
  using test::misc::SampleXUnionInStruct;

  std::vector<uint8_t> input = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line data
  };

  auto s = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(input);
  auto xu = std::move(s.xu);

  auto s2 = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(input);
  auto xu2 = std::move(s2.xu);

  EXPECT_EQ(xu.Which(), SampleXUnion::Tag::kUnknown);
  EXPECT_EQ(xu.Ordinal(), 0xba5eba11);
  EXPECT_EQ(*xu.UnknownBytes(),
            std::vector<uint8_t>(input.cbegin() + sizeof(fidl_xunion_t), input.cend()));

  EXPECT_EQ(xu.Ordinal(), xu2.Ordinal());
  EXPECT_EQ(*xu.UnknownBytes(), *xu2.UnknownBytes());
  EXPECT_TRUE(fidl::Equals(xu, xu2));

  std::vector<uint8_t> different_unknown_data = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,  // DIFFERENT fake out-of-line data
  };

  auto s3 = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(different_unknown_data);
  auto xu3 = std::move(s3.xu);

  EXPECT_EQ(xu.Ordinal(), xu3.Ordinal());
  EXPECT_NE(*xu.UnknownBytes(), *xu3.UnknownBytes());
  EXPECT_FALSE(fidl::Equals(xu, xu3));

  std::vector<uint8_t> different_ordinal = {
      0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00,  // DIFFERENT invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line data
  };

  auto s4 = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(different_ordinal);
  auto xu4 = std::move(s4.xu);

  EXPECT_NE(xu.Ordinal(), xu4.Ordinal());
  EXPECT_EQ(*xu.UnknownBytes(), *xu4.UnknownBytes());
  EXPECT_FALSE(fidl::Equals(xu, xu4));
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
