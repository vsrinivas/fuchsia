// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/format_test_support.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/member_ptr.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

namespace {

class FormatTest : public TestWithLoop {
 public:
  FormatTest() : eval_context_(fxl::MakeRefCounted<MockEvalContext>()) {}

  fxl::RefPtr<MockEvalContext> eval_context() const { return eval_context_; }
  MockSymbolDataProvider* provider() { return eval_context_->data_provider(); }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
};

}  // namespace

TEST_F(FormatTest, Void) {
  FormatOptions opts;

  // "None" base type is used in some cases as an encoding for void.
  ExprValue val_void(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeNone, 0, "myvoid"), {});
  EXPECT_EQ(" = myvoid, void\n", GetDebugTreeForValue(eval_context(), val_void, opts));
}

TEST_F(FormatTest, Signed) {
  FormatOptions opts;

  // 8-bit.
  ExprValue val_int8(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "char"), {123});
  EXPECT_EQ(" = char, 123\n", GetDebugTreeForValue(eval_context(), val_int8, opts));

  // 16-bit.
  ExprValue val_int16(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "short"),
                      {0xe0, 0xf0});
  EXPECT_EQ(" = short, -3872\n", GetDebugTreeForValue(eval_context(), val_int16, opts));

  // 32-bit.
  ExprValue val_int32(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int"),
                      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ(" = int, 67305985\n", GetDebugTreeForValue(eval_context(), val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "long long"),
                      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ(" = long long, -2\n", GetDebugTreeForValue(eval_context(), val_int64, opts));

  // Force a 32-bit float to an int.
  ExprValue val_float(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
                      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatOptions::NumFormat::kSigned;
  EXPECT_EQ(" = float, 16909060\n", GetDebugTreeForValue(eval_context(), val_float, opts));
}

TEST_F(FormatTest, Unsigned) {
  FormatOptions opts;

  // 8-bit.
  ExprValue val_int8(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "char"), {123});
  EXPECT_EQ(" = char, 123\n", GetDebugTreeForValue(eval_context(), val_int8, opts));

  // 16-bit.
  ExprValue val_int16(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "short"),
                      {0xe0, 0xf0});
  EXPECT_EQ(" = short, 61664\n", GetDebugTreeForValue(eval_context(), val_int16, opts));

  // 32-bit.
  ExprValue val_int32(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "int"),
                      {0x01, 0x02, 0x03, 0x04});
  EXPECT_EQ(" = int, 67305985\n", GetDebugTreeForValue(eval_context(), val_int32, opts));

  // 64-bit.
  ExprValue val_int64(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "long long"),
                      {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  EXPECT_EQ(" = long long, 18446744073709551614\n",
            GetDebugTreeForValue(eval_context(), val_int64, opts));

  // 128 bit (this always get output as hex today).
  ExprValue val_int128(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 16, "insanely long"),
      {0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,    // Low 64 bits.
       0x04, 0x03, 0x02, 0x01, 0xef, 0xbe, 0xad, 0xde});  // High 64 bits.
  EXPECT_EQ(" = insanely long, 0xdeadbeef010203041112131415161718\n",
            GetDebugTreeForValue(eval_context(), val_int128, opts));

  // Force a 32-bit float to different bases.
  ExprValue val_float(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
                      {0x04, 0x03, 0x02, 0x01});
  opts.num_format = FormatOptions::NumFormat::kUnsigned;
  EXPECT_EQ(" = float, 16909060\n", GetDebugTreeForValue(eval_context(), val_float, opts));
  opts.num_format = FormatOptions::NumFormat::kHex;
  EXPECT_EQ(" = float, 0x1020304\n", GetDebugTreeForValue(eval_context(), val_float, opts));
  opts.num_format = FormatOptions::NumFormat::kBin;
  EXPECT_EQ(" = float, 0b1'00000010'00000011'00000100\n",
            GetDebugTreeForValue(eval_context(), val_float, opts));

  // Zero-pad.
  opts.zero_pad_hex_bin = true;
  EXPECT_EQ(" = float, 0b00000001'00000010'00000011'00000100\n",
            GetDebugTreeForValue(eval_context(), val_float, opts));
  opts.num_format = FormatOptions::NumFormat::kHex;
  EXPECT_EQ(" = float, 0x01020304\n", GetDebugTreeForValue(eval_context(), val_float, opts));
  EXPECT_EQ(" = char, 0x7b\n", GetDebugTreeForValue(eval_context(), val_int8, opts));
  ExprValue val_int8_short(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "char"),
                           {1});
  EXPECT_EQ(" = char, 0x01\n", GetDebugTreeForValue(eval_context(), val_int8_short, opts));
  ExprValue val_int64_short(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t"),
      {1, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = uint64_t, 0x0000000000000001\n",
            GetDebugTreeForValue(eval_context(), val_int64_short, opts));
}

