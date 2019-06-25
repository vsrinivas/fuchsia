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
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

namespace {

class FormatTest : public TestWithLoop {
 public:
  FormatTest() : eval_context_(fxl::MakeRefCounted<MockEvalContext>()) {}

  MockSymbolDataProvider* provider() { return eval_context_->data_provider(); }

  // Formats a given node synchronously.
  void SyncFormat(FormatNode* node, const FormatExprValueOptions& opts) {
    // Populate the value.
    bool called = false;
    FillFormatNodeValue(node, eval_context_, fit::defer_callback([&called]() {
                          debug_ipc::MessageLoop::Current()->QuitNow();
                          called = true;
                        }));
    if (!called)
      loop().Run();

    called = false;
    FillFormatNodeDescription(node, opts, eval_context_,
                              fit::defer_callback([&called]() {
                                debug_ipc::MessageLoop::Current()->QuitNow();
                                called = true;
                              }));
    if (!called)
      loop().Run();
  }

  std::unique_ptr<FormatNode> GetDescribedNode(
      const ExprValue& value, const FormatExprValueOptions& opts) {
    auto node = std::make_unique<FormatNode>(std::string(), value);
    SyncFormat(node.get(), opts);
    return node;
  }

  // Recursively describes all nodes in the given tree. If update_value is set,
  // the value of the node will also be refreshed.
  void RecursiveSyncDescribe(FormatNode* node, bool update_value,
                             const FormatExprValueOptions& opts) {
    if (update_value)
      SyncFormat(node, opts);
    else
      FillFormatNodeDescription(node, opts, eval_context_, {});

    for (auto& c : node->children())
      RecursiveSyncDescribe(c.get(), update_value, opts);
  }

  // Returns "<type>, <description>" for the given formatted node. Errors
  // are also output.
  std::string GetTypeDesc(const FormatNode* node) {
    if (node->err().has_error())
      return "Err: " + node->err().msg();
    return node->type() + ", " + node->description();
  }

  // Returns "<type>, <description>" for the given formatting.
  // On error, returns "Err: <msg>".
  std::string SyncTypeDesc(const ExprValue& value,
                           const FormatExprValueOptions& opts) {
    auto node = GetDescribedNode(value, opts);
    return GetTypeDesc(node.get());
  }

  // Recursively formats the values until everything is described and
  // outputs a hierarchical tree structure, each level indented two spaces.
  //
  // Note that normally the root name will be empty so it will start with
  // " = <type>, <description>"
  //
  // <name> = <type>, <description>
  //   <child name> = <child type>, <child description>
  //     <child level 2 name> = <child 2 type>, <child 2 description>
  //   <child name> = <child type>, <child description>
  std::string SyncTreeTypeDesc(const ExprValue& value,
                               const FormatExprValueOptions& opts) {
    auto node = std::make_unique<FormatNode>(std::string(), value);
    RecursiveSyncDescribe(node.get(), true, opts);

    std::string result;
    RecursiveTreeTypeDesc(node.get(), &result, 0);
    return result;
  }

 private:
  // Recursive backend for SyncTreeTypeDesc.
  void RecursiveTreeTypeDesc(const FormatNode* node, std::string* output,
                             int indent) {
    output->append(std::string(indent * 2, ' '));
    output->append(node->name());
    output->append(" = ");
    output->append(GetTypeDesc(node));
    output->append("\n");
    for (auto& c : node->children())
      RecursiveTreeTypeDesc(c.get(), output, indent + 1);
  }

