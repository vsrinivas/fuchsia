// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/array_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/enumeration.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/function_type.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "garnet/bin/zxdb/symbols/member_ptr.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/symbol_context.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/mock_format_value_process_context.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

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
  return fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                           LazySymbol(GetCharType()));
}

class FormatValueTest : public TestWithLoop {
 public:
  FormatValueTest()
      : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

  MockFormatValueProcessContext& process_context() { return process_context_; }
  MockSymbolDataProvider* provider() { return provider_.get(); }

  // Synchronously calls FormatExprValue, returning the result.
  std::string SyncFormatValue(const ExprValue& value,
                              const FormatExprValueOptions& opts) {
    bool called = false;
    std::string output;

    // Makes a heap-allocated copy of the ProcessContext for the formatter to
    // manage.
    auto formatter = fxl::MakeRefCounted<FormatValue>(
        std::make_unique<MockFormatValueProcessContext>(process_context_));

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
  MockFormatValueProcessContext process_context_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

}  // namespace

TEST_F(FormatValueTest, Void) {
  FormatExprValueOptions opts;

  // Bare void type (not valid in C++ but we can generate).
  auto void_type = fxl::MakeRefCounted<BaseType>();
  ExprValue val_void(void_type, {});
  EXPECT_EQ("void", SyncFormatValue(val_void, opts));

  // Void type with overridden name.
  auto named_void_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeNone, 0, "VOID");
  ExprValue val_named_void(named_void_type, {});
  EXPECT_EQ("VOID", SyncFormatValue(val_named_void, opts));

  // Void pointer encoded as a pointer to a "none" BaseType.
  auto void_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                         LazySymbol(void_type));
  ExprValue val_void_ptr(void_ptr_type, {8, 7, 6, 5, 4, 3, 2, 1});
  EXPECT_EQ("(void*) 0x102030405060708", SyncFormatValue(val_void_ptr, opts));

  // Void pointer encoded as a pointer to nothing.
  auto void_ptr_type2 =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol());
  ExprValue val_void_ptr2(void_ptr_type2, {8, 7, 6, 5, 4, 3, 2, 1});
  EXPECT_EQ("(void*) 0x102030405060708", SyncFormatValue(val_void_ptr2, opts));

  // Minimal verbosity with above values.
  opts.verbosity = FormatExprValueOptions::Verbosity::kMinimal;
  EXPECT_EQ("void", SyncFormatValue(val_void, opts));
  EXPECT_EQ("(*) 0x102030405060708", SyncFormatValue(val_void_ptr, opts));
  EXPECT_EQ("(*) 0x102030405060708", SyncFormatValue(val_void_ptr2, opts));
}

TEST_F(FormatValueTest, Signed) {
  FormatExprValueOptions opts;

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
  opts.num_format = FormatExprValueOptions::NumFormat::kSigned;
  EXPECT_EQ("16909060", SyncFormatValue(val_float, opts));
}

TEST_F(FormatValueTest, Unsigned) {
  FormatExprValueOptions opts;

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
  opts.num_format = FormatExprValueOptions::NumFormat::kUnsigned;
  EXPECT_EQ("16909060", SyncFormatValue(val_float, opts));
  opts.num_format = FormatExprValueOptions::NumFormat::kHex;
  EXPECT_EQ("0x1020304", SyncFormatValue(val_float, opts));
}

TEST_F(FormatValueTest, Bool) {
  FormatExprValueOptions opts;

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
  FormatExprValueOptions opts;

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
  opts.num_format = FormatExprValueOptions::NumFormat::kChar;
  ExprValue val_int32(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t"),
      {'$', 0x01, 0x00, 0x00});
  EXPECT_EQ("'$'", SyncFormatValue(val_int32, opts));
}

TEST_F(FormatValueTest, Float) {
  FormatExprValueOptions opts;

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
  FormatExprValueOptions opts;

  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "int");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                    LazySymbol(base_type));

  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  ExprValue value(ptr_type, data);

  // Print normally. Pointers always display their types.
  EXPECT_EQ("(int*) 0x807060504030201", SyncFormatValue(value, opts));

  // Print with type printing forced on. The result should be the same (the
  // type shouldn't be duplicated).
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(int*) 0x807060504030201", SyncFormatValue(value, opts));

  // Minimal formatting should omit the type name
  opts.verbosity = FormatExprValueOptions::Verbosity::kMinimal;
  EXPECT_EQ("(*) 0x807060504030201", SyncFormatValue(value, opts));

  // Test an invalid one with an incorrect size.
  data.resize(7);
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  ExprValue bad_value(ptr_type, data);
  EXPECT_EQ(
      "(int*) <The value of type 'int*' is the incorrect size (expecting 8, "
      "got 7). Please file a bug.>",
      SyncFormatValue(bad_value, opts));
}