TEST_F(FormatTest, Bool) {
  FormatOptions opts;

  // 8-bit true.
  ExprValue val_true8(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"), {0x01});
  EXPECT_EQ(" = bool, true\n", GetDebugTreeForValue(eval_context(), val_true8, opts));

  // 8-bit false.
  ExprValue val_false8(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool"),
                       {0x00});
  EXPECT_EQ(" = bool, false\n", GetDebugTreeForValue(eval_context(), val_false8, opts));

  // 32-bit true.
  ExprValue val_false32(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 4, "bool"),
                        {0x00, 0x01, 0x00, 0x00});
  EXPECT_EQ(" = bool, false\n", GetDebugTreeForValue(eval_context(), val_false8, opts));
}

TEST_F(FormatTest, Char) {
  FormatOptions opts;

  // 8-bit char.
  ExprValue val_char8(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"),
                      {'c'});
  EXPECT_EQ(" = char, 'c'\n", GetDebugTreeForValue(eval_context(), val_char8, opts));

  // Hex encoded 8-bit char.
  ExprValue val_char8_zero(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"), {0});
  EXPECT_EQ(" = char, '\\x00'\n", GetDebugTreeForValue(eval_context(), val_char8_zero, opts));

  // Backslash-escaped 8-bit char.
  ExprValue val_char8_quote(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 1, "char"), {'\"'});
  EXPECT_EQ(" = char, '\\\"'\n", GetDebugTreeForValue(eval_context(), val_char8_quote, opts));

  // 32-bit char (downcasted to 8 for printing).
  ExprValue val_char32(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 4, "big"),
                       {'A', 1, 2, 3});
  EXPECT_EQ(" = big, 'A'\n", GetDebugTreeForValue(eval_context(), val_char32, opts));

  // 32-bit int forced to char.
  opts.num_format = FormatOptions::NumFormat::kChar;
  ExprValue val_int32(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t"),
                      {'$', 0x01, 0x00, 0x00});
  EXPECT_EQ(" = int32_t, '$'\n", GetDebugTreeForValue(eval_context(), val_int32, opts));
}

TEST_F(FormatTest, Float) {
  FormatOptions opts;

  uint8_t buffer[8];

  // 32-bit float.
  float in_float = 3.14159;
  memcpy(buffer, &in_float, 4);
  ExprValue val_float(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float"),
                      std::vector<uint8_t>(buffer, &buffer[4]));
  EXPECT_EQ(" = float, 3.14159\n", GetDebugTreeForValue(eval_context(), val_float, opts));

  // 64-bit float.
  double in_double = 9.875e+12;
  memcpy(buffer, &in_double, 8);
  ExprValue val_double(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double"),
                       std::vector<uint8_t>(buffer, &buffer[8]));
  EXPECT_EQ(" = double, 9.875e+12\n", GetDebugTreeForValue(eval_context(), val_double, opts));
}