  fxl::RefPtr<MockEvalContext> eval_context_;
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

TEST_F(FormatTest, Structs) {
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
      " = Pair, \n"
      "  first = Foo, \n"
      "    a = int32_t, 0x110011\n"
      "    b = int32_t&, 0x1100\n"
      "       = int32_t, 0x12\n"
      "  second = Foo, \n"
      "    a = int32_t, 0x330033\n"
      "    b = int32_t&, 0x1100\n"
      "       = int32_t, 0x12\n",
      SyncTreeTypeDesc(pair_value, opts));
}

TEST_F(FormatTest, Struct_Anon) {
  // Test an anonymous struct. Clang will generate structs with no names for
  // things like closures. This struct has no members.
  auto anon_struct = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  auto anon_struct_ptr = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(anon_struct));
  ExprValue anon_value(anon_struct_ptr,
                       {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  EXPECT_EQ(
      " = (anon struct)*, 0x1100\n"
      "  * = (anon struct), \n",
      SyncTreeTypeDesc(anon_value, FormatExprValueOptions()));
}

// Structure members can be marked as "artifical" by the compiler. We shouldn't
// print these.
TEST_F(FormatTest, Struct_Artificial) {
  auto int32_type = MakeInt32Type();
  auto foo_type =
      MakeCollectionType(DwarfTag::kStructureType, "Foo",
                         {{"normal", int32_type}, {"artificial", int32_type}});

  // Print without anything being marked artificial.
  ExprValue value(foo_type, {1, 0, 0, 0, 2, 0, 0, 0});
  EXPECT_EQ(
      " = Foo, \n"
      "  normal = int32_t, 1\n"
      "  artificial = int32_t, 2\n",
      SyncTreeTypeDesc(value, FormatExprValueOptions()));

  // Mark second one as artificial.
  DataMember* artificial_member = const_cast<DataMember*>(
      foo_type->data_members()[1].Get()->AsDataMember());
  artificial_member->set_artificial(true);

  EXPECT_EQ(
      " = Foo, \n"
      "  normal = int32_t, 1\n",
      SyncTreeTypeDesc(value, FormatExprValueOptions()));
}

// GDB and LLDB both print all members of a union and accept the possibility
// that sometimes one of them might be garbage, we do the same.
TEST_F(FormatTest, Union) {
  FormatExprValueOptions opts;

  // Define a union type with two int32 values.
  auto int32_type = MakeInt32Type();

  auto union_type =
      fxl::MakeRefCounted<Collection>(DwarfTag::kUnionType, "MyUnion");
  union_type->set_byte_size(int32_type->byte_size());

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
  EXPECT_EQ(
      " = MyUnion, \n"
      "  a = int32_t, 42\n"
      "  b = int32_t, 42\n",
      SyncTreeTypeDesc(value, opts));
}

// Tests formatting when a class has derived base classes.
TEST_F(FormatTest, DerivedClasses) {
  auto int32_type = MakeInt32Type();
  auto base = MakeCollectionType(DwarfTag::kStructureType, "Base",
                                 {{"a", int32_type}, {"b", int32_type}});

  // This second base class is empty, it should be omitted from the output.
  auto empty_base =
      fxl::MakeRefCounted<Collection>(DwarfTag::kClassType, "EmptyBase");

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

  // Only the Base should be printed, EmptyBase should be omitted because it
  // has no data.
  FormatExprValueOptions opts;
  EXPECT_EQ(
      " = Derived, \n"
      "  Base = Base, \n"
      "    a = int32_t, 1\n"
      "    b = int32_t, 2\n"
      "  c = int32_t, 3\n"
      "  d = int32_t, 4\n",
      SyncTreeTypeDesc(value, opts));
}

TEST_F(FormatTest, Pointer) {
  FormatExprValueOptions opts;

  auto base_type = MakeInt32Type();
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                    LazySymbol(base_type));

  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  ExprValue value(ptr_type, data);

  // The pointer points to invalid memory.
  EXPECT_EQ(
      " = int32_t*, 0x807060504030201\n"
      "  * = Err: Invalid pointer 0x807060504030201\n",
      SyncTreeTypeDesc(value, opts));

  // Provide some memory backing for the request.
  constexpr uint64_t kAddress = 0x807060504030201;
  provider()->AddMemory(kAddress, {123, 0, 0, 0});
  EXPECT_EQ(
      " = int32_t*, 0x807060504030201\n"
      "  * = int32_t, 123\n",
      SyncTreeTypeDesc(value, opts));

  // Test an invalid one with an incorrect size.
  data.resize(7);
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  ExprValue bad_value(ptr_type, data);
  EXPECT_EQ(
      " = Err: The value of type 'int32_t*' is the incorrect size (expecting "
      "8, got 7). Please file a bug.\n",
      SyncTreeTypeDesc(bad_value, opts));
}

TEST_F(FormatTest, Reference) {
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
  EXPECT_EQ(
      " = int&, 0x1100\n"
      "   = int, 123\n",
      SyncTreeTypeDesc(value, opts));

  // Test an invalid one with an invalid address.
  std::vector<uint8_t> bad_data = {0x00, 0x22, 0, 0, 0, 0, 0, 0};
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  value = ExprValue(ref_type, bad_data);
  EXPECT_EQ(
      " = int&, 0x2200\n"
      "   = Err: Invalid pointer 0x2200\n",
      SyncTreeTypeDesc(value, opts));

  // Test an rvalue reference. This is treated the same as a regular reference
  // from an interpretation and printing perspective.
  auto rvalue_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kRvalueReferenceType, LazySymbol(base_type));
  value = ExprValue(rvalue_ref_type, data);
  opts.verbosity = FormatExprValueOptions::Verbosity::kMedium;
  EXPECT_EQ(
      " = int&&, 0x1100\n"
      "   = int, 123\n",
      SyncTreeTypeDesc(value, opts));
}

