// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/common/test_with_loop.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/array_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/struct_class.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

fxl::RefPtr<BaseType> GetCharType() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1,
                                       "char");
}

fxl::RefPtr<BaseType> GetInt32Type() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
}

fxl::RefPtr<ModifiedType> GetCharPointerType() {
  return fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                           LazySymbol(GetCharType()));
}

class FormatValueTest : public TestWithLoop {
 public:
  FormatValueTest()
      : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

  MockSymbolDataProvider* provider() { return provider_.get(); }

  // Synchronously calls FormatExprValue, returning the result.
  std::string SyncFormatValue(const ExprValue& value,
                              const FormatValueOptions& opts) {
    bool called = false;
    std::string output;

    auto formatter = fxl::MakeRefCounted<FormatValue>();

    formatter->AppendValue(provider_, value, opts);
    formatter->Complete([&called, &output](OutputBuffer out) {
      called = true;
      output = out.AsString();
      debug_ipc::MessageLoop::Current()->QuitNow();
    });

    if (called)
      return output;

    loop().Run();

    EXPECT_TRUE(called);
    return output;
  }

 private:
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

}  // namespace

TEST_F(FormatValueTest, Signed) {
  FormatValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "char"),
      {123});
  EXPECT_EQ("123", SyncFormatValue(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("-3872", SyncFormatValue(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("67305985", SyncFormatValue(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "long long"),
      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("-2", SyncFormatValue(val_int64, opts));

  // Force a 32-bit float to an int.
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatValueOptions::NumFormat::kSigned;
  EXPECT_EQ("16909060", SyncFormatValue(val_float, opts));
}

TEST_F(FormatValueTest, Unsigned) {
  FormatValueOptions opts;

  // 8-bit.
  ExprValue val_int8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "char"),
      {123});
  EXPECT_EQ("123", SyncFormatValue(val_int8, opts));

  // 16-bit.
  ExprValue val_int16(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "short"),
      {0xe0, 0xf0});
  EXPECT_EQ("61664", SyncFormatValue(val_int16, opts));

  // 32-bit.
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "int"),
      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ("67305985", SyncFormatValue(val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                    1, "long long"),
                      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ("18446744073709551614", SyncFormatValue(val_int64, opts));

  // Force a 32-bit float to an unsigned and a hex.
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatValueOptions::NumFormat::kUnsigned;
  EXPECT_EQ("16909060", SyncFormatValue(val_float, opts));
  opts.num_format = FormatValueOptions::NumFormat::kHex;
  EXPECT_EQ("0x1020304", SyncFormatValue(val_float, opts));
}

TEST_F(FormatValueTest, Bool) {
  FormatValueOptions opts;

  // 8-bit true.
  ExprValue val_true8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x01});
  EXPECT_EQ("true", SyncFormatValue(val_true8, opts));

  // 8-bit false.
  ExprValue val_false8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
      {0x00});
  EXPECT_EQ("false", SyncFormatValue(val_false8, opts));

  // 32-bit true.
  ExprValue val_false32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 4, "bool"),
      {0x00, 0x01, 0x00, 0x00});
  EXPECT_EQ("false", SyncFormatValue(val_false8, opts));
}

TEST_F(FormatValueTest, Char) {
  FormatValueOptions opts;

  // 8-bit char.
  ExprValue val_char8(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {'c'});
  EXPECT_EQ("'c'", SyncFormatValue(val_char8, opts));

  // Hex encoded 8-bit char.
  ExprValue val_char8_zero(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {0});
  EXPECT_EQ(R"('\x00')", SyncFormatValue(val_char8_zero, opts));

  // Backslash-escaped 8-bit char.
  ExprValue val_char8_quote(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
      {'\"'});
  EXPECT_EQ(R"('\"')", SyncFormatValue(val_char8_quote, opts));

  // 32-bit char (downcasted to 8 for printing).
  ExprValue val_char32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 4, "big"),
      {'A', 1, 2, 3});
  EXPECT_EQ("'A'", SyncFormatValue(val_char32, opts));

  // 32-bit int forced to char.
  opts.num_format = FormatValueOptions::NumFormat::kChar;
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t"),
      {'$', 0x01, 0x00, 0x00});
  EXPECT_EQ("'$'", SyncFormatValue(val_int32, opts));
}

TEST_F(FormatValueTest, Float) {
  FormatValueOptions opts;

  uint8_t buffer[8];

  // 32-bit float.
  float in_float = 3.14159;
  memcpy(buffer, &in_float, 4);
  ExprValue val_float(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
      std::vector<uint8_t>(&buffer[0], &buffer[4]));
  EXPECT_EQ("3.14159", SyncFormatValue(val_float, opts));

  // 64-bit float.
  double in_double = 9.875e+12;
  memcpy(buffer, &in_double, 8);
  ExprValue val_double(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double"),
      std::vector<uint8_t>(&buffer[0], &buffer[8]));
  EXPECT_EQ("9.875e+12", SyncFormatValue(val_double, opts));
}

TEST_F(FormatValueTest, Pointer) {
  FormatValueOptions opts;

  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "int");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                                    LazySymbol(base_type));

  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  ExprValue value(ptr_type, data);
  EXPECT_EQ("(int*) 0x807060504030201", SyncFormatValue(value, opts));

  // Test an invalid one with an incorrect size.
  data.resize(7);
  ExprValue bad_value(ptr_type, data);
  EXPECT_EQ(
      "<The value of type 'int*' is the incorrect size (expecting 8, got 7). "
      "Please file a bug.>",
      SyncFormatValue(bad_value, opts));
}