TEST_F(FormatTest, Structs) {
  FormatOptions opts;
  opts.num_format = FormatOptions::NumFormat::kHex;

  auto int32_type = MakeInt32Type();

  // Make an int reference. Reference type printing combined with struct type printing can get
  // complicated.
  auto int_ref = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, int32_type);

  // The references point to this data.
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {0x12, 0, 0, 0});

  // Struct with two values, an int and a int&, and a pair of two of those structs.
  auto foo =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", int32_type}, {"b", int_ref}});
  auto pair =
      MakeCollectionType(DwarfTag::kStructureType, "Pair", {{"first", foo}, {"second", foo}});

  ExprValue pair_value(pair, {0x11, 0x00, 0x11, 0x00,                            // (int32) a
                              0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // (int32&) b
                              0x33, 0x00, 0x33, 0x00,                            // (int32) a
                              0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // (int32&) b

  // The references when not printing all types are printed after the struct member name.
  EXPECT_EQ(
      " = Pair, \n"
      "  first = Foo, \n"
      "    a = int32_t, 0x110011\n"
      "    b = int32_t&, 0x1100\n"
      "       = int32_t, 0x12\n"
      "  second = Foo, \n"
      "    a = int32_t, 0x330033\n"
      "    b = int32_t&, 0x1100\n"
      "       = int32_t, 0x12\n",
      GetDebugTreeForValue(eval_context(), pair_value, opts));
}

TEST_F(FormatTest, StructStatic) {
  // Currently we don't output static struct members so this test validates that this case is
  // handled as expected. This may be changed in the future if we change the policy on statics.
  auto extern_member = fxl::MakeRefCounted<DataMember>("static_one", MakeInt32Type(), 0);
  extern_member->set_is_external(true);
  auto regular_member = fxl::MakeRefCounted<DataMember>("regular_one", MakeInt32Type(), 0);

  auto collection = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  collection->set_assigned_name("Collection");
  collection->set_data_members({LazySymbol(extern_member), LazySymbol(regular_member)});

  // The collection is just the single non-external int32.
  constexpr uint8_t kRegularValue = 42;
  ExprValue value(collection, {kRegularValue, 0, 0, 0});

  FormatOptions opts;
  EXPECT_EQ(
      " = Collection, \n"
      "  regular_one = int32_t, 42\n",
      GetDebugTreeForValue(eval_context(), value, opts));
}

TEST_F(FormatTest, Struct_Anon) {
  // Test an anonymous struct. Clang will generate structs with no names for things like closures.
  // This struct has no members.
  auto anon_struct = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  auto anon_struct_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, anon_struct);
  ExprValue anon_value(anon_struct_ptr, {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  EXPECT_EQ(
      " = (anon struct)*, 0x1100\n"
      "  * = (anon struct), \n",
      GetDebugTreeForValue(eval_context(), anon_value, FormatOptions()));
}

// Structure members can be marked as "artificial" by the compiler. We shouldn't print these.
TEST_F(FormatTest, Struct_Artificial) {
  auto int32_type = MakeInt32Type();
  auto foo_type = MakeCollectionType(DwarfTag::kStructureType, "Foo",
                                     {{"normal", int32_type}, {"artificial", int32_type}});

  // Print without anything being marked artificial.
  ExprValue value(foo_type, {1, 0, 0, 0, 2, 0, 0, 0});
  EXPECT_EQ(
      " = Foo, \n"
      "  normal = int32_t, 1\n"
      "  artificial = int32_t, 2\n",
      GetDebugTreeForValue(eval_context(), value, FormatOptions()));

  // Mark second one as artificial.
  DataMember* artificial_member =
      const_cast<DataMember*>(foo_type->data_members()[1].Get()->As<DataMember>());
  artificial_member->set_artificial(true);

  EXPECT_EQ(
      " = Foo, \n"
      "  normal = int32_t, 1\n",
      GetDebugTreeForValue(eval_context(), value, FormatOptions()));
}

// GDB and LLDB both print all members of a union and accept the possibility that sometimes one of
// them might be garbage, we do the same.
TEST_F(FormatTest, Union) {
  FormatOptions opts;

  // Define a union type with two int32 values.
  auto int32_type = MakeInt32Type();

  auto union_type = fxl::MakeRefCounted<Collection>(DwarfTag::kUnionType, "MyUnion");
  union_type->set_byte_size(int32_type->byte_size());

  std::vector<LazySymbol> data_members;

  auto member_1 = fxl::MakeRefCounted<DataMember>();
  member_1->set_assigned_name("a");
  member_1->set_type(int32_type);
  member_1->set_member_location(0);
  data_members.push_back(member_1);

  auto member_2 = fxl::MakeRefCounted<DataMember>();
  member_2->set_assigned_name("b");
  member_2->set_type(int32_type);
  member_2->set_member_location(0);
  data_members.push_back(member_2);

  union_type->set_data_members(std::move(data_members));

  ExprValue value(union_type, {42, 0, 0, 0});
  EXPECT_EQ(
      " = MyUnion, \n"
      "  a = int32_t, 42\n"
      "  b = int32_t, 42\n",
      GetDebugTreeForValue(eval_context(), value, opts));
}

// Tests formatting when a class has derived base classes.
TEST_F(FormatTest, DerivedClasses) {
  auto int32_type = MakeInt32Type();
  auto base =
      MakeCollectionType(DwarfTag::kStructureType, "Base", {{"a", int32_type}, {"b", int32_type}});

  // This second base class is empty, it should be omitted from the output.
  auto empty_base = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType, "EmptyBase");

  // Derived class, leave enough room to hold |Base|.
  auto derived =
      MakeCollectionTypeWithOffset(DwarfTag::kStructureType, "Derived", base->byte_size(),
                                   {{"c", int32_type}, {"d", int32_type}});

  auto inherited = fxl::MakeRefCounted<InheritedFrom>(base, 0);
  auto empty_inherited = fxl::MakeRefCounted<InheritedFrom>(empty_base, 0);
  derived->set_inherited_from({LazySymbol(inherited), LazySymbol(empty_inherited)});

  uint8_t kAValue = 1;
  uint8_t kBValue = 2;
  uint8_t kCValue = 3;
  uint8_t kDValue = 4;
  ExprValue value(derived, {kAValue, 0, 0, 0,    // (int32) Base.a
                            kBValue, 0, 0, 0,    // (int32) Base.b
                            kCValue, 0, 0, 0,    // (int32) Derived.c
                            kDValue, 0, 0, 0});  // (int32) Derived.d

  // Only the Base should be printed, EmptyBase should be omitted because it has no data.
  FormatOptions opts;
  EXPECT_EQ(
      " = Derived, \n"
      "  Base = Base, \n"
      "    a = int32_t, 1\n"
      "    b = int32_t, 2\n"
      "  c = int32_t, 3\n"
      "  d = int32_t, 4\n",
      GetDebugTreeForValue(eval_context(), value, opts));
}

TEST_F(FormatTest, Pointer) {
  FormatOptions opts;

  auto base_type = MakeInt32Type();
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, base_type);

  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  ExprValue value(ptr_type, data);

  // The pointer points to invalid memory.
  EXPECT_EQ(
      " = int32_t*, 0x807060504030201\n"
      "  * = Err: Invalid pointer 0x807060504030201\n",
      GetDebugTreeForValue(eval_context(), value, opts));

  // Provide some memory backing for the request.
  constexpr uint64_t kAddress = 0x807060504030201;
  provider()->AddMemory(kAddress, {123, 0, 0, 0});
  EXPECT_EQ(
      " = int32_t*, 0x807060504030201\n"
      "  * = int32_t, 123\n",
      GetDebugTreeForValue(eval_context(), value, opts));

  // Test an invalid one with an incorrect size.
  data.resize(7);
  ExprValue bad_value(ptr_type, data);
  EXPECT_EQ(
      " = Err: The value of type 'int32_t*' is the incorrect size (expecting "
      "8, got 7). Please file a bug.\n",
      GetDebugTreeForValue(eval_context(), bad_value, opts));
}