TEST_F(FormatValueTest, GoodStrings) {
  FormatExprValueOptions opts;

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

  // Force type info.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ(std::string("(char*) ") + kExpected,
            SyncFormatValue(ExprValue(ptr_type, address_data), opts));

  // This string has the same data but is type encoded as char[12], it should
  // give the same output (except for type info).
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  auto array_type = fxl::MakeRefCounted<ArrayType>(GetCharType(), 12);
  EXPECT_EQ(kExpected, SyncFormatValue(ExprValue(array_type, data), opts));

  // Force type info.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ(std::string("(char[12]) ") + kExpected,
            SyncFormatValue(ExprValue(array_type, data), opts));
}

TEST_F(FormatValueTest, BadStrings) {
  FormatExprValueOptions opts;
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
  FormatExprValueOptions opts;

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
  FormatExprValueOptions opts;

  // Array of two int32's: [1, 2]
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);

  // Empty array with valid pointer.
  auto empty_array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 0);
  EXPECT_EQ(R"({})", SyncFormatValue(ExprValue(empty_array_type,
                                               std::vector<uint8_t>(), source),
                                     opts));

  // Array type declares a size but there's no data.
  auto array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 1);
  EXPECT_EQ(
      R"(<Array data (0 bytes) is too small for the expected size (4 bytes).>)",
      SyncFormatValue(ExprValue(array_type, std::vector<uint8_t>(), source),
                      opts));
}

TEST_F(FormatValueTest, TruncatedArray) {
  FormatExprValueOptions opts;
  opts.max_array_size = 2;

  // Array of two int32's: {1, 2}
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);
  std::vector<uint8_t> data = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};

  auto array_type = fxl::MakeRefCounted<ArrayType>(GetInt32Type(), 2);

  // This array has exactly the max size, we shouldn't mark it as truncated.
  EXPECT_EQ("{1, 2}",
            SyncFormatValue(ExprValue(array_type, data, source), opts));

  // Try one with type info forced on. Only the root array type should have the
  // type, not each individual element.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(int32_t[2]) {1, 2}",
            SyncFormatValue(ExprValue(array_type, data, source), opts));

  // This one is truncated.
  opts.max_array_size = 1;
  EXPECT_EQ("(int32_t[2]) {1, ...}",
            SyncFormatValue(ExprValue(array_type, data, source), opts));
}

TEST_F(FormatValueTest, Reference) {
  FormatExprValueOptions opts;

  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "int");
  auto ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType,
                                                    LazySymbol(base_type));
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {123, 0, 0, 0, 0, 0, 0, 0});

  // This data refers to the address above.
  std::vector<uint8_t> data = {0x00, 0x11, 0, 0, 0, 0, 0, 0};
  ExprValue value(ref_type, data);
  EXPECT_EQ("(int&) 0x1100 = 123", SyncFormatValue(value, opts));

  // Forcing type info on shouldn't duplicate the type.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(int&) 0x1100 = 123", SyncFormatValue(value, opts));

  // Force with minimal formatting (no addr ot type info).
  opts.verbosity = FormatExprValueOptions::Verbosity::kMinimal;
  EXPECT_EQ("123", SyncFormatValue(value, opts));

  // Test an invalid one with an invalid address.
  std::vector<uint8_t> bad_data = {0x00, 0x22, 0, 0, 0, 0, 0, 0};
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  value = ExprValue(ref_type, bad_data);
  EXPECT_EQ("(int&) 0x2200 = <Invalid pointer 0x2200>",
            SyncFormatValue(value, opts));

  // Test an rvalue reference. This is treated the same as a regular reference
  // from an interpretation and printing perspective.
  auto rvalue_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kRvalueReferenceType, LazySymbol(base_type));
  value = ExprValue(rvalue_ref_type, data);
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  EXPECT_EQ("(int&&) 0x1100 = 123", SyncFormatValue(value, opts));

  opts.verbosity = FormatExprValueOptions::Verbosity::kMinimal;
  EXPECT_EQ("123", SyncFormatValue(value, opts));
}

