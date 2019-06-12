// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/format_expr_value_options.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

namespace {

class FormatTest : public TestWithLoop {
 public:
  FormatTest()
      : eval_context_(fxl::MakeRefCounted<MockEvalContext>()),
        provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

  MockSymbolDataProvider* provider() { return provider_.get(); }

  std::unique_ptr<FormatNode> SyncFormat(const ExprValue& value,
                                         const FormatExprValueOptions& opts) {
    auto node = std::make_unique<FormatNode>(std::string(), value);
    FillFormatNodeDescription(node.get(), opts, eval_context_);
    return node;
  }

  // Returns "<type>, <description>" for the given formatting.
  // On error, returns "Err: <msg>".
  std::string SyncTypeDesc(const ExprValue& value,
                           const FormatExprValueOptions& opts) {
    auto node = SyncFormat(value, opts);
    if (node->err().has_error())
      return "Err: " + node->err().msg();
    return node->type() + ", " + node->description();
  }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

}  // namespace

TEST_F(FormatTest, Signed) {
  FormatExprValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "char"),
      {123});
  EXPECT_EQ("char, 123", SyncTypeDesc(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("short, -3872", SyncTypeDesc(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("int, 67305985", SyncTypeDesc(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "long long"),
      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("long long, -2", SyncTypeDesc(val_int64, opts));

  // Force a 32-bit float to an int.
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatExprValueOptions::NumFormat::kSigned;
  EXPECT_EQ("float, 16909060", SyncTypeDesc(val_float, opts));
}

TEST_F(FormatTest, Unsigned) {
  FormatExprValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "char"),
      {123});
  EXPECT_EQ("char, 123", SyncTypeDesc(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("short, 61664", SyncTypeDesc(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("int, 67305985", SyncTypeDesc(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                    1, "long long"),
                      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("long long, 18446744073709551614", SyncTypeDesc(val_int64, opts));

  // Force a 32-bit float to an unsigned and a hex.
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatExprValueOptions::NumFormat::kUnsigned;
  EXPECT_EQ("float, 16909060", SyncTypeDesc(val_float, opts));
  opts.num_format = FormatExprValueOptions::NumFormat::kHex;
  EXPECT_EQ("float, 0x1020304", SyncTypeDesc(val_float, opts));
}

TEST_F(FormatTest, Bool) {
  FormatExprValueOptions opts;

  // 8-bit true.
  ExprValue val_true8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x01});
  EXPECT_EQ("bool, true", SyncTypeDesc(val_true8, opts));

  // 8-bit false.
  ExprValue val_false8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x00});
  EXPECT_EQ("bool, false", SyncTypeDesc(val_false8, opts));

  // 32-bit true.
  ExprValue val_false32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 4, "bool"),
      {0x00, 0x01, 0x00, 0x00});
  EXPECT_EQ("bool, false", SyncTypeDesc(val_false8, opts));
}

TEST_F(FormatTest, Char) {
  FormatExprValueOptions opts;

  // 8-bit char.
  ExprValue val_char8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {'c'});
  EXPECT_EQ("char, 'c'", SyncTypeDesc(val_char8, opts));

  // Hex encoded 8-bit char.
  ExprValue val_char8_zero(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {0});
  EXPECT_EQ(R"(char, '\x00')", SyncTypeDesc(val_char8_zero, opts));

  // Backslash-escaped 8-bit char.
  ExprValue val_char8_quote(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {'\"'});
  EXPECT_EQ(R"(char, '\"')", SyncTypeDesc(val_char8_quote, opts));

  // 32-bit char (downcasted to 8 for printing).
  ExprValue val_char32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 4, "big"),
      {'A', 1, 2, 3});
  EXPECT_EQ("big, 'A'", SyncTypeDesc(val_char32, opts));

  // 32-bit int forced to char.
  opts.num_format = FormatExprValueOptions::NumFormat::kChar;
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t"),
      {'$', 0x01, 0x00, 0x00});
  EXPECT_EQ("int32_t, '$'", SyncTypeDesc(val_int32, opts));
}

TEST_F(FormatTest, Float) {
  FormatExprValueOptions opts;

  uint8_t buffer[8];

  // 32-bit float.
  float in_float = 3.14159;
  memcpy(buffer, &in_float, 4);
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      std::vector<uint8_t>(&buffer[0], &buffer[4]));
  EXPECT_EQ("float, 3.14159", SyncTypeDesc(val_float, opts));

  // 64-bit float.
  double in_double = 9.875e+12;
  memcpy(buffer, &in_double, 8);
  ExprValue val_double(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double"),
      std::vector<uint8_t>(&buffer[0], &buffer[8]));
  EXPECT_EQ("double, 9.875e+12", SyncTypeDesc(val_double, opts));
}

}  // namespace zxdb
