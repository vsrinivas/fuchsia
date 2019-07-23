// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Check that we can decode things that we can encode.

#include <lib/fidl/internal.h>

#include <iostream>

#include <fidl/test/misc/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/test/test_util.h"

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

std::vector<uint8_t> kSimpleTable_X_42_Y_67 = std::vector<uint8_t>{
    5,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
    255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
    8,   0,   0,   0,   0,   0,   0,   0,    // envelope 1: num bytes / num handles
    255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
    0,   0,   0,   0,   0,   0,   0,   0,    // envelope 2: num bytes / num handles
    0,   0,   0,   0,   0,   0,   0,   0,    // no alloc
    0,   0,   0,   0,   0,   0,   0,   0,    // envelope 3: num bytes / num handles
    0,   0,   0,   0,   0,   0,   0,   0,    // no alloc
    0,   0,   0,   0,   0,   0,   0,   0,    // envelope 4: num bytes / num handles
    0,   0,   0,   0,   0,   0,   0,   0,    // no alloc
    8,   0,   0,   0,   0,   0,   0,   0,    // envelope 5: num bytes / num handles
    255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
    42,  0,   0,   0,   0,   0,   0,   0,    // field X
    67,  0,   0,   0,   0,   0,   0,   0,    // field Y
};

TEST(SimpleTable, CheckBytesWithXY) {
  SimpleTable input;
  input.set_x(42);
  input.set_y(67);

  EXPECT_TRUE(ValueToBytes(input, kSimpleTable_X_42_Y_67));
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

TEST(XUnion, Empty) {
  SampleXUnion input;

  auto expected = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion discriminator + padding
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes + num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope data is absent
  };
  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnion, Int32) {
  SampleXUnion input;
  input.set_i(0xdeadbeef);

  auto expected = std::vector<uint8_t>{
      0xa5, 0x47, 0xdf, 0x29, 0x00, 0x00, 0x00, 0x00,  // xunion discriminator + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes + num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data is present
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,  // envelope content (0xdeadbeef) + padding
  };
  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnion, SimpleUnion) {
  SimpleUnion su;
  su.set_str("hello");

  SampleXUnion input;
  input.set_su(std::move(su));

  auto expected = std::vector<uint8_t>{
      0x53, 0x76, 0x31, 0x6f, 0x00, 0x00, 0x00, 0x00,  // xunion discriminator + padding
      0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes + num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data is present
      // secondary object 0
      0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // union discriminant + padding
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // string size
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // string pointer is present
      // secondary object 1
      'h', 'e', 'l', 'l', 'o', 0x00, 0x00, 0x00,  // string: "hello"
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnion, SimpleTable) {
  SimpleTable st;
  st.set_x(42);
  st.set_y(67);

  SampleXUnion input;
  input.set_st(std::move(st));

  auto expected = std::vector<uint8_t>{
      0xdd, 0x2c, 0x65, 0x30, 0x00, 0x00, 0x00, 0x00,  // xunion discriminator + padding
      0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes + num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data is present
                                                       // <table data follows>
  };
  expected.insert(expected.end(), kSimpleTable_X_42_Y_67.cbegin(), kSimpleTable_X_42_Y_67.cend());

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnion, SerializeAndDeserializeInt32) {
  SampleXUnion input;
  input.set_i(0xdeadbeef);

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnion>(input)));
}

TEST(XUnion, SerializeAndDeserializeSimpleUnion) {
  SimpleUnion su;
  su.set_str("hello");

  SampleXUnion input;
  input.set_su(std::move(su));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnion>(input)));
}

TEST(XUnion, SerializeAndDeserializeSimpleTable) {
  SimpleTable st;
  st.set_x(42);
  st.set_y(67);

  SampleXUnion input;
  input.set_st(std::move(st));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<SampleXUnion>(input)));
}

