// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Check that we can decode things that we can encode.

#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/internal.h>

#include <iostream>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/clone.h"
#include "test/test_util.h"

namespace fidl {
namespace test {
namespace misc {

namespace {

using fidl::test::util::RoundTrip;
using fidl::test::util::ValueToBytes;

TEST(SimpleStruct, SerializeAndDeserialize) {
  Int64Struct input{1};
  EXPECT_TRUE(fidl::Equals(input, RoundTrip<Int64Struct>(input)));
}

TEST(SimpleTable, CheckEmptyTable) {
  SimpleTable input;

  auto expected = std::vector<uint8_t>{
      0,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(SimpleTable, SerializeAndDeserialize) {
  SimpleTable input;
  input.set_x(1);
  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SimpleTable>(input)));
  // OlderSimpleTable is an abbreviated ('old') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, RoundTrip<OlderSimpleTable>(input).x());
  // NewerSimpleTable is an extended ('new') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, RoundTrip<NewerSimpleTable>(input).x());
}

TEST(SimpleTable, SerializeAndDeserializeWithReserved) {
  SimpleTable input;
  input.set_y(1);
  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SimpleTable>(input)));
  // OlderSimpleTable is an abbreviated ('old') version of SimpleTable:
  // We should be able to decode to it (but since it doesn't have y,
  // we can't ask for that!)
  EXPECT_FALSE(RoundTrip<OlderSimpleTable>(input).has_x());
  // NewerSimpleTable is an extended ('new') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, RoundTrip<NewerSimpleTable>(input).y());
}

TEST(Empty, SerializeAndDeserialize) {
  Empty input{};
  EXPECT_TRUE(fidl::Equals(input, RoundTrip<Empty>(input)));
}

TEST(Empty, CheckBytes) {
  Empty input;

  auto expected = std::vector<uint8_t>{
      0, 0, 0, 0, 0, 0, 0, 0,  // empty struct zero field + padding
  };
  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(EmptyStructSandwich, SerializeAndDeserialize) {
  EmptyStructSandwich input{
      .before = "before",
      .after = "after",
  };
  EXPECT_TRUE(fidl::Equals(input, RoundTrip<EmptyStructSandwich>(input)));
}

TEST(EmptyStructSandwich, CheckBytes) {
  EmptyStructSandwich input{.before = "before", .after = "after"};

  auto expected = std::vector<uint8_t>{
      6,   0,   0,   0,   0,   0,   0,   0,    // length of "before"
      255, 255, 255, 255, 255, 255, 255, 255,  // "before" is present
      0,   0,   0,   0,   0,   0,   0,   0,    // empty struct zero field + padding
      5,   0,   0,   0,   0,   0,   0,   0,    // length of "world"
      255, 255, 255, 255, 255, 255, 255, 255,  // "after" is present
      'b', 'e', 'f', 'o', 'r', 'e', 0,   0,    // "before" string + padding
      'a', 'f', 't', 'e', 'r', 0,   0,   0,    // "after" string + padding
  };
  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnion, SerializeAndDeserializeInt32) {
  SampleXUnionInStruct input;
  input.xu.set_i(0xdeadbeef);

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnionInStruct>(input)));
}

TEST(XUnion, SerializeAndDeserializeSimpleUnion) {
  SimpleUnion su;
  su.set_str("hello");

  SampleXUnionInStruct input;
  input.xu.set_su(std::move(su));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnionInStruct>(input)));
}

TEST(XUnion, SerializeAndDeserializeSimpleTable) {
  SimpleTable st;
  st.set_x(42);
  st.set_y(67);

  SampleXUnionInStruct input;
  input.xu.set_st(std::move(st));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnionInStruct>(input)));
}

TEST(OptionalXUnionInStruct, SerializeAndDeserializeAbsent) {
  OptionalXUnionInStruct input;
  input.before = "before";
  input.after = "after";

  OptionalXUnionInStruct output = RoundTrip<OptionalXUnionInStruct>(input);
  EXPECT_EQ(output.xu.get(), nullptr);
}

TEST(OptionalXUnionInStruct, SerializeAndDeserializePresent) {
  auto xu = std::make_unique<SampleXUnion>();
  xu->set_i(0xdeadbeef);

  OptionalXUnionInStruct input;
  input.before = "before";
  input.after = "after";
  input.xu = std::move(xu);

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<OptionalXUnionInStruct>(input)));
}

TEST(XUnionInTable, SerializeAndDeserialize) {
  SampleXUnion xu;
  xu.set_i(0xdeadbeef);

  XUnionInTable input;
  input.set_xu(std::move(xu));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<XUnionInTable>(input)));
}

TEST(PrimitiveArrayInTable, SerializeAndDeserialize) {
  PrimitiveArrayInTable input;
  std::array<int32_t, 9> array = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  input.set_arr(array);

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<PrimitiveArrayInTable>(input)));
}

}  // namespace

}  // namespace misc
}  // namespace test
}  // namespace fidl