TEST_F(FormatTest, Reference) {
  FormatOptions opts;

  auto base_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "int");
  auto ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, base_type);
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {123, 0, 0, 0, 0, 0, 0, 0});

  // This data refers to the address above.
  std::vector<uint8_t> data = {0x00, 0x11, 0, 0, 0, 0, 0, 0};
  ExprValue value(ref_type, data);
  EXPECT_EQ(
      " = int&, 0x1100\n"
      "   = int, 123\n",
      GetDebugTreeForValue(eval_context(), value, opts));

  // Test an invalid one with an invalid address.
  std::vector<uint8_t> bad_data = {0x00, 0x22, 0, 0, 0, 0, 0, 0};
  value = ExprValue(ref_type, bad_data);
  EXPECT_EQ(
      " = int&, 0x2200\n"
      "   = Err: Invalid pointer 0x2200\n",
      GetDebugTreeForValue(eval_context(), value, opts));

  // Test an rvalue reference. This is treated the same as a regular reference from an
  // interpretation and printing perspective.
  auto rvalue_ref_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kRvalueReferenceType, base_type);
  value = ExprValue(rvalue_ref_type, data);
  EXPECT_EQ(
      " = int&&, 0x1100\n"
      "   = int, 123\n",
      GetDebugTreeForValue(eval_context(), value, opts));
}

