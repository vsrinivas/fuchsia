// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Wrapper around FormatExprValue that returns the output as a string.
std::string DoFormat(const ExprValue& value,
                     const FormatValueOptions& options) {
  OutputBuffer out;
  FormatExprValue(value, options, &out);
  return out.AsString();
}

}  // namespace

TEST(FormatValue, Signed) {
  FormatValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "char"),
      {123});
  EXPECT_EQ("123", DoFormat(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("-3872", DoFormat(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("67305985", DoFormat(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned,
                                                   8, "long long"),
                     {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("-2", DoFormat(val_int64, opts));

  // Force a 32-bit float to an int.
  ExprValue val_float(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat,
                                                   4, "float"),
                     {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatValueOptions::NumFormat::kSigned;
  EXPECT_EQ("16909060", DoFormat(val_float, opts));
}

TEST(FormatValue, Unsigned) {
  FormatValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "char"),
      {123});
  EXPECT_EQ("123", DoFormat(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("61664", DoFormat(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("67305985", DoFormat(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                   1, "long long"),
                     {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("18446744073709551614", DoFormat(val_int64, opts));

  // Force a 32-bit float to an unsigned and a hex.
  ExprValue val_float(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat,
                                                   4, "float"),
                     {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatValueOptions::NumFormat::kUnsigned;
  EXPECT_EQ("16909060", DoFormat(val_float, opts));
  opts.num_format = FormatValueOptions::NumFormat::kHex;
  EXPECT_EQ("0x1020304", DoFormat(val_float, opts));
}

TEST(FormatValue, Bool) {
  FormatValueOptions opts;

  // 8-bit true.
  ExprValue val_true8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x01});
  EXPECT_EQ("true", DoFormat(val_true8, opts));

  // 8-bit false.
  ExprValue val_false8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x00});
  EXPECT_EQ("false", DoFormat(val_false8, opts));

  // 32-bit true.
  ExprValue val_false32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 4, "bool"),
      {0x00, 0x01, 0x00, 0x00});
  EXPECT_EQ("false", DoFormat(val_false8, opts));
}

TEST(FormatValue, Char) {
  FormatValueOptions opts;

  // 8-bit char.
  ExprValue val_char8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {'c'});
  EXPECT_EQ("'c'", DoFormat(val_char8, opts));

  // 32-bit char (downcasted to 8 for printing).
  ExprValue val_char32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 4, "big"),
      {'A', 1, 2, 3});
  EXPECT_EQ("'A'", DoFormat(val_char32, opts));

  // 32-bit int forced to char.
  opts.num_format = FormatValueOptions::NumFormat::kChar;
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t"),
      {'$', 0x01, 0x00, 0x00});
  EXPECT_EQ("'$'", DoFormat(val_int32, opts));
}

TEST(FormatValue, Float) {
  FormatValueOptions opts;

  uint8_t buffer[8];

  // 32-bit float.
  float in_float = 3.14159;
  memcpy(buffer, &in_float, 4);
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      std::vector<uint8_t>(&buffer[0], &buffer[4]));
  EXPECT_EQ("3.14159", DoFormat(val_float, opts));

  // 64-bit float.
  double in_double = 9.875e+12;
  memcpy(buffer, &in_double, 8);
  ExprValue val_double(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double"),
      std::vector<uint8_t>(&buffer[0], &buffer[8]));
  EXPECT_EQ("9.875e+12", DoFormat(val_double, opts));
}

}  // namespace zxdb
