// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/test/test_util.h>

#include <vector>

#include <fidl/test/misc/cpp/fidl.h>

#include "gtest/gtest.h"

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

// TODO(FIDL-725): Port this test to GIDL.
TEST(XUnion, FlexibleXUnionWithUnknownData) {
  using test::misc::SampleXUnion;
  using test::misc::SampleXUnionInStruct;

  std::vector<uint8_t> input = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line data
  };

  auto s = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(input);
  SampleXUnion xu = std::move(s.xu);

  EXPECT_EQ(xu.Which(), SampleXUnion::Tag::kUnknown);
  EXPECT_EQ(xu.Ordinal(), 0xba5eba11);
  EXPECT_EQ(*xu.UnknownData(), std::vector<uint8_t>(input.cbegin() + sizeof(fidl_message_header_t) +
                                                        sizeof(fidl_xunion_t),
                                                    input.cend()));

  // Reset the xunion to a known field, and ensure it behaves correctly.
  xu.set_i(5);
  EXPECT_EQ(xu.i(), 5);
  EXPECT_EQ(xu.Which(), SampleXUnion::Tag::kI);
  EXPECT_EQ(xu.Ordinal(), SampleXUnion::Tag::kI);
  EXPECT_EQ(xu.UnknownData(), nullptr);
}

TEST(XUnion, EmptyXUnionEquality) {
  using test::misc::SampleXUnion;

  EXPECT_TRUE(::fidl::Equals(SampleXUnion(), SampleXUnion()));
}

TEST(XUnion, FlexibleXUnionsEquality) {
  using test::misc::SampleXUnion;
  using test::misc::SampleXUnionInStruct;

  std::vector<uint8_t> input = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // transaction header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
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
  EXPECT_EQ(*xu.UnknownData(), std::vector<uint8_t>(input.cbegin() + sizeof(fidl_message_header_t) +
                                                        sizeof(fidl_xunion_t),
                                                    input.cend()));

  EXPECT_EQ(xu.Ordinal(), xu2.Ordinal());
  EXPECT_EQ(*xu.UnknownData(), *xu2.UnknownData());
  EXPECT_TRUE(fidl::Equals(xu, xu2));

  std::vector<uint8_t> different_unknown_data = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // transaction header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,  // DIFFERENT fake out-of-line data
  };

  auto s3 = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(different_unknown_data);
  auto xu3 = std::move(s3.xu);

  EXPECT_EQ(xu.Ordinal(), xu3.Ordinal());
  EXPECT_NE(*xu.UnknownData(), *xu3.UnknownData());
  EXPECT_FALSE(fidl::Equals(xu, xu3));

  std::vector<uint8_t> different_ordinal = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // transaction header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
      0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00,  // DIFFERENT invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line data
  };

  auto s4 = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(different_ordinal);
  auto xu4 = std::move(s4.xu);

  EXPECT_NE(xu.Ordinal(), xu4.Ordinal());
  EXPECT_EQ(*xu.UnknownData(), *xu4.UnknownData());
  EXPECT_FALSE(fidl::Equals(xu, xu4));
}

TEST(XUnion, XUnionFactoryFunctions) {
  using test::misc::SampleXUnion;
  using test::misc::SimpleTable;

  SampleXUnion prim_xu = SampleXUnion::WithI(123);

  EXPECT_EQ(prim_xu.i(), 123);
  EXPECT_EQ(prim_xu.Which(), SampleXUnion::Tag::kI);
  EXPECT_EQ(prim_xu.Ordinal(), SampleXUnion::Tag::kI);
  EXPECT_EQ(prim_xu.UnknownData(), nullptr);

  // Test passing an object with no copy constructor (only move) to the
  // factory function.
  SimpleTable tbl;
  SampleXUnion tbl_xu = SampleXUnion::WithSt(std::move(tbl));

  EXPECT_EQ(tbl_xu.Which(), SampleXUnion::Tag::kSt);
  EXPECT_EQ(tbl_xu.Ordinal(), SampleXUnion::Tag::kSt);
  EXPECT_EQ(tbl_xu.UnknownData(), nullptr);
}

// Confirms that an xunion can have a variant with both type and name identifiers as "empty" and not
// collide with any tags, internal or otherwise, indicating the xunion is in its unknown monostate.
TEST(XUnion, XUnionContainingEmptyStruct) {
  using test::misc::Empty;
  using test::misc::XUnionContainingEmptyStruct;

  XUnionContainingEmptyStruct xu;

  EXPECT_EQ(xu.Which(), XUnionContainingEmptyStruct::Tag::kUnknown);
  EXPECT_FALSE(xu.is_empty());

  Empty empty;
  xu.set_empty(std::move(empty));
  EXPECT_EQ(xu.Which(), XUnionContainingEmptyStruct::Tag::kEmpty);
  EXPECT_TRUE(xu.is_empty());
}

TEST(XUnion, ReadBothOrdinals) {
  using test::misc::SampleXUnion;
  using test::misc::SampleXUnionInStruct;

  std::vector<uint8_t> hashed_ordinal = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
      0xa5, 0x47, 0xdf, 0x29, 0x00, 0x00, 0x00, 0x00,  // hashed I ordinal
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x00, 0x00,  // I (uint32) + padding
  };

  auto hashed_s = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(hashed_ordinal);
  auto hashed_xu = std::move(hashed_s.xu);

  EXPECT_EQ(hashed_xu.Ordinal(), 1ul);
  EXPECT_TRUE(hashed_xu.is_i());
  EXPECT_EQ(hashed_xu.Which(), SampleXUnion::Tag::kI);
  EXPECT_EQ(hashed_xu.i(), -272716322);

  std::vector<uint8_t> explicit_ordinal = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // explicit I ordinal
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x00, 0x00,  // I (int32) + padding
  };

  auto explicit_s = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(hashed_ordinal);
  auto explicit_xu = std::move(explicit_s.xu);

  EXPECT_EQ(explicit_xu.Ordinal(), 1ul);
  EXPECT_TRUE(explicit_xu.is_i());
  EXPECT_EQ(explicit_xu.Which(), SampleXUnion::Tag::kI);
  EXPECT_EQ(explicit_xu.i(), -272716322);

  // Regardless of which kind of ordinal was encounted when decoding, the explicit version is used
  // when encoding
  EXPECT_TRUE(::fidl::Equals(hashed_xu, explicit_xu));
  bool equality =
      fidl::test::util::ValueToBytes<decltype(hashed_xu), fidl::test::util::EncoderFactoryV1>(
          hashed_xu, std::vector<uint8_t>(explicit_ordinal.begin() + 16, explicit_ordinal.end()));
  EXPECT_TRUE(equality);
  equality =
      fidl::test::util::ValueToBytes<decltype(explicit_xu), fidl::test::util::EncoderFactoryV1>(
          explicit_xu, std::vector<uint8_t>(explicit_ordinal.begin() + 16, explicit_ordinal.end()));
  EXPECT_TRUE(equality);
}

}  // namespace
}  // namespace fidl