TEST_F(FormatTest, GoodStrings) {
  FormatOptions opts;

  constexpr uint64_t kAddress = 0x1100;
  std::vector<uint8_t> data = {'A', 'B', 'C', 'D', 'E', 'F', '\n', 0x01, 'z', '\\', '"', 0};
  provider()->AddMemory(kAddress, data);

  // The expected children of the string, not counting the null terminator.
  std::string expected_members_no_null =
      R"(  [0] = char, 'A'
  [1] = char, 'B'
  [2] = char, 'C'
  [3] = char, 'D'
  [4] = char, 'E'
  [5] = char, 'F'
  [6] = char, '\n'
  [7] = char, '\x01'
  [8] = char, 'z'
  [9] = char, '\\'
  [10] = char, '\"'
)";

  // The expected children of the string, including the null terminator.
  std::string expected_members_with_null = expected_members_no_null + "  [11] = char, '\\x00'\n";

  std::string expected_desc_string = R"("ABCDEF\n\x01z\\\"")";

  // Little-endian version of the address.
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // This string is a char*. It should show the string contents (stopping before the null
  // terminator). Note that Visual Studio shows the same thing in the description that we do, but
  // the children is like a normal pointer so there is only see the first character.
  auto ptr_type = MakeCharPointerType();
  EXPECT_EQ(" = char*, " + expected_desc_string + "\n" + expected_members_no_null,
            GetDebugTreeForValue(eval_context(), ExprValue(ptr_type, address_data), opts));

  // This string has the same data but is type encoded as char[12], it should give the same output
  // (except for type info).
  auto array_type = fxl::MakeRefCounted<ArrayType>(MakeSignedChar8Type(), 12);
  EXPECT_EQ(" = char[12], " + expected_desc_string + "\n" + expected_members_with_null,
            GetDebugTreeForValue(eval_context(), ExprValue(array_type, data), opts));

  // This type is a "const array of const char". I don't know how to type this in C (most related
  // things end up as "const pointer to const char") and the type name looks wrong but GCC will
  // generate this for the type of compiler-generated variables like __func__.
  auto char_type = MakeSignedChar8Type();
  auto const_char = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, char_type);
  auto array_const_char = fxl::MakeRefCounted<ArrayType>(const_char, 12);
  auto const_array_const_char =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, array_const_char);
  EXPECT_EQ(std::string(" = const const char[12], ") + expected_desc_string + "\n" +
                expected_members_with_null,
            GetDebugTreeForValue(eval_context(), ExprValue(const_array_const_char, data), opts));
}

TEST_F(FormatTest, BadStrings) {
  FormatOptions opts;
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // Should report invalid pointer.
  auto ptr_type = MakeCharPointerType();
  ExprValue ptr_value(ptr_type, address_data);
  EXPECT_EQ(" = Err: 0x1100 invalid pointer\n",
            GetDebugTreeForValue(eval_context(), ptr_value, opts));

  // A null string should print just the null and not say invalid.
  ExprValue null_value(ptr_type, std::vector<uint8_t>(sizeof(uint64_t)));
  EXPECT_EQ(" = char*, 0x0\n", GetDebugTreeForValue(eval_context(), null_value, opts));
}

