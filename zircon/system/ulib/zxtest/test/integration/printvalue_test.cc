// Copyright 2020 The Fuchsia Authors. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <arpa/inet.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <limits>
#include <string>

#include <fbl/string.h>
#include <zxtest/zxtest.h>

// Tests the formatted output value returned by the various PrintValue<> specializations.

// Test printing of primitive types.
TEST(PrintValueTest, PrimitiveTypes) {
  EXPECT_EQ("2147483647", zxtest::PrintValue(static_cast<int32_t>(2147483647)));
  EXPECT_EQ("4294967295", zxtest::PrintValue(static_cast<uint32_t>(4294967295u)));
  EXPECT_EQ("9223372036854775807", zxtest::PrintValue(static_cast<int64_t>(9223372036854775807)));
  EXPECT_EQ("18446744073709551615",
            zxtest::PrintValue(static_cast<uint64_t>(18446744073709551615u)));
  EXPECT_EQ("1024.000000", zxtest::PrintValue(1024.0f));
  EXPECT_EQ("-0.531250", zxtest::PrintValue(-0.53125));
}

// Test printing of string types.
TEST(PrintValueTest, StringTypes) {
  const char* foo = nullptr;
  EXPECT_EQ("<nullptr>", zxtest::PrintValue(foo));
  EXPECT_EQ("bar", zxtest::PrintValue("bar"));
  EXPECT_EQ("baz", zxtest::PrintValue(std::string("baz")));
  EXPECT_EQ("qux", zxtest::PrintValue(fbl::String("qux")));
}

#if !defined(__Fuchsia__)

// Test printing of status types.
TEST(PrintValueTest, StatusType) { EXPECT_EQ("0", zxtest::PrintStatus(ZX_OK)); }

#endif

// Test printing of tuple types.
TEST(PrintValueTest, TupleType) {
  const auto tuple = std::make_tuple(3, "rabbits", ZX_OK);
  EXPECT_EQ("{ 3, rabbits, 0 }", zxtest::PrintValue(tuple));
}

// Test that an unknown value type is printed as hex.
TEST(PrintValueTest, ValueAsHex) {
  struct Foo {
    uint32_t foo = 0;
  };
  Foo foo{htonl(0xDEADBEEF)};
  EXPECT_EQ("DE AD BE EF", zxtest::PrintValue(foo));
}
