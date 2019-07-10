// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/test/test_util.h>

#include <vector>

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
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: # of bytes + # of handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line data
  };

  auto s = ::fidl::test::util::DecodedBytes<SampleXUnionInStruct>(input);
  SampleXUnion xu = std::move(s.xu);

  EXPECT_EQ(xu.Which(), SampleXUnion::Tag::kUnknown);
  EXPECT_EQ(xu.Ordinal(), 0xba5eba11);
  EXPECT_EQ(*xu.UnknownData(), std::vector<uint8_t>(input.cbegin() + 24, input.cend()));

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
  EXPECT_EQ(*xu.UnknownData(), std::vector<uint8_t>(input.cbegin() + 24, input.cend()));

  EXPECT_EQ(xu.Ordinal(), xu2.Ordinal());
  EXPECT_EQ(*xu.UnknownData(), *xu2.UnknownData());
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
  EXPECT_NE(*xu.UnknownData(), *xu3.UnknownData());
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
  EXPECT_EQ(*xu.UnknownData(), *xu4.UnknownData());
  EXPECT_FALSE(fidl::Equals(xu, xu4));
}

}  // namespace
}  // namespace fidl