TEST_F(FormatTest, TruncatedString) {
  FormatOptions opts;

  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {'A', 'B', 'C', 'D', 'E', 'F'});

  // Little-endian version of kAddress.
  std::vector<uint8_t> address_data = {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // This string doesn't end in a null terminator but rather invalid memory. We should print as much
  // as we have.
  auto ptr_type = MakeCharPointerType();
  EXPECT_EQ(
      " = char*, \"ABCDEF\"\n"
      "  [0] = char, 'A'\n"
      "  [1] = char, 'B'\n"
      "  [2] = char, 'C'\n"
      "  [3] = char, 'D'\n"
      "  [4] = char, 'E'\n"
      "  [5] = char, 'F'\n",
      GetDebugTreeForValue(eval_context(), ExprValue(ptr_type, address_data), opts));

  // Should only report the first 4 chars with a ... indicator.
  opts.max_array_size = 4;  // Truncate past this value.
  EXPECT_EQ(
      " = char*, \"ABCD\"...\n"
      "  [0] = char, 'A'\n"
      "  [1] = char, 'B'\n"
      "  [2] = char, 'C'\n"
      "  [3] = char, 'D'\n"
      "  ... = , \n",
      GetDebugTreeForValue(eval_context(), ExprValue(ptr_type, address_data), opts));
}

TEST_F(FormatTest, RustEnum) {
  auto rust_enum = MakeTestRustEnum();

  // Since "none" is the default, random discriminant values (here, the 32-bit "100" value) will
  // match it. It has no value, so the expectation has an awkward ", " at the end.
  ExprValue none_value(rust_enum, {100, 0, 0, 0,              // Discriminant
                                   0, 0, 0, 0, 0, 0, 0, 0});  // Unused
  FormatOptions opts;
  EXPECT_EQ(
      " = RustEnum, None\n"
      "  None = None, \n",
      GetDebugTreeForValue(eval_context(), none_value, opts));

  // Scalar value.
  ExprValue scalar_value(rust_enum, {0, 0, 0, 0,    // Discriminant
                                     51, 0, 0, 0,   // Scalar value.
                                     0, 0, 0, 0});  // Unused
  EXPECT_EQ(
      " = RustEnum, Scalar\n"
      "  Scalar = Scalar, \n"
      "    0 = int32_t, 51\n",
      GetDebugTreeForValue(eval_context(), scalar_value, opts));

  // Point value.
  ExprValue point_value(rust_enum, {1, 0, 0, 0,    // Discriminant
                                    1, 0, 0, 0,    // x
                                    2, 0, 0, 0});  // y
  EXPECT_EQ(
      " = RustEnum, Point\n"
      "  Point = Point, \n"
      "    x = int32_t, 1\n"
      "    y = int32_t, 2\n",
      GetDebugTreeForValue(eval_context(), point_value, opts));
}

TEST_F(FormatTest, RustTuple) {
  auto tuple_two_type = MakeRustTuple("(int32_t, uint64_t)", {MakeInt32Type(), MakeUint64Type()});
  ExprValue tuple_two(tuple_two_type, {123, 0, 0, 0,               // int32_t member 0
                                       78, 0, 0, 0, 0, 0, 0, 0});  // uint64_t member 1
  FormatOptions opts;
  EXPECT_EQ(
      " = (int32_t, uint64_t), \n"
      "  0 = int32_t, 123\n"
      "  1 = uint64_t, 78\n",
      GetDebugTreeForValue(eval_context(), tuple_two, opts));

  // 1-element tuple struct.
  auto tuple_struct_one_type = MakeRustTuple("Some", {MakeInt32Type()});
  ExprValue tuple_struct_one(tuple_struct_one_type, {123, 0, 0, 0});  // int32_t member 0
  EXPECT_EQ(
      " = Some, \n"
      "  0 = int32_t, 123\n",
      GetDebugTreeForValue(eval_context(), tuple_struct_one, opts));
}