TEST_F(FormatValueTest, Structs) {
  FormatExprValueOptions opts;
  opts.num_format = FormatExprValueOptions::NumFormat::kHex;

  auto int32_type = MakeInt32Type();

  // Make an int reference. Reference type printing combined with struct type
  // printing can get complicated.
  auto int_ref = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType,
                                                   LazySymbol(int32_type));

  // The references point to this data.
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {0x12, 0, 0, 0});

  // Struct with two values, an int and a int&, and a pair of two of those
  // structs.
  auto foo = MakeCollectionType(DwarfTag::kStructureType, "Foo",
                                {{"a", int32_type}, {"b", int_ref}});
  auto pair = MakeCollectionType(DwarfTag::kStructureType, "Pair",
                                 {{"first", foo}, {"second", foo}});

  ExprValue pair_value(
      pair, {0x11, 0x00, 0x11, 0x00,                            // (int32) a
             0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // (int32&) b
             0x33, 0x00, 0x33, 0x00,                            // (int32) a
             0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // (int32&) b

  // The references when not printing all types are printed after the
  // struct member name.
  EXPECT_EQ(
      "{first = {a = 0x110011, b = (int32_t&) 0x1100 = 0x12}, "
      "second = {a = 0x330033, b = (int32_t&) 0x1100 = 0x12}}",
      SyncFormatValue(pair_value, opts));

  // Force type info. Now the reference types move before the member names like
  // the other types.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ(
      "(Pair) {(Foo) first = {(int32_t) a = 0x110011, (int32_t&) b = 0x1100 = "
      "0x12}, "
      "(Foo) second = {(int32_t) a = 0x330033, (int32_t&) b = 0x1100 = 0x12}}",
      SyncFormatValue(pair_value, opts));

  // Test an anonymous struct. Clang will generate structs with no names for
  // things like closures.
  auto anon_struct = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  auto anon_struct_ptr = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(anon_struct));
  ExprValue anon_value(anon_struct_ptr,
                       {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  EXPECT_EQ("((anon struct)*) 0x1100", SyncFormatValue(anon_value, opts));
}

// GDB and LLDB both print all members of a union and accept the possibility
// that sometimes one of them might be garbage, we do the same.
TEST_F(FormatValueTest, Union) {
  FormatExprValueOptions opts;

  // Define a union type with two int32 values.
  auto int32_type = MakeInt32Type();

  auto union_type = fxl::MakeRefCounted<Collection>(DwarfTag::kUnionType);
  union_type->set_byte_size(int32_type->byte_size());
  union_type->set_assigned_name("MyUnion");

  std::vector<LazySymbol> data_members;

  auto member_1 = fxl::MakeRefCounted<DataMember>();
  member_1->set_assigned_name("a");
  member_1->set_type(LazySymbol(int32_type));
  member_1->set_member_location(0);
  data_members.push_back(LazySymbol(member_1));

  auto member_2 = fxl::MakeRefCounted<DataMember>();
  member_2->set_assigned_name("b");
  member_2->set_type(LazySymbol(int32_type));
  member_2->set_member_location(0);
  data_members.push_back(LazySymbol(member_2));

  union_type->set_data_members(std::move(data_members));

  ExprValue value(union_type, {42, 0, 0, 0});
  EXPECT_EQ("{a = 42, b = 42}", SyncFormatValue(value, opts));
}

// Tests formatting when a class has derived base classes.
TEST_F(FormatValueTest, DerivedClasses) {
  auto int32_type = MakeInt32Type();
  auto base = MakeCollectionType(DwarfTag::kStructureType, "Base",
                                 {{"a", int32_type}, {"b", int32_type}});

  // This second base class is empty, it should be omitted from the output.
  auto empty_base = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  empty_base->set_assigned_name("EmptyBase");

  // Derived class, leave enough room to hold |Base|.
  auto derived = MakeCollectionTypeWithOffset(
      DwarfTag::kStructureType, "Derived", base->byte_size(),
      {{"c", int32_type}, {"d", int32_type}});

  auto inherited = fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base), 0);
  auto empty_inherited =
      fxl::MakeRefCounted<InheritedFrom>(LazySymbol(empty_base), 0);
  derived->set_inherited_from(
      {LazySymbol(inherited), LazySymbol(empty_inherited)});

  uint8_t kAValue = 1;
  uint8_t kBValue = 2;
  uint8_t kCValue = 3;
  uint8_t kDValue = 4;
  ExprValue value(derived, {kAValue, 0, 0, 0,    // (int32) Base.a
                            kBValue, 0, 0, 0,    // (int32) Base.b
                            kCValue, 0, 0, 0,    // (int32) Derived.c
                            kDValue, 0, 0, 0});  // (int32) Derived.d

  // Default formatting. Only the Base should be printed, EmptyBase should be
  // omitted because it has no data.
  FormatExprValueOptions opts;
  EXPECT_EQ("{Base = {a = 1, b = 2}, c = 3, d = 4}",
            SyncFormatValue(value, opts));

  // Force types on. The type of the base class should not be duplicated.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ(
      "(Derived) {Base = {(int32_t) a = 1, (int32_t) b = 2}, (int32_t) c = 3, "
      "(int32_t) d = 4}",
      SyncFormatValue(value, opts));
}