TEST(InlineXUnionInStruct, VerifyWireFormatXUnionIsPresent) {
  SampleXUnion xu;
  xu.set_i(0xdeadbeef);

  InlineXUnionInStruct input;
  input.before = "before";
  input.after = "after";
  input.xu = std::move(xu);

  auto expected = std::vector<uint8_t>{
      0x06,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "before" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence
      0xa5,
      0x47,
      0xdf,
      0x29,
      0x00,
      0x00,
      0x00,
      0x00,  // xunion discriminator + padding
      0x08,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // num bytes + num handles
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // envelope data is present
      0x05,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "after" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence
      // secondary object 1: "before"
      'b',
      'e',
      'f',
      'o',
      'r',
      'e',
      0x00,
      0x00,
      // secondary object 2: xunion content
      0xef,
      0xbe,
      0xad,
      0xde,
      0x00,
      0x00,
      0x00,
      0x00,  // xunion envelope content (0xdeadbeef) + padding
      // secondary object 3: "after"
      'a',
      'f',
      't',
      'e',
      'r',
      0x00,
      0x00,
      0x00,
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(OptionalXUnionInStruct, VerifyWireFormatXUnionIsAbsent) {
  OptionalXUnionInStruct input;
  input.before = "before";
  input.after = "after";

  auto expected = std::vector<uint8_t>{
      0x06,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "before" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence

      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // xunion discriminator + padding
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // num bytes + num handles
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // envelope data is absent

      0x05,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "after" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence

      // secondary object 1: "before"
      'b',
      'e',
      'f',
      'o',
      'r',
      'e',
      0x00,
      0x00,

      // secondary object 2: "after"
      'a',
      'f',
      't',
      'e',
      'r',
      0x00,
      0x00,
      0x00,
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(OptionalXUnionInStruct, VerifyWireFormatXUnionIsPresent) {
  auto xu = std::make_unique<SampleXUnion>();
  xu->set_i(0xdeadbeef);

  OptionalXUnionInStruct input;
  input.before = "before";
  input.after = "after";
  input.xu = std::move(xu);

  auto expected = std::vector<uint8_t>{
      0x06,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "before" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence

      0xa5,
      0x47,
      0xdf,
      0x29,
      0x00,
      0x00,
      0x00,
      0x00,  // xunion discriminator + padding
      0x08,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // num bytes + num handles
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // envelope data is present

      0x05,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,  // "after" length
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,  // "before" presence

      // secondary object 1: "before"
      'b',
      'e',
      'f',
      'o',
      'r',
      'e',
      0x00,
      0x00,

      // secondary object 2: xunion content
      0xef,
      0xbe,
      0xad,
      0xde,
      0x00,
      0x00,
      0x00,
      0x00,  // xunion envelope content (0xdeadbeef) + padding

      // secondary object 3: "after"
      'a',
      'f',
      't',
      'e',
      'r',
      0x00,
      0x00,
      0x00,
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
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

TEST(XUnionInTable, VerifyWireFormatXUnionIsAbsent) {
  XUnionInTable input;
  input.set_before("before");
  input.set_after("after");

  auto expected = std::vector<uint8_t>{
      // primary object
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // vector<envelope> element count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector<envelope> present

      // secondary object 1: vector data
      // vector[0]: envelope<string before>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      // vector[1]: envelope<SampleXUnion xu>
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion is absent
      // vector[2]: envelope<string after>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present

      // secondary object 2: "before" length + pointer
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "before" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" present

      // secondary object 3: "before"
      'b', 'e', 'f', 'o', 'r', 'e', 0x00, 0x00,  // "before"

      // secondary object 4: "after" length + pointer
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "after" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" present

      // secondary object 5: "before"
      'a', 'f', 't', 'e', 'r', 0x00, 0x00, 0x00,  // "after"
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnionInTable, VerifyWireFormatXUnionIsPresent) {
  SampleXUnion xu;
  xu.set_i(0xdeadbeef);

  XUnionInTable input;
  input.set_before("before");
  input.set_xu(std::move(xu));
  input.set_after("after");

  auto expected = std::vector<uint8_t>{
      // primary object
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // vector<envelope> element count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector<envelope> present

      // secondary object 1: vector data
      // vector[0]: envelope<string before>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      // vector[1]: envelope<SampleXUnion xu>
      0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // xunion is present
      // vector[2]: envelope<string after>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present

      // secondary object 2: "before" length + pointer
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "before" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" present

      // secondary object 3: "before"
      'b', 'e', 'f', 'o', 'r', 'e', 0x00, 0x00,  // "before"

      // secondary object 4: xunion
      0xa5, 0x47, 0xdf, 0x29, 0x00, 0x00, 0x00, 0x00,  // xunion discriminator + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes + num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data is present

      // secondary object 5: xunion content
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,  // 0xdeadbeef + padding

      // secondary object 6: "after" length + pointer
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "after" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" present

      // secondary object 7: "after"
      'a', 'f', 't', 'e', 'r', 0x00, 0x00, 0x00,  // "after"
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(XUnionInTable, SerializeAndDeserialize) {
  SampleXUnion xu;
  xu.set_i(0xdeadbeef);

  XUnionInTable input;
  input.set_xu(std::move(xu));

  EXPECT_TRUE(fidl::Equals(input, RoundTrip<XUnionInTable>(input)));
}

TEST(PrimitiveArrayInTable, VerifyWireFormatArrayIsPresent) {
  PrimitiveArrayInTable input;
  std::array<int32_t, 9> array = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  input.set_before("before");
  input.set_arr(array);
  input.set_after("after");

  auto expected = std::vector<uint8_t>{
      // primary object
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // vector<envelope> element count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector<envelope> present

      // secondary object 1: vector data
      // vector[0]: envelope<string before>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      // vector[1]: envelope<array<int32, 9> arr>
      0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // array is present
      // vector[2]: envelope<string after>
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // size + handle count
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present

      // secondary object 2: "before" length + pointer
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "before" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" present

      // secondary object 3: "before"
      'b', 'e', 'f', 'o', 'r', 'e', 0x00, 0x00,  // "before"

      // secondary object 4: array content
      0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
      0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x00,
      0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      // secondary object 5: "after" length + pointer
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // "after" length
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" present

      // secondary object 6: "after"
      'a', 'f', 't', 'e', 'r', 0x00, 0x00, 0x00,  // "after"
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
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
