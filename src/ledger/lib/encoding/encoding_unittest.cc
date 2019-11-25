// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/encoding/encoding.h"

#include <fuchsia/ledger/testing/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/ledger/lib/convert/convert.h"

namespace ledger {
namespace {

using EncodingTest = ::testing::Test;

// Checks that encoding then decoding a fidl object of type T results in the same value.
// This should be true for any object that does not contain handles.
template <typename T>
void EncodeDecodeCycle(const T& val) {
  T input = fidl::Clone(val);

  fuchsia::mem::Buffer buffer;
  ASSERT_TRUE(EncodeToBuffer(&input, &buffer));

  T output;
  ASSERT_TRUE(DecodeFromBuffer(buffer, &output));

  EXPECT_TRUE(fidl::Equals(val, output));
}

TEST(EncodingTest, EmptyStruct) { EncodeDecodeCycle(fuchsia::ledger::testing::TestStruct()); }

TEST(EncodingTest, FullStructUnion) {
  // Test all three union values.
  EncodeDecodeCycle(
      fuchsia::ledger::testing::TestStruct()
          .set_some_string("str")
          .set_some_int(42)
          .set_some_float(4.2)
          .set_test_union(std::move(fuchsia::ledger::testing::TestUnion().set_message_1(
              std::move(fuchsia::ledger::testing::TestMessage1().set_bytes({0u, 1u, 2u}))))));

  std::array<fuchsia::ledger::testing::TestEnum, 3> a = {fuchsia::ledger::testing::TestEnum::A,
                                                         fuchsia::ledger::testing::TestEnum::C,
                                                         fuchsia::ledger::testing::TestEnum::B};
  auto message_2 = fuchsia::ledger::testing::TestMessage2();
  message_2.test_enum.swap(a);
  EncodeDecodeCycle(
      fuchsia::ledger::testing::TestStruct()
          .set_some_string("str")
          .set_some_int(42)
          .set_some_float(4.2)
          .set_test_union(std::move(
              fuchsia::ledger::testing::TestUnion().set_message_2(std::move(message_2)))));
  EncodeDecodeCycle(
      fuchsia::ledger::testing::TestStruct()
          .set_some_string("str")
          .set_some_int(42)
          .set_some_float(4.2)
          .set_test_union(
              std::move(fuchsia::ledger::testing::TestUnion().set_message_3("another string"))));
}

}  // namespace
}  // namespace ledger