TEST_F(FormatTest, Enumeration) {
  // Unsigned 64-bit enum.
  Enumeration::Map unsigned_map;
  unsigned_map[0] = "kZero";
  unsigned_map[1] = "kOne";
  unsigned_map[std::numeric_limits<uint64_t>::max()] = "kMax";
  auto unsigned_enum =
      fxl::MakeRefCounted<Enumeration>("UnsignedEnum", LazySymbol(), 8, false, unsigned_map);

  // Found value
  FormatOptions opts;
  EXPECT_EQ(" = UnsignedEnum, kZero\n",
            GetDebugTreeForValue(eval_context(), ExprValue(unsigned_enum, {0, 0, 0, 0, 0, 0, 0, 0}),
                                 opts));
  EXPECT_EQ(" = UnsignedEnum, kMax\n",
            GetDebugTreeForValue(
                eval_context(),
                ExprValue(unsigned_enum, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}), opts));

  // Found value forced to hex.
  FormatOptions hex_opts;
  hex_opts.num_format = FormatOptions::NumFormat::kHex;
  EXPECT_EQ(
      " = UnsignedEnum, 0xffffffffffffffff\n",
      GetDebugTreeForValue(
          eval_context(),
          ExprValue(unsigned_enum, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}), hex_opts));

  // Not found value.
  EXPECT_EQ(" = UnsignedEnum, 12\n",
            GetDebugTreeForValue(eval_context(),
                                 ExprValue(unsigned_enum, {12, 0, 0, 0, 0, 0, 0, 0}), opts));

  // Signed 32-bit enum.
  Enumeration::Map signed_map;
  signed_map[0] = "kZero";
  signed_map[static_cast<uint64_t>(-5)] = "kMinusFive";
  signed_map[static_cast<uint64_t>(std::numeric_limits<int32_t>::max())] = "kMax";
  auto signed_enum =
      fxl::MakeRefCounted<Enumeration>("SignedEnum", LazySymbol(), 4, true, signed_map);

  // Found values.
  EXPECT_EQ(" = SignedEnum, kZero\n",
            GetDebugTreeForValue(eval_context(), ExprValue(signed_enum, {0, 0, 0, 0}), opts));
  EXPECT_EQ(
      " = SignedEnum, kMinusFive\n",
      GetDebugTreeForValue(eval_context(), ExprValue(signed_enum, {0xfb, 0xff, 0xff, 0xff}), opts));

  // Not-found value.
  EXPECT_EQ(
      " = SignedEnum, -4\n",
      GetDebugTreeForValue(eval_context(), ExprValue(signed_enum, {0xfc, 0xff, 0xff, 0xff}), opts));

  // Not-found signed value printed as hex should be unsigned.
  EXPECT_EQ(" = SignedEnum, 0xffffffff\n",
            GetDebugTreeForValue(eval_context(), ExprValue(signed_enum, {0xff, 0xff, 0xff, 0xff}),
                                 hex_opts));
}

TEST_F(FormatTest, EmptyAndBadArray) {
  FormatOptions opts;

  // Array of two int32's: [1, 2]
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);

  // Empty array with valid pointer.
  auto empty_array_type = fxl::MakeRefCounted<ArrayType>(MakeInt32Type(), 0);
  EXPECT_EQ(" = int32_t[0], \n",
            GetDebugTreeForValue(
                eval_context(), ExprValue(empty_array_type, std::vector<uint8_t>(), source), opts));

  // Array type declares a size but there's no data.
  auto array_type = fxl::MakeRefCounted<ArrayType>(MakeInt32Type(), 1);
  EXPECT_EQ(" = Err: Array data (0 bytes) is too small for the expected size (4 bytes).\n",
            GetDebugTreeForValue(eval_context(),
                                 ExprValue(array_type, std::vector<uint8_t>(), source), opts));
}

TEST_F(FormatTest, TruncatedArray) {
  FormatOptions opts;
  opts.max_array_size = 2;

  // Array of two int32's: {1, 2}
  constexpr uint64_t kAddress = 0x1100;
  ExprValueSource source(kAddress);
  std::vector<uint8_t> data = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};

  auto array_type = fxl::MakeRefCounted<ArrayType>(MakeInt32Type(), 2);

  // This array has exactly the max size, we shouldn't mark it as truncated.
  EXPECT_EQ(
      " = int32_t[2], \n"
      "  [0] = int32_t, 1\n"
      "  [1] = int32_t, 2\n",
      GetDebugTreeForValue(eval_context(), ExprValue(array_type, data, source), opts));

  // This one is truncated.
  opts.max_array_size = 1;
  EXPECT_EQ(
      " = int32_t[2], \n"
      "  [0] = int32_t, 1\n"
      "  ... = , \n",
      GetDebugTreeForValue(eval_context(), ExprValue(array_type, data, source), opts));
}

