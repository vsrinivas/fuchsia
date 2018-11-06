// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Check that we can decode things that we can encode.

#include <iostream>
#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/internal.h>
#include "gtest/gtest.h"
#include "lib/fidl/cpp/clone.h"

namespace fidl {
namespace test {
namespace misc {

namespace {

template <class Output, class Input>
Output RoundTrip(const Input& input) {
  const ::fidl::FidlField fake_input_interface_fields[] = {
      ::fidl::FidlField(Input::FidlType, 16),
  };
  const fidl_type_t fake_input_interface_struct{
      ::fidl::FidlCodedStruct(fake_input_interface_fields, 1,
                              16 + CodingTraits<Input>::encoded_size, "Input")};
  const ::fidl::FidlField fake_output_interface_fields[] = {
      ::fidl::FidlField(Output::FidlType, 16),
  };
  const fidl_type_t fake_output_interface_struct{::fidl::FidlCodedStruct(
      fake_output_interface_fields, 1, 16 + CodingTraits<Output>::encoded_size,
      "Output")};

  fidl::Encoder enc(0xfefefefe);
  auto ofs = enc.Alloc(CodingTraits<Input>::encoded_size);
  fidl::Clone(input).Encode(&enc, ofs);
  auto msg = enc.GetMessage();
  const char* err_msg = nullptr;
  EXPECT_EQ(ZX_OK, msg.Validate(&fake_input_interface_struct, &err_msg))
      << err_msg;
  EXPECT_EQ(ZX_OK, msg.Decode(&fake_output_interface_struct, &err_msg))
      << err_msg;
  fidl::Decoder dec(std::move(msg));
  Output output;
  Output::Decode(&dec, &output, ofs);
  return output;
}

TEST(SimpleStruct, SerializeAndDeserialize) {
  Int64Struct input{1};
  EXPECT_EQ(input, RoundTrip<Int64Struct>(input));
}

bool cmp_payload(const uint8_t* actual, size_t actual_size,
                 const uint8_t* expected, size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      std::cout << "element[" << i << "]: " <<
        "actual=" << +actual[i] << " " <<
        "expected=" << +expected[i] <<
        "\n";
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    std::cout << "element[...]: " <<
      "actual.size=" << +actual_size << " " <<
      "expected.size=" << +expected_size <<
      "\n";
  }
  return pass;
}

template <class Input>
bool ValueToBytes(const Input& input, const std::vector<uint8_t>& expected) {
  fidl::Encoder enc(0xfefefefe);
  auto offset = enc.Alloc(CodingTraits<SimpleTable>::encoded_size);
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  auto payload = msg.payload();
  return cmp_payload(
    reinterpret_cast<const uint8_t*>(payload.data()), payload.actual(),
    reinterpret_cast<const uint8_t*>(expected.data()), expected.size()
  );
}

TEST(SimpleTable, CheckEmptyTable) {
  SimpleTable input;

  auto expected = std::vector<uint8_t>{
    0, 0, 0, 0, 0, 0, 0, 0, // max ordinal
    255, 255, 255, 255, 255, 255, 255, 255, // alloc present
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(SimpleTable, CheckBytesWithXY) {
  SimpleTable input;
  input.set_x(42);
  input.set_y(67);

  auto expected = std::vector<uint8_t>{
    5, 0, 0, 0, 0, 0, 0, 0, // max ordinal
    255, 255, 255, 255, 255, 255, 255, 255, // alloc present
    8, 0, 0, 0, 0, 0, 0, 0, // envelope 1: num bytes / num handles
    255, 255, 255, 255, 255, 255, 255, 255, // alloc present
    0, 0, 0, 0, 0, 0, 0, 0, // envelope 2: num bytes / num handles
    0, 0, 0, 0, 0, 0, 0, 0, // no alloc
    0, 0, 0, 0, 0, 0, 0, 0, // envelope 3: num bytes / num handles
    0, 0, 0, 0, 0, 0, 0, 0, // no alloc
    0, 0, 0, 0, 0, 0, 0, 0, // envelope 4: num bytes / num handles
    0, 0, 0, 0, 0, 0, 0, 0, // no alloc
    8, 0, 0, 0, 0, 0, 0, 0, // envelope 5: num bytes / num handles
    255, 255, 255, 255, 255, 255, 255, 255, // alloc present
    42, 0, 0, 0, 0, 0, 0, 0, // field X
    67, 0, 0, 0, 0, 0, 0, 0, // field Y
  };

  EXPECT_TRUE(ValueToBytes(input, expected));
}

TEST(SimpleTable, SerializeAndDeserialize) {
  SimpleTable input;
  input.set_x(1);
  EXPECT_EQ(input, RoundTrip<SimpleTable>(input));
  // OlderSimpleTable is an abbreviated ('old') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, *RoundTrip<OlderSimpleTable>(input).x());
  // NewerSimpleTable is an extended ('new') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, *RoundTrip<NewerSimpleTable>(input).x());
}

TEST(SimpleTable, SerializeAndDeserializeWithReserved) {
  SimpleTable input;
  input.set_y(1);
  EXPECT_EQ(input, RoundTrip<SimpleTable>(input));
  // OlderSimpleTable is an abbreviated ('old') version of SimpleTable:
  // We should be able to decode to it (but since it doesn't have y,
  // we can't ask for that!)
  EXPECT_FALSE(RoundTrip<OlderSimpleTable>(input).has_x());
  // NewerSimpleTable is an extended ('new') version of SimpleTable:
  // We should be able to decode to it.
  EXPECT_EQ(1, *RoundTrip<NewerSimpleTable>(input).y());
}

}  // namespace

}  // namespace misc
}  // namespace test
}  // namespace fidl