TEST_F(FormatValueTest, Enumeration) {
  // Unsigned 64-bit enum.
  Enumeration::Map unsigned_map;
  unsigned_map[0] = "kZero";
  unsigned_map[1] = "kOne";
  unsigned_map[std::numeric_limits<uint64_t>::max()] = "kMax";
  auto unsigned_enum = fxl::MakeRefCounted<Enumeration>(
      "UnsignedEnum", LazySymbol(), 8, false, unsigned_map);

  // Found value
  FormatExprValueOptions opts;
  EXPECT_EQ("kZero",
            SyncFormatValue(ExprValue(unsigned_enum, {0, 0, 0, 0, 0, 0, 0, 0}),
                            opts));
  EXPECT_EQ("kMax",
            SyncFormatValue(ExprValue(unsigned_enum, {0xff, 0xff, 0xff, 0xff,
                                                      0xff, 0xff, 0xff, 0xff}),
                            opts));

  // Found value forced to hex.
  FormatExprValueOptions hex_opts;
  hex_opts.num_format = FormatExprValueOptions::NumFormat::kHex;
  EXPECT_EQ("0xffffffffffffffff",
            SyncFormatValue(ExprValue(unsigned_enum, {0xff, 0xff, 0xff, 0xff,
                                                      0xff, 0xff, 0xff, 0xff}),
                            hex_opts));

  // Not found value.
  EXPECT_EQ("12",
            SyncFormatValue(ExprValue(unsigned_enum, {12, 0, 0, 0, 0, 0, 0, 0}),
                            opts));

  // Signed 32-bit enum.
  Enumeration::Map signed_map;
  signed_map[0] = "kZero";
  signed_map[static_cast<uint64_t>(-5)] = "kMinusFive";
  signed_map[static_cast<uint64_t>(std::numeric_limits<int32_t>::max())] =
      "kMax";
  auto signed_enum = fxl::MakeRefCounted<Enumeration>(
      "SignedEnum", LazySymbol(), 4, true, signed_map);

  // Found values.
  EXPECT_EQ("kZero",
            SyncFormatValue(ExprValue(signed_enum, {0, 0, 0, 0}), opts));
  EXPECT_EQ(
      "kMinusFive",
      SyncFormatValue(ExprValue(signed_enum, {0xfb, 0xff, 0xff, 0xff}), opts));

  // Not-found value.
  EXPECT_EQ("-4", SyncFormatValue(
                      ExprValue(signed_enum, {0xfc, 0xff, 0xff, 0xff}), opts));

  // Not-found signed value printed as hex should be unsigned.
  EXPECT_EQ("0xffffffff",
            SyncFormatValue(ExprValue(signed_enum, {0xff, 0xff, 0xff, 0xff}),
                            hex_opts));

  // Force type info.
  FormatExprValueOptions type_opts;
  type_opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(SignedEnum) kZero",
            SyncFormatValue(ExprValue(signed_enum, {0, 0, 0, 0}), type_opts));
  EXPECT_EQ("(SignedEnum) -4",
            SyncFormatValue(ExprValue(signed_enum, {0xfc, 0xff, 0xff, 0xff}),
                            type_opts));
}