// Tests printing nullptr_t which is defined as "typedef decltype(nullptr) nullptr_t;".
TEST_F(FormatTest, NullptrT) {
  // Clang and GCC currently defined "decltype(nullptr)" as an "unspecified" type. Our decoder will
  // force these to be the size of a pointer (the symbols don't seem to define a size).
  auto underlying_type = fxl::MakeRefCounted<Type>(DwarfTag::kUnspecifiedType);
  underlying_type->set_assigned_name("decltype(nullptr_t)");
  underlying_type->set_byte_size(8);

  // The nullptr_t is defined as a typedef for the above.
  auto nullptr_t_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kTypedef, underlying_type);
  nullptr_t_type->set_assigned_name("nullptr_t");

  ExprValue null_value(nullptr_t_type, {0, 0, 0, 0, 0, 0, 0, 0});

  FormatOptions opts;
  EXPECT_EQ(" = nullptr_t, 0x0\n", GetDebugTreeForValue(eval_context(), null_value, opts));
}

TEST_F(FormatTest, FunctionPtr) {
  // This is a function type. There isn't a corresponding C/C++ type for a function type (without a
  // pointer modifier) but we define it anyway in case it comes up (possibly another language).
  auto func_type = fxl::MakeRefCounted<FunctionType>(LazySymbol(), std::vector<LazySymbol>());

  // This type is "void (*)()"
  auto func_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, func_type);

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("MyFunc");

  // Map the address to point to the function.
  constexpr uint64_t kAddress = 0x1234;
  eval_context()->AddLocation(
      kAddress, Location(kAddress, FileLine("file.cc", 21), 0, symbol_context, function));

  // Function.
  FormatOptions opts;
  ExprValue null_func(func_type, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = void(), 0x0\n", GetDebugTreeForValue(eval_context(), null_func, opts));

  // Null function pointer.
  ExprValue null_ptr(func_ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = void (*)(), 0x0\n", GetDebugTreeForValue(eval_context(), null_ptr, opts));

  // Function pointer to unknown memory is printed in hex.
  EXPECT_EQ(" = void (*)(), 0x5\n",
            GetDebugTreeForValue(eval_context(), ExprValue(func_ptr_type, {5, 0, 0, 0, 0, 0, 0, 0}),
                                 opts));

  // Found symbol (matching kAddress) should be printed.
  ExprValue good_ptr(func_ptr_type, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = void (*)(), &MyFunc\n", GetDebugTreeForValue(eval_context(), good_ptr, opts));

  // Member function pointer. The type naming of function pointers is tested by the MemberPtr class,
  // and otherwise the code paths are the same, so here we only need to verify things are hooked up.
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType, "MyClass");

  auto member_func = fxl::MakeRefCounted<MemberPtr>(containing, func_type);
  ExprValue null_member_func_ptr(member_func, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = void (MyClass::*)(), 0x0\n",
            GetDebugTreeForValue(eval_context(), null_member_func_ptr, opts));

  // Member function to a known symbol. This doesn't resolve to something that looks like a class
  // member, but that's OK, wherever the address points to is what we print.
  ExprValue good_member_func_ptr(member_func, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = void (MyClass::*)(), &MyFunc\n",
            GetDebugTreeForValue(eval_context(), good_member_func_ptr, opts));

  // Numeric overrides force addresses instead of the resolved name.
  opts.num_format = FormatOptions::NumFormat::kHex;
  EXPECT_EQ(" = void (MyClass::*)(), 0x1234\n",
            GetDebugTreeForValue(eval_context(), good_member_func_ptr, opts));
}

// This tests pointers to member data. Pointers to member functions were tested by the FunctionPtr
// test.
TEST_F(FormatTest, MemberPtr) {
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType, "MyClass");

  auto int32_type = MakeInt32Type();
  auto member_int32 = fxl::MakeRefCounted<MemberPtr>(containing, int32_type);

  // Null pointer.
  FormatOptions opts;
  ExprValue null_member_ptr(member_int32, {0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = int32_t MyClass::*, 0x0\n",
            GetDebugTreeForValue(eval_context(), null_member_ptr, opts));

  // Regular pointer.
  ExprValue good_member_ptr(member_int32, {0x34, 0x12, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(" = int32_t MyClass::*, 0x1234\n",
            GetDebugTreeForValue(eval_context(), good_member_ptr, opts));
}

}  // namespace zxdb