TEST_F(FormatTest, RustEnum) {
  auto rust_enum = MakeTestRustEnum();

  // Since "none" is the default, random disciminant values (here, the 32-bit
  // "100" value) will match it.
  ExprValue none_value(rust_enum, {100, 0, 0, 0,              // Discriminant
                                   0, 0, 0, 0, 0, 0, 0, 0});  // Unused
  FormatExprValueOptions opts;
  EXPECT_EQ(" = RustEnum, None\n", SyncTreeTypeDesc(none_value, opts));

  // Scalar value.
  ExprValue scalar_value(rust_enum, {0, 0, 0, 0,    // Discriminant
                                     51, 0, 0, 0,   // Scalar value.
                                     0, 0, 0, 0});  // Unused
  EXPECT_EQ(
      " = RustEnum, Scalar\n"
      "  Scalar = Scalar, \n"
      "    0 = int32_t, 51\n",
      SyncTreeTypeDesc(scalar_value, opts));

  // Point value.
  ExprValue point_value(rust_enum, {1, 0, 0, 0,    // Discriminant
                                    1, 0, 0, 0,    // x
                                    2, 0, 0, 0});  // y
  EXPECT_EQ(
      " = RustEnum, Point\n"
      "  Point = Point, \n"
      "    x = int32_t, 1\n"
      "    y = int32_t, 2\n",
      SyncTreeTypeDesc(point_value, opts));
}

TEST_F(FormatTest, RustTuple) {
  auto tuple_two_type = MakeTestRustTuple("(int32_t, uint64_t)",
                                          {MakeInt32Type(), MakeUint64Type()});
  ExprValue tuple_two(tuple_two_type,
                      {123, 0, 0, 0,               // int32_t member 0
                       78, 0, 0, 0, 0, 0, 0, 0});  // uint64_t member 1
  FormatExprValueOptions opts;
  EXPECT_EQ(
      " = (int32_t, uint64_t), \n"
      "  0 = int32_t, 123\n"
      "  1 = uint64_t, 78\n",
      SyncTreeTypeDesc(tuple_two, opts));

  // 1-element tuple struct.
  auto tuple_struct_one_type = MakeTestRustTuple("Some", {MakeInt32Type()});
  ExprValue tuple_struct_one(tuple_struct_one_type,
                             {123, 0, 0, 0});  // int32_t member 0
  EXPECT_EQ(
      " = Some, \n"
      "  0 = int32_t, 123\n",
      SyncTreeTypeDesc(tuple_struct_one, opts));
}

}  // namespace zxdb