TEST_F(FormatValueTest, FunctionPtr) {
  // This is a function type. There isn't a corresponding C/C++ type for a
  // function type (without a pointer modifier) but we define it anyway in
  // case it comes up (possibly another language).
  auto func_type = fxl::MakeRefCounted<FunctionType>(LazySymbol(),
                                                     std::vector<LazySymbol>());

  // This type is "void (*)()"
  auto func_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                         LazySymbol(func_type));

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("MyFunc");

  // Map the address to point to the function.
  constexpr uint64_t kAddress = 0x1234;
  process_context().AddResult(
      kAddress, Location(kAddress, FileLine("file.cc", 21), 0, symbol_context,
                         LazySymbol(function)));

  // Function.
  FormatExprValueOptions type_opts;
  type_opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  ExprValue null_func(func_type, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("(void()) 0x0", SyncFormatValue(null_func, type_opts));

  // Null function pointer.
  FormatExprValueOptions opts;
  ExprValue null_ptr(func_ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("0x0", SyncFormatValue(null_ptr, opts));

  // Null function pointer with forced type info,
  EXPECT_EQ("(void (*)()) 0x0", SyncFormatValue(null_ptr, type_opts));

  // Function pointer to unknown memory is printed in hex by default.
  EXPECT_EQ("0x5",
            SyncFormatValue(ExprValue(func_ptr_type, {5, 0, 0, 0, 0, 0, 0, 0}),
                            opts));

  // Found symbol (matching kAddress) should be printed.
  ExprValue good_ptr(func_ptr_type, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("&MyFunc", SyncFormatValue(good_ptr, opts));

  // Force output as hex even when the function is matched.
  FormatExprValueOptions hex_opts;
  hex_opts.num_format = FormatExprValueOptions::NumFormat::kHex;
  EXPECT_EQ("0x1234", SyncFormatValue(good_ptr, hex_opts));

  // Member function pointer. The type naming of function pointers is tested by
  // the MemberPtr class, and otherwise the code paths are the same, so here
  // we only need to verify things are hooked up.
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  containing->set_assigned_name("MyClass");

  auto member_func = fxl::MakeRefCounted<MemberPtr>(LazySymbol(containing),
                                                    LazySymbol(func_type));
  ExprValue null_member_func_ptr(member_func, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("0x0", SyncFormatValue(null_member_func_ptr, opts));
  EXPECT_EQ("(void (MyClass::*)()) 0x0",
            SyncFormatValue(null_member_func_ptr, type_opts));

  // Member function to a known symbol. This doesn't resolve to something that
  // looks like a class member, but that's OK, wherever the address points to
  // is what we print.
  ExprValue good_member_func_ptr(member_func, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("(void (MyClass::*)()) &MyFunc",
            SyncFormatValue(good_member_func_ptr, type_opts));
}

// This tests pointers to member data. Pointers to member functions were tested
// by the FunctionPtr test.
TEST_F(FormatValueTest, MemberPtr) {
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  containing->set_assigned_name("MyClass");

  auto int32_type = GetInt32Type();
  auto member_int32 = fxl::MakeRefCounted<MemberPtr>(LazySymbol(containing),
                                                     LazySymbol(int32_type));

  // Null pointer.
  FormatExprValueOptions opts;
  ExprValue null_member_ptr(member_int32, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("(int32_t MyClass::*) 0x0", SyncFormatValue(null_member_ptr, opts));

  // Regular pointer with types forced on.
  FormatExprValueOptions type_opts;
  type_opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  ExprValue good_member_ptr(member_int32, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ("(int32_t MyClass::*) 0x1234",
            SyncFormatValue(good_member_ptr, type_opts));
}

// Tests printing nullptr_t which is defined as
// "typedef decltype(nullptr) nullptr_t;".
TEST_F(FormatValueTest, NullptrT) {
  // Clang and GCC currently defined "decltype(nullptr)" as an "unspecified"
  // type. Our decoder will force these to be the size of a pointer (the
  // symbols don't seem to define a size).
  auto underlying_type = fxl::MakeRefCounted<Type>(DwarfTag::kUnspecifiedType);
  underlying_type->set_assigned_name("decltype(nullptr_t)");
  underlying_type->set_byte_size(8);

  // The nullptr_t is defined as a typedef for the above.
  auto nullptr_t_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kTypedef, LazySymbol(underlying_type));
  nullptr_t_type->set_assigned_name("nullptr_t");

  ExprValue null_value(nullptr_t_type, {0, 0, 0, 0, 0, 0, 0, 0});

  FormatExprValueOptions opts;
  EXPECT_EQ("0x0", SyncFormatValue(null_value, opts));

  // Now with type printing.
  opts.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(nullptr_t) 0x0", SyncFormatValue(null_value, opts));
}

}  // namespace zxdb