TEST_F(FormatValueTest, GoodStrings) {
  FormatValueOptions opts;

  constexpr uint64_t kAddress = 0x1100;
  std::vector<uint8_t> data = {'A',  'B',  'C', 'D',  'E', 'F',
                               '\n', 0x01, 'z', '\\', '"', 0};
  provider()->AddMemory(kAddress, data);

  // Little-endian version of the address.
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00};

  // This string is a char* and it should stop printing at the null terminator.
  const char kExpected[] = R"("ABCDEF\n\x01z\\\"")";
  auto ptr_type = GetCharPointerType();
  EXPECT_EQ(kExpected,
            SyncFormatValue(ExprValue(ptr_type, address_data), opts));

  // This string has the same data but is type encoded as char[12], it should
  // give the same output.
  auto array_type = fxl::MakeRefCounted<ArrayType>(GetCharType(), 12);
  EXPECT_EQ(kExpected, SyncFormatValue(ExprValue(array_type, data), opts));
}

TEST_F(FormatValueTest, BadStrings) {
  FormatValueOptions opts;
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00};

  // Should report invalid pointer.
  auto ptr_type = GetCharPointerType();
  ExprValue ptr_value(ptr_type, address_data);
  EXPECT_EQ("0x1100 <invalid pointer>", SyncFormatValue(ptr_value, opts));

  // A null string should print just the null and not say invalid.
  ExprValue null_value(ptr_type, std::vector<uint8_t>(sizeof(uint64_t)));
  EXPECT_EQ("0x0", SyncFormatValue(null_value, opts));
}

TEST_F(FormatValueTest, TruncatedString) {
  FormatValueOptions opts;

  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {'A', 'B', 'C', 'D', 'E', 'F'});

  // Little-endian version of kAddress.
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00};

  // This string doesn't end in a null terminator but rather invalid memory.
  // We should print as much as we have.
  auto ptr_type = GetCharPointerType();
  EXPECT_EQ(R"("ABCDEF")",
            SyncFormatValue(ExprValue(ptr_type, address_data), opts));

  // Should only report the first 4 chars with a ... indicator.
  opts.max_array_size = 4;  // Truncate past this value.
  EXPECT_EQ(R"("ABCD"...)",
            SyncFormatValue(ExprValue(ptr_type, address_data), opts));
}

TEST_F(FormatValueTest, EmptyAndBadArray) {
  FormatValueOptions opts;

  // Array of two int32's: [1, 2]
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);

  // Empty array with valid pointer.
  auto empty_array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 0);
  EXPECT_EQ(
      R"({})",
      SyncFormatValue(
          ExprValue(empty_array_type, std::vector<uint8_t>(), source), opts));

  // Array type declares a size but there's no data.
  auto array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 1);
  EXPECT_EQ(
      R"(<Array data (0 bytes) is too small for the expected size (4 bytes).>)",
      SyncFormatValue(ExprValue(array_type, std::vector<uint8_t>(), source),
                      opts));
}

TEST_F(FormatValueTest, TruncatedArray) {
  FormatValueOptions opts;
  opts.max_array_size = 2;

  // Array of two int32's: {1, 2}
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);
  std::vector<uint8_t> data = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};

  auto array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 2);

  // This array has exactly the max size, we shouldn't mark it as truncated.
  EXPECT_EQ(R"({1, 2})",
            SyncFormatValue(ExprValue(array_type, data, source), opts));

  // This one is truncated.
  opts.max_array_size = 1;
  EXPECT_EQ(R"({1, ...})",
            SyncFormatValue(ExprValue(array_type, data, source), opts));
}

TEST_F(FormatValueTest, Reference) {
  FormatValueOptions opts;

  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "int");
  auto ref_type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagReferenceType,
                                                    LazySymbol(base_type));
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {123, 0, 0, 0, 0, 0, 0, 0});

  // This data refers to the address above.
  std::vector<uint8_t> data = {0x00, 0x11, 0, 0, 0, 0, 0, 0};
  ExprValue value(ref_type, data);
  EXPECT_EQ("(int&) 0x1100 = 123", SyncFormatValue(value, opts));

  // Test an invalid one with an invalud address.
  std::vector<uint8_t> bad_data = {0x00, 0x22, 0, 0, 0, 0, 0, 0};
  value = ExprValue(ref_type, bad_data);
  EXPECT_EQ("(int&) 0x2200 = <Invalid pointer 0x2200>",
            SyncFormatValue(value, opts));
}

// TODO(brettw) check nested arrays.
// Pretty sure this ends up being wrong:
//   int a[2][2] = { ... }

TEST_F(FormatValueTest, Structs) {
  FormatValueOptions opts;
  opts.num_format = FormatValueOptions::NumFormat::kHex;

  auto int32_type = MakeInt32Type();

  // Struct with two values, and a pair of two of those structs.
  auto foo = MakeStruct2Members("Foo", int32_type, "a", int32_type, "b");
  auto pair = MakeStruct2Members("Pair", foo, "first", foo, "second");

  ExprValue pair_value(pair, {0x11, 0x00, 0x11, 0x00, 0x22, 0x00, 0x22, 0x00,
                              0x33, 0x00, 0x33, 0x00, 0x44, 0x00, 0x44, 0x00});
  EXPECT_EQ(
      "{first = {a = 0x110011, b = 0x220022}, second = {a = 0x330033, b = "
      "0x440044}}",
      SyncFormatValue(pair_value, opts));
}

}  // namespace zxdb
